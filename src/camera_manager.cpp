// ============================================================================
// camera_manager.cpp — Runtime camera backend dispatcher
// ============================================================================

#include "camera_manager.h"
#include "dji_camera.h"
#include "gopro_camera.h"
#include "ble_recycler.h"
#include "ble_log.h"
#include "ble_worker.h"
#include "cpu_stats.h"
#include "camera_slot_policy.h"
#include "ble_link.h"
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>

static_assert(NIMBLE_MAX_CONNECTIONS == CAMERA_CONNECTION_SLOTS,
              "NimBLE and camera slot limits must match");

// Arduino core 3.3+ frees the BT controller memory in initArduino() unless a
// BLE library registers itself through this header's static constructor.
// NimBLE-Arduino 1.4.1 predates that mechanism, so without this include
// esp_bt_controller_init() returns ESP_ERR_INVALID_STATE and the board
// boot-loops with "ESP_ERROR_CHECK failed" inside NimBLEDevice::init().
#if __has_include("esp32-hal-alloc-ble-mem.h")
#include "esp32-hal-alloc-ble-mem.h"
#endif

// ──────────────────────────────────────────────────────────────────────────────
// Security & Concurrency Globals
// ──────────────────────────────────────────────────────────────────────────────
// This firmware is NOT single-threaded. Two tasks touch camera state:
//   • the Arduino loop() task (core 1 on the classic ESP32, core 0 on the C3)
//   • the NimBLE host task (pinned to core 0)
// BLE callbacks (scan results, connection events, notifications) run on the
// host task, so anything they write while loop() reads it needs protection.
// That is handled close to the data: cam_registry::camRegistryRemember() and
// scan_results::scanResultsAdd() use their own critical sections, and the
// deferred log ring (ble_log) hands payloads over under a portMUX.
//
// The two mutexes below remain for the rare cross-task disconnect/scan path,
// but they use short timeouts (never portMAX_DELAY) so a wedged BLE host task
// can never deadlock loop().
// ──────────────────────────────────────────────────────────────────────────────

static SemaphoreHandle_t g_stateMutex = NULL;
static SemaphoreHandle_t g_scanMutex = NULL;

// Short lock timeout so loop() never blocks forever on the BLE host task.
static const uint32_t kCamMutexTimeoutMs = 50;

// Helper to safely sanitize device names (prevent XSS injection via BLE ads)
void sanitizeDeviceName(char* dest, const char* src, size_t maxSize) {
    if (!dest || !src || maxSize == 0) return;
    
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < maxSize - 1; i++) {
        char c = src[i];
        // Allow only alphanumeric, space, dash, underscore, dot
        if ((c >= 'a' && c <= 'z') || 
            (c >= 'A' && c <= 'Z') || 
            (c >= '0' && c <= '9') || 
            c == ' ' || c == '-' || c == '_' || c == '.') {
            dest[j++] = c;
        } else {
            // Replace unsafe chars with '?'
            dest[j++] = '?';
        }
    }
    dest[j] = '\0';
}

// Thread-safe initialization of mutexes
static void initMutexes() {
    if (g_stateMutex == NULL) {
        g_stateMutex = xSemaphoreCreateMutex();
        configASSERT(g_stateMutex);
    }
    if (g_scanMutex == NULL) {
        g_scanMutex = xSemaphoreCreateMutex();
        configASSERT(g_scanMutex);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Internal state
// ──────────────────────────────────────────────────────────────────────────────

static bool _stackReady = false;

struct CameraSlot {
    CameraBackend *backend = nullptr;
    uint8_t type = CAMERA_DJI;
    char mac[18] = "";
    // Per-camera recording intent. A camera that is offline keeps the command
    // pending and receives it immediately after reconnecting (step 2).
    int8_t pendingCmd = 0;          // 1 = start, -1 = stop, 0 = nothing to send
    uint32_t lastAttemptMs = 0;
    uint32_t retryBackoffMs = BLE_RECONNECT_INTERVAL_MS;
    bool wasReady = false;
};
static CameraSlot _slots[CAMERA_CONNECTION_SLOTS];
static bool _recordingDesired = false;
static uint8_t _nextSlot = 0;
// Slot whose connect job currently owns the BLE worker, and since when.
static uint8_t  _jobSlot = 0xFF;
static uint32_t _jobStartMs = 0;
static bool _connectionsEnabled = true;
static bool _kickPending = false;
static bool _selectedPending = false;
static bool _knownScanning = false;
// User discovery scan requested while the BLE worker was still busy; started
// from camUpdate() as soon as it is free (instead of delay() in the handler).
static bool _userScanPending = false;
static uint32_t _userScanRequestMs = 0;
static const uint32_t kUserScanWaitMs = 3000;
static uint32_t _lastKnownScan = 0;
static uint32_t _knownScanBackoff = BLE_RECONNECT_INTERVAL_MS;
static uint8_t _visibleMask = 0;
static uint8_t _addressTypes[MAX_SAVED_CAMERAS];
static SavedCamera _scanSaved[MAX_SAVED_CAMERAS];
static uint8_t _scanSavedCount = 0;
static portMUX_TYPE _knownMux = portMUX_INITIALIZER_UNLOCKED;

static int8_t savedIndex(const CameraSlot &slot) {
    const OpenSlateSettings &s = settingsGet();
    for (uint8_t i = 0; i < s.camCount; i++) {
        if (slot.type == s.cams[i].type &&
            strcasecmp(slot.mac, s.cams[i].mac) == 0) return i;
    }
    return -1;
}

static int8_t primarySlot() {
    int8_t fallback = -1;
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        if (!_slots[i].backend) continue;
        int8_t index = savedIndex(_slots[i]);
        if (index < 0) continue;
        if (settingsGet().cams[index].active) return i;
        if (fallback < 0) fallback = i;
    }
    return fallback;
}

static CameraBackend *primaryBackend() {
    int8_t index = primarySlot();
    return index < 0 ? nullptr : _slots[index].backend;
}

class KnownCameraScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *device) override {
        std::string mac = device->getAddress().toString();
        portENTER_CRITICAL(&_knownMux);
        for (uint8_t i = 0; i < _scanSavedCount; i++) {
            if (strcasecmp(mac.c_str(), _scanSaved[i].mac) == 0) {
                _visibleMask |= (1u << i);
                _addressTypes[i] = device->getAddressType();
                break;
            }
        }
        portEXIT_CRITICAL(&_knownMux);
    }
};
static KnownCameraScanCallbacks _knownScanCallbacks;

static void knownScanComplete(NimBLEScanResults) {}

static void clearSlot(uint8_t index) {
    CameraSlot &slot = _slots[index];
    if (slot.backend) slot.backend->release();
    slot.backend = nullptr;
    slot.mac[0] = '\0';
    slot.pendingCmd = 0;
    slot.wasReady = false;
}

static void assignSlot(uint8_t index, const SavedCamera &camera, uint8_t addressType) {
    CameraSlot &slot = _slots[index];
    slot.type = camera.type;
    strlcpy(slot.mac, camera.mac, sizeof(slot.mac));
    slot.backend = camera.type == CAMERA_GOPRO ? &gpCamera(index) : &djiCamera(index);
    slot.backend->init();
    slot.backend->targetMac(camera.mac, addressType);
    // A camera that joins while recording is desired must start recording too.
    slot.pendingCmd = _recordingDesired ? 1 : 0;
    slot.retryBackoffMs = BLE_RECONNECT_INTERVAL_MS;
    slot.lastAttemptMs = millis() - slot.retryBackoffMs;
    slot.wasReady = false;
    DBG("CAM: slot %u assigned to %s (%s)", (unsigned)index + 1,
        camera.mac, cameraTypeName((CameraType)camera.type));
}

static void updateKnownScan() {
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (_knownScanning) {
        if (scan->isScanning()) return;
        _knownScanning = false;
        _lastKnownScan = millis();
        uint8_t visible, addressTypes[MAX_SAVED_CAMERAS];
        portENTER_CRITICAL(&_knownMux);
        visible = _visibleMask;
        memcpy(addressTypes, _addressTypes, sizeof(addressTypes));
        portEXIT_CRITICAL(&_knownMux);
        uint8_t assigned = 0;
        int8_t preferred = -1;
        const OpenSlateSettings &s = settingsGet();
        // Never apply a snapshot to a registry that changed during this scan.
        if (_scanSavedCount != s.camCount) return;
        for (uint8_t i = 0; i < s.camCount; i++) {
            if (s.cams[i].type != _scanSaved[i].type ||
                strcasecmp(s.cams[i].mac, _scanSaved[i].mac) != 0) return;
            if (s.cams[i].active) preferred = i;
        }
        for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
            int8_t saved = savedIndex(_slots[i]);
            if (_slots[i].backend && saved >= 0) assigned |= (1u << saved);
        }
        bool added = false;
        if (_selectedPending && preferred >= 0) {
            if (assigned & (1u << preferred)) {
                _selectedPending = false;
            } else if (visible & (1u << preferred)) {
                int8_t target = -1;
                for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
                    if (!_slots[i].backend) { target = i; break; }
                }
                if (target < 0) {
                    int8_t primary = primarySlot();
                    target = primary == 0 ? 1 : 0;
                    int8_t oldSaved = savedIndex(_slots[target]);
                    if (oldSaved >= 0) assigned &= ~(1u << oldSaved);
                    clearSlot(target);
                }
                assignSlot(target, s.cams[preferred], addressTypes[preferred]);
                assigned |= (1u << preferred);
                _selectedPending = false;
                added = true;
            }
        } else {
            _selectedPending = false;
        }
        for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
            if (_slots[i].backend) continue;
            int8_t candidate = cameraNextVisible(visible, assigned, s.camCount, preferred);
            if (candidate < 0) break;
            assignSlot(i, s.cams[candidate], addressTypes[candidate]);
            assigned |= (1u << candidate);
            added = true;
        }
        _knownScanBackoff = added ? BLE_RECONNECT_INTERVAL_MS :
            cameraRetryBackoff(_knownScanBackoff, BLE_RECONNECT_MAX_MS);
        return;
    }
    bool hasFreeSlot = false;
    uint8_t assignedCount = 0;
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        if (!_slots[i].backend) hasFreeSlot = true;
        else assignedCount++;
    }
    if ((!_selectedPending && (!hasFreeSlot || assignedCount >= settingsGet().camCount)) ||
        !settingsGet().camCount || camIsOtaQuiet() ||
        scan->isScanning() || bleWorkerBusy() ||
        millis() - _lastKnownScan < _knownScanBackoff) return;
    portENTER_CRITICAL(&_knownMux);
    _scanSavedCount = settingsGet().camCount;
    memcpy(_scanSaved, settingsGet().cams, sizeof(_scanSaved));
    _visibleMask = 0;
    portEXIT_CRITICAL(&_knownMux);
    scan->setAdvertisedDeviceCallbacks(&_knownScanCallbacks, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(40);
    scan->setMaxResults(0);
    if (scan->start(BLE_KNOWN_SCAN_SECONDS, knownScanComplete, false)) {
        _knownScanning = true;
    } else {
        DBG("CAM: known-camera scan failed");
        _lastKnownScan = millis();
        _knownScanBackoff = cameraRetryBackoff(_knownScanBackoff, BLE_RECONNECT_MAX_MS);
    }
}

// OTA radio-quiet: when true the shared 2.4GHz radio belongs to WiFi.
// Backends check camIsOtaQuiet() before any scan/reconnect/keep-alive.
static bool _otaQuiet = false;

void camSetOtaQuiet(bool quiet) {
    _otaQuiet = quiet;
    if (quiet) {
        // Stop any in-progress discovery scan immediately so beacons +
        // the OTA TCP stream get full airtime. Existing BLE *connection*
        // is kept (dropping it would lose the bond session); only new
        // radio activity is suppressed — see dji/gopro update guards.
        NimBLEScan *pScan = NimBLEDevice::getScan();
        if (pScan && pScan->isScanning()) pScan->stop();
        _knownScanning = false;
        _lastKnownScan = millis();
        DBG("CAM: OTA quiet ON — BLE scan stopped, reconnect paused");
    } else {
        DBG("CAM: OTA quiet OFF — BLE activity resumed");
    }
}

bool camIsOtaQuiet() { return _otaQuiet; }

static void shutdownSlots() {
    // Stop shared discovery and release both independent connections.
    NimBLEScan *pScan = NimBLEDevice::getScan();
    if (pScan && pScan->isScanning()) {
        pScan->stop();
    }
    // H06: disconnect() alone leaks the NimBLEClient (and its GATT attribute
    // table) for the rest of the boot. The backend knows which client is its
    // own, so let it release the object — disposal itself is deferred to the
    // next update tick because NimBLEDevice::deleteClient() blocks on GAP.
    _knownScanning = false;
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) clearSlot(i);
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

void camInit() {
    if (_stackReady) return;

    // Initialize thread-safety primitives first
    initMutexes();

    // E04: the deferred log ring must be empty before any BLE callback can
    // push into it.
    bleLogInit();
    // CPU sampling reads the loop task's handle, so it must run on loop().
    cpuStatsInit();

    DBG("CAM: Initialising NimBLE stack...");
    NimBLEDevice::init("ESP32-OpenSlate");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    // Bonding enabled (needed for GoPro LE pairing); Just Works IO caps.
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    // P02: the connect worker must exist before the first backend update tick
    // tries to submit a connect job.
    bleWorkerInit();

    _stackReady = true;

    _lastKnownScan = millis() - _knownScanBackoff;
    DBG("CAM: two independent connection slots ready");
}

// ──────────────────────────────────────────────────────────────────────────────
// Per-camera recording commands (step 2)
// ──────────────────────────────────────────────────────────────────────────────
// Commands are absolute, so repeating them after a reconnect is always safe.
// Each slot keeps its own pending command and backoff: a stickcam that dropped
// out of range receives its STOP as soon as it is back, independently of the
// copter camera.
static void updateSlotCommands() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        CameraSlot &slot = _slots[i];
        if (!slot.backend) continue;
        bool ready = slot.backend->isReady();
        bool readyEdge = ready && !slot.wasReady;
        slot.wasReady = ready;
        // The camera refused the START (busy, card full …): try again at the
        // slow retry rate for as long as recording is wanted.
        if (slot.pendingCmd == 0 && _recordingDesired && ready &&
            slot.backend->recordRejected() &&
            now - slot.lastAttemptMs >= BLE_RECONNECT_MAX_MS) {
            slot.pendingCmd = 1;
            slot.retryBackoffMs = BLE_RECONNECT_MAX_MS;
        }
        if (slot.pendingCmd == 0) continue;
        if (readyEdge) {
            // Link just came back: send the deferred command now instead of
            // waiting out a backoff that grew while the camera was away.
            slot.retryBackoffMs = BLE_RECONNECT_INTERVAL_MS;
            slot.lastAttemptMs = now - slot.retryBackoffMs;
        }
        if (now - slot.lastAttemptMs < slot.retryBackoffMs) continue;
        slot.lastAttemptMs = now;
        if (!ready) {
            slot.retryBackoffMs = cameraRetryBackoff(slot.retryBackoffMs,
                                                     BLE_RECONNECT_MAX_MS);
            continue;
        }
        bool sent = slot.pendingCmd > 0 ? slot.backend->sendStartRecord()
                                        : slot.backend->sendStopRecord();
        if (sent) {
            slot.pendingCmd = 0;
            slot.retryBackoffMs = BLE_RECONNECT_INTERVAL_MS;
        } else {
            slot.retryBackoffMs = cameraRetryBackoff(slot.retryBackoffMs,
                                                     BLE_RECONNECT_MAX_MS);
        }
    }
}

// A search attempt (ble_link.h) may hold the single BLE worker for up to
// BLE_CONNECT_TIMEOUT_MS. After BLE_CONNECT_SLICE_MS, hand it over whenever
// another slot is waiting to reconnect or a known-camera scan is due — so an
// absent stickcam never delays the copter camera, and two absent cameras
// share the radio in slices.
static void yieldSearchIfOthersWait() {
    if (!bleWorkerBusy() || !bleLinkSearching()) return;
    if (millis() - _jobStartMs < BLE_CONNECT_SLICE_MS) return;
    bool waiting = false;
    bool hasFreeSlot = false;
    uint8_t assignedCount = 0;
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        if (!_slots[i].backend) { hasFreeSlot = true; continue; }
        assignedCount++;
        if (i != _jobSlot && _slots[i].backend->getState() == BLE_DISCONNECTED) waiting = true;
    }
    if (hasFreeSlot && assignedCount < settingsGet().camCount &&
        millis() - _lastKnownScan >= _knownScanBackoff) waiting = true;
    if (waiting && bleLinkYield()) {
        DBG("CAM: search slice of slot %u over — handing the BLE worker over", _jobSlot + 1);
    }
}

void camUpdate() {
    if (!_stackReady) return;
    // H06: free clients released by a previous disconnect (or by a backend we
    // have since switched away from) as soon as the GAP teardown completed.
    bleRecycleProcess();
    // E04: emit the log records the BLE host task handed over instead of
    // letting it block on the UART.
    bleLogProcess();
    // CPU panel data — throttled internally to CPU_SAMPLE_INTERVAL_MS.
    cpuStatsUpdate();
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        if (_slots[i].backend && savedIndex(_slots[i]) < 0) clearSlot(i);
    }
    if (_userScanPending) {
        if (!bleWorkerBusy()) {
            _userScanPending = false;
            if (camPrepareUserScan()) camStartUserScan();
            else DBG("CAM: deferred discovery refused (BLE busy)");
        } else if (millis() - _userScanRequestMs >= kUserScanWaitMs) {
            _userScanPending = false;
            DBG("CAM: deferred discovery dropped, worker stayed busy");
        }
    }
    if (!bleWorkerBusy()) {
        if (_kickPending) camKick();
        if (_connectionsEnabled && !camIsOtaQuiet()) updateKnownScan();
    }
    yieldSearchIfOthersWait();
    bool allowConnect = _connectionsEnabled && !camIsOtaQuiet() &&
                        !NimBLEDevice::getScan()->isScanning();
    uint8_t first = _nextSlot;
    for (uint8_t n = 0; n < CAMERA_CONNECTION_SLOTS; n++) {
        uint8_t i = (first + n) % CAMERA_CONNECTION_SLOTS;
        if (!_slots[i].backend) continue;
        bool busyBefore = bleWorkerBusy();
        _slots[i].backend->update(allowConnect);
        if (!busyBefore && bleWorkerBusy()) {
            _nextSlot   = (i + 1) % CAMERA_CONNECTION_SLOTS;
            _jobSlot    = i;
            _jobStartMs = millis();
        }
    }
    updateSlotCommands();
}

void camSetRecordingDesired(bool on) {
    if (!_stackReady) return;
    _recordingDesired = on;
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        CameraSlot &slot = _slots[i];
        if (!slot.backend) continue;
        slot.pendingCmd = on ? 1 : -1;
        slot.retryBackoffMs = BLE_RECONNECT_INTERVAL_MS;
        slot.lastAttemptMs = millis() - slot.retryBackoffMs;
    }
    // Send on the edge itself so a ready camera reacts without a loop delay.
    updateSlotCommands();
}

bool camRecordingDesired() { return _recordingDesired; }

BleConnectionState camGetState() {
    if (!_stackReady) return BLE_DISCONNECTED;
    CameraBackend *backend = primaryBackend();
    if (backend) return backend->getState();
    return _knownScanning ? BLE_SCANNING : BLE_DISCONNECTED;
}

const CameraTelemetry& camGetTelemetry() {
    static CameraTelemetry empty;
    if (!_stackReady) return empty;
    CameraBackend *backend = primaryBackend();
    return backend ? backend->getTelemetry() : empty;
}

bool camIsReady() {
    if (!_stackReady) return false;
    CameraBackend *backend = primaryBackend();
    return backend && backend->isReady();
}

const char* camGetName() {
    int8_t index = primarySlot();
    return cameraTypeName(index < 0 ? settingsGet().camera : (CameraType)_slots[index].type);
}

void camSetCamera(CameraType type) {
    if (_stackReady && type != settingsGet().camera) {
        DBG("CAM: Switching backend to %s", cameraTypeName(type));
        settingsGet().camera = type;
        settingsSave();
    }
}

void camKick() {
    if (!_stackReady) return;
    _connectionsEnabled = true;
    _kickPending = true;
    // User action: end a background search now; the kick runs next tick.
    if (bleWorkerBusy()) { bleLinkYield(); return; }
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan->isScanning()) scan->stop();
    _knownScanning = false;

    // Find the active saved camera (if any).
    OpenSlateSettings &s = settingsGet();
    int8_t active = -1;
    for (uint8_t i = 0; i < s.camCount; i++) {
        if (s.cams[i].active) { active = (int8_t)i; break; }
    }
    if (active < 0) {
        _kickPending = false;
        DBG("CAM: kick requested but no active saved camera");
        return;
    }

    SavedCamera &c = s.cams[active];
    if ((CameraType)c.type != s.camera) {
        camSetCamera((CameraType)c.type);
    }

    _kickPending = false;
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        if (_slots[i].backend && savedIndex(_slots[i]) == active) return;
    }
    // Resolve presence and BLE address type before replacing a live slot.
    // An offline selection must not evict either connected camera.
    _selectedPending = true;
    _knownScanBackoff = BLE_RECONNECT_INTERVAL_MS;
    _lastKnownScan = millis() - _knownScanBackoff;
}

// Disconnect current camera and stop BLE operations (for UI disconnect)
void camDisconnect() {
    // Short-timeout lock: never stall loop() on the BLE host task.
    bool locked = (g_stateMutex != NULL &&
                   xSemaphoreTake(g_stateMutex,
                                  pdMS_TO_TICKS(kCamMutexTimeoutMs)) == pdTRUE);
    
    shutdownSlots();
    _connectionsEnabled = false;
    _kickPending = false;
    _selectedPending = false;
    
    // Clear active camera flag in settings
    OpenSlateSettings &s = settingsGet();
    for (uint8_t i = 0; i < s.camCount; i++) {
        if (s.cams[i].active) {
            s.cams[i].active = false;
            break;
        }
    }
    settingsSave();
    
    DBG("CAM: both connection slots disconnected");
    
    if (locked) {
        xSemaphoreGive(g_stateMutex);
    }
}

// User-initiated one-shot discovery scan.  Called from the /api/camera
// {scan:true} endpoint. Separate from the short, saved-camera-only scans.
void camStartUserScan() {
    if (!_stackReady) return;
    if (!camPrepareUserScan()) {
        DBG("CAM: discovery refused while BLE is busy");
        return;
    }
    
    // Short-timeout lock: skip (don't queue) if the BLE task holds it.
    bool locked = (g_scanMutex != NULL &&
                   xSemaphoreTake(g_scanMutex,
                                  pdMS_TO_TICKS(kCamMutexTimeoutMs)) == pdTRUE);
    
    CameraType t = settingsGet().camera;
    DBG("CAM: user-initiated scan (backend=%s)", cameraTypeName(t));

    if (t == CAMERA_GOPRO) gpStartScan();
    else                    djiStartScan();
    
    if (locked) {
        xSemaphoreGive(g_scanMutex);
    }
}

bool camRequestUserScan() {
    if (!_stackReady || camIsOtaQuiet()) return false;
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        if (_slots[i].backend && _slots[i].backend->getState() == BLE_AUTHENTICATING)
            return false;   // never disturb a pairing in progress
    }
    if (bleWorkerBusy()) {
        // A background search may hold the worker: ask it to end its slice
        // and start the scan from camUpdate() once the worker is free.
        bleLinkYield();
        _userScanPending = true;
        _userScanRequestMs = millis();
        return true;
    }
    if (!camPrepareUserScan()) return false;
    camStartUserScan();
    return true;
}

bool camPrepareUserScan() {
    if (!_stackReady || camIsOtaQuiet()) return false;
    if (bleWorkerBusy()) return false;
    if (_knownScanning) {
        NimBLEDevice::getScan()->stop();
        _knownScanning = false;
        _lastKnownScan = millis();
    }
    if (NimBLEDevice::getScan()->isScanning()) return false;
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        if (_slots[i].backend && _slots[i].backend->getState() == BLE_AUTHENTICATING)
            return false;
    }
    return true;
}

uint8_t camConnectedCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        if (_slots[i].backend && _slots[i].backend->isReady()) count++;
    }
    return count;
}

uint8_t camAssignedCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        if (_slots[i].backend) count++;
    }
    return count;
}

bool camSavedIsReady(uint8_t saved) {
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        if (_slots[i].backend && savedIndex(_slots[i]) == saved)
            return _slots[i].backend->isReady();
    }
    return false;
}

bool camSavedStatus(uint8_t saved, CamSavedStatus &out) {
    out = CamSavedStatus();
    for (uint8_t i = 0; i < CAMERA_CONNECTION_SLOTS; i++) {
        const CameraSlot &slot = _slots[i];
        if (!slot.backend || savedIndex(slot) != (int8_t)saved) continue;
        out.assigned    = true;
        out.ready       = slot.backend->isReady();
        out.stopPending = slot.pendingCmd < 0;
        out.recRejected = slot.backend->recordRejected();
        out.tel         = slot.backend->getTelemetry();   // copy
        return true;
    }
    return false;
}

const char* camGetLastError() {
    CameraBackend *backend = primaryBackend();
    return backend ? backend->getLastError() : "";
}
