// ============================================================================
// dji_camera.cpp — NimBLE BLE Central for DJI Osmo Action (DUML protocol)
// ============================================================================
// Implements the DUML-over-BLE protocol of the DJI Osmo Action family.
// Field-tested: pairing, record start/stop, battery telemetry.
// ============================================================================

#include "dji_camera.h"
#include "ble_link.h"
#include "duml_codec.h"
#include "cam_registry.h"
#include "scan_results.h"
#include "settings.h"
#include "camera_manager.h"
#include "ble_recycler.h"
#include "ble_log.h"
#include "ble_worker.h"
#include "camera_slot_policy.h"
#include <NimBLEDevice.h>
#include <atomic>
// ──────────────────────────────────────────────────────────────────────────────
// DJI GATT UUIDs
// ──────────────────────────────────────────────────────────────────────────────
static NimBLEUUID DJI_SERVICE_UUID("0000fff0-0000-1000-8000-00805f9b34fb");
static NimBLEUUID DJI_CHAR_FFF3("0000fff3-0000-1000-8000-00805f9b34fb");
static NimBLEUUID DJI_CHAR_FFF4("0000fff4-0000-1000-8000-00805f9b34fb"); // Pairing Arm + Notifications
static NimBLEUUID DJI_CHAR_FFF5("0000fff5-0000-1000-8000-00805f9b34fb"); // DUML Commands (WriteNoResponse)

// ──────────────────────────────────────────────────────────────────────────────
// Internal state
// ──────────────────────────────────────────────────────────────────────────────
class DjiCamera : public CameraBackend, private NimBLEClientCallbacks {
public:
    void init() override;
    void update(bool allowConnect) override;
    void release() override;
    void targetMac(const char *mac, uint8_t addressType) override;
    bool sendStartRecord() override;
    bool sendStopRecord() override;
    BleConnectionState getState() const override;
    const CameraTelemetry& getTelemetry() const override;
    bool isReady() const override;
    const char* getLastError() const override;

private:
    void onConnect(NimBLEClient *client) override;
    void onDisconnect(NimBLEClient *client) override;
    bool connectToCamera();
    bool connectJob();
    void connectAbort(NimBLEClient *client);
    void applyConnectedState(NimBLEClient *client);
    void notifyCallback(NimBLERemoteCharacteristic *pChar, uint8_t *pData,
                        size_t length, bool isNotify);
    void sendPairingArm();
    void sendSetPairingPIN();
    void sendKeepAlive();
    void sendApprovalAck();
    void noteConnectFailure(BleConnectOutcome outcome = BLE_CONNECT_FAILED);
    bool onConnParamsUpdateRequest(NimBLEClient *client, const ble_gap_upd_params *params) override;

    std::atomic<BleConnectionState> _bleState{BLE_DISCONNECTED};
    CameraTelemetry _telemetry;
    NimBLEClient *_pClient = nullptr;
    NimBLERemoteCharacteristic *_pControlChar = nullptr; // fff5
    NimBLERemoteCharacteristic *_pTelemetryChar = nullptr; // fff3
    NimBLERemoteCharacteristic *_pAuthChar = nullptr; // fff4
    NimBLEAddress _targetAddress;
    bool _hasTargetAddress = false;
    bool _doConnect = false;
    bool _sessionEstablished = false;
    uint16_t _sequenceCounter = 1;
    uint32_t _lastReconnectAttempt = 0;
    uint32_t _authBackoffMs = BLE_RECONNECT_INTERVAL_MS;
    uint32_t _reconnectBackoffMs = BLE_RECONNECT_INTERVAL_MS;
    uint32_t _lastKeepAlive = 0;
    uint32_t _pairingArmedTime = 0;
    uint32_t _authStartMs = 0;
    uint32_t _lastRxMs = 0;
    // DJI recording time is counted locally from an accepted absolute START.
    uint32_t _recStartMs = 0;
    bool _recActive = false;
    bool _lastCmdWasStart = false;
    // E04: set by notifyCallback (NimBLE host task) when the camera reports
    // "pairing approved"; the ACK write itself happens in update() (loop()).
    std::atomic<bool> _approvalAckPending{false};
    // Reconnect timing (bench diagnostics): stamped on the worker / host task,
    // printed once from update() when the session is up.
    uint32_t _tLost = 0, _tAttempt = 0, _tLink = 0, _tSvc = 0, _tSub = 0, _tSession = 0;
    std::atomic<bool> _timingPending{false};
    // Session wake (BLE wake sequence): once the session is
    // established, send 0x53/0x10 to 0x1C once and then the 0x00/0x2B session
    // ping at ~1 Hz. Action 4+ only push status/battery/storage after this;
    // the Action 2 ignores both (verified) and is polled for battery instead.
    // Set on the host task, consumed in update().
    std::atomic<bool> _wakePending{false};
    uint32_t _wakeDueMs = 0;
    bool     _wakeArmed = false;
    uint32_t _lastPingMs = 0;
    uint32_t _lastBattPollMs = 0;
    std::atomic<bool> _battPushSeen{false};  // camera pushes 0x0D/0x02 itself -> stop polling
    // Frame logger state (host task only): log each (flags,set,id) the first
    // time it is seen and again whenever its payload changes (rate-limited),
    // so unknown telemetry can be mapped from the serial log.
    struct SeenFrame { uint32_t key; uint32_t hash; uint32_t ms; };
    SeenFrame _seen[24] = {};
    uint8_t   _seenCount = 0;
    void logFrameIfNew(const uint8_t *pData, size_t length, uint8_t nBytes);
    void sendWake();
    void sendSessionPing();
    void sendBatteryQuery();
    void parseBattery(const uint8_t *p, size_t n);
    std::atomic<uint32_t> _connectGen{0};
    std::atomic<bool> _jobActive{false};
    struct DjiConnectJob {
        NimBLEClient *client;
        NimBLEAddress address;
        uint32_t gen;
    } _connectJob;
    char _lastError[48] = "";
};

CameraBackend& djiCamera(uint8_t slot) {
    static DjiCamera cameras[CAMERA_CONNECTION_SLOTS];
    configASSERT(slot < CAMERA_CONNECTION_SLOTS);
    return cameras[slot];
}

// ──────────────────────────────────────────────────────────────────────────────
// Forward declarations
// Note: djiStartScan() is declared in dji_camera.h (non-static) so it can
// be called from camera_manager.cpp via camStartUserScan().
// ──────────────────────────────────────────────────────────────────────────────
void                djiStartScan();
static bool    isDjiDevice(NimBLEAdvertisedDevice *device);
// DUML wire format lives in duml_codec.cpp (pure, unit-tested on the host).
static size_t  buildDumlPacket(uint8_t *buffer, size_t bufCap, uint8_t sender, uint8_t receiver,
                               uint16_t msgId, uint8_t flags, uint8_t cmdSet, uint8_t cmdId,
                               const uint8_t *payload, size_t payloadLen);

// ──────────────────────────────────────────────────────────────────────────────
// BLE Callbacks
// ──────────────────────────────────────────────────────────────────────────────
// onConnect/onDisconnect run on whichever task drove the GAP event — with the
// P02 worker that is the ble_worker task for the connect half, and the NimBLE
// host task for the disconnect half. Neither may log directly, so both hand a
// record to the deferred ring (E04).
void DjiCamera::onConnect(NimBLEClient *pClient) {
    if (pClient != _pClient) return;
    bleLogPush(BLE_LOG_DJI_CONNECTED);
}
void DjiCamera::onDisconnect(NimBLEClient *pClient) {
    if (pClient != _pClient) return;
    bleLogPush(BLE_LOG_DJI_DISCONNECTED);
    if (_sessionEstablished) _tLost = millis();   // link loss of a live session
    _sessionEstablished = false;
    _pControlChar = nullptr;
    _pTelemetryChar = nullptr;
    _pAuthChar = nullptr;
    _telemetry.dataValid = false;
    _telemetry.state = CAM_STATE_UNKNOWN;
    _telemetry.batteryPercent = 255;   // never show a stale % after a link loss
    _telemetry.batteryMv = 0;
    _telemetry.batteryTempC10 = INT16_MIN;
    _recActive = false;
    _recStartMs = 0;
    _approvalAckPending = false;
    _wakePending = false;
    _seenCount = 0;   // host task owns _seen: re-log every frame type after a reconnect
    _lastReconnectAttempt = millis();
    _bleState = BLE_DISCONNECTED;
}

// ──────────────────────────────────────────────────────────────────────────────
// RTOS-SAFE scan callback. Runs on the NimBLE host task — any blocking call
// (Preferences, Serial.printf, snprintf to a long string, malloc of large
// buffers) will starve the BLE stack and trip the RTOS watchdog.
// Rules enforced below:
//   • NO Preferences/NVS writes (deferred to camRegistryProcess() in loop()).
//   • NO DBG/Serial output — log_printf() busy-waits on the UART TX FIFO, so a
//     ~100 char line costs the host task ~9 ms. Records go through the
//     deferred ring (bleLogPush) and are printed from loop() instead (E04).
//   • All NimBLE-owned std::strings are COPIED to locals before use — the
//     temporary returned by getAddress().toString() is destroyed at the
//     end of the full-expression and the .c_str() pointer would dangle.
//   • Only lightweight RAM pushes to lock-free pending slots, or short
//     critical sections around multi-field state.
// ──────────────────────────────────────────────────────────────────────────────
class OpenSlateAdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *advertisedDevice) override {
        // Check if device matches our filter OR if "Show All" mode is enabled.
        bool isMatched = isDjiDevice(advertisedDevice);
        if (!isMatched && scanResultsIsShowAll()) {
            // In "Show All" mode, accept any device with a valid address.
            std::string addr = advertisedDevice->getAddress().toString();
            isMatched = !addr.empty();
        }
        if (!isMatched) return;

        // Copy the address and name into local std::strings.  NimBLE returns
        // std::string temporaries from getAddress().toString() and
        // getName() — both are destroyed at end-of-statement, so .c_str()
        // on the returned temporary would dangle before scanResultsAdd() uses
        // it.  Keeping the std::strings alive in this scope is safe; they
        // each hold a small fixed buffer that the heap allocator hands back
        // when this function returns.
        std::string macStr  = advertisedDevice->getAddress().toString();
        std::string nameStr = advertisedDevice->haveName()
                              ? advertisedDevice->getName() : "";
        int8_t rssi = advertisedDevice->getRSSI();

        // Lock-free RAM push: scan_results module stores it in a static
        // array with no I/O. NVS write happens later in loop().
        scanResultsAdd(CAMERA_DJI, macStr.c_str(), nameStr.c_str(), rssi, isMatched);

        // Pending registry write: just set a flag + copy into a small
        // struct. camRegistryRemember() in cam_registry.cpp only touches
        // a 4-byte struct + strlcpy() — no NVS inside the callback.
        camRegistryRemember(CAMERA_DJI, macStr.c_str(), nameStr.c_str());

    }
};
static OpenSlateAdvertisedDeviceCallbacks _scanCallbacks;

// ──────────────────────────────────────────────────────────────────────────────
// Device identification — RENAME-PROOF
// ──────────────────────────────────────────────────────────────────────────────
// The DJI Osmo Action family is identified by any of these signals (OR-combined
// so a single missing signal still matches):
//   1. Name prefixes: "Osmo Action", "DJI Action", etc.
//   2. MAC OUI prefixes: 34:d2:62, 60:60:1f, ec:9e:ea
//   3. Advertised service UUID 0xFFF0
//   4. Manufacturer data: contains DJI company id (0xAA 0x08) or Xtra (0xF7 0xAA)
// When settings.scanAll == true, ANY advertiser with a valid address is shown
// so the user can identify their camera manually.
// ──────────────────────────────────────────────────────────────────────────────

static const char *DJI_NAME_PREFIXES[] = {
    "Osmo Action", "DJI Action", "OSMO ACTION", "DJI ACTION",
    "Action 2", "action2", "Action 4", "Action 5", "OsmoAction",
    // NOTE: personal device names (e.g. a developer's renamed camera) were
    // removed here on purpose — matching must stay generic so one user's
    // rename can't cause another user's mis-pair. Use scanAll mode to find
    // renamed cameras by RSSI instead.
};
static const size_t DJI_NAME_PREFIX_COUNT = sizeof(DJI_NAME_PREFIXES) / sizeof(DJI_NAME_PREFIXES[0]);

// MAC OUI (Organisationally Unique Identifier) — first 3 bytes of the MAC.
// Verified from public DJI registration data and sniffing Osmo Action BLE
// advertisements with multiple firmware versions.
static const char *DJI_OUI_PREFIXES[] = {
    "34:d2:62", "60:60:1f", "ec:9e:ea",
};
static const size_t DJI_OUI_COUNT = sizeof(DJI_OUI_PREFIXES) / sizeof(DJI_OUI_PREFIXES[0]);

// Advertised service UUID 0xFFF0 (DJI Camera Control).
static NimBLEUUID DJI_ADV_SERVICE_UUID("0000fff0-0000-1000-8000-00805f9b34fb");

// BLE Company Identifiers (assigned by Bluetooth SIG):
//   0x08AA = DJI  (little-endian on the wire → 0xAA, 0x08 in the adv payload)
//   0xAAF7 = Xtra (the same silicon used in some DJI cameras)
static const uint8_t DJI_COMPANY_ID[2]     = { 0xAA, 0x08 };
static const uint8_t DJI_XTRA_COMPANY[2]   = { 0xAA, 0xF7 };

/// True if `haystack` contains the 2-byte needle anywhere. O(n) but n is tiny.
static bool bytesContain(const std::string &haystack, const uint8_t *needle) {
    if (haystack.size() < 2) return false;
    for (size_t i = 0; i + 1 < haystack.size(); i++) {
        if ((uint8_t)haystack[i]     == needle[0] &&
            (uint8_t)haystack[i + 1] == needle[1]) return true;
    }
    return false;
}

static bool isDjiDevice(NimBLEAdvertisedDevice *device) {
    // "Show all nearby devices" — accept any advertiser with a valid
    // address. The brand pill at pair time defines the backend type, so the
    // user takes responsibility for picking their own camera.
    if (settingsGet().scanAll) {
        // Require at least a parseable address — isDjiDevice is called from
        // a NimBLE callback where the device pointer is always valid, but
        // we still avoid the empty-string corner case.
        std::string addr = device->getAddress().toString();
        return !addr.empty();
    }

    // Signal 1: name prefix match.
    if (device->haveName()) {
        std::string name = device->getName();
        for (size_t i = 0; i < DJI_NAME_PREFIX_COUNT; i++) {
            if (name.find(DJI_NAME_PREFIXES[i]) != std::string::npos) return true;
        }
    }

    // Signal 2: MAC OUI match.
    std::string addr = device->getAddress().toString();
    for (size_t i = 0; i < DJI_OUI_COUNT; i++) {
        if (addr.rfind(DJI_OUI_PREFIXES[i], 0) == 0) return true;
    }

    // Signal 3: advertised service UUID 0xFFF0.
    if (device->isAdvertisingService(DJI_ADV_SERVICE_UUID)) return true;

    // Signal 4: manufacturer data contains DJI / Xtra company id.
    // Guard: getManufacturerData() may return an empty string for
    // devices that omit the field — handle gracefully.
    std::string mfr = device->getManufacturerData();
    if (!mfr.empty()) {
        if (bytesContain(mfr, DJI_COMPANY_ID))   return true;
        if (bytesContain(mfr, DJI_XTRA_COMPANY)) return true;
    }

    return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// Scan management
// ──────────────────────────────────────────────────────────────────────────────

/// No-op completion handler — required so NimBLE uses the *non-blocking*
/// scan API.  The two-argument start(duration, bool) overload BLOCKS the
/// calling task until the scan finishes (forever when duration == 0),
/// which would freeze the entire main loop.
static void scanCompleteCb(NimBLEScanResults results) {
    DBG("DJI: Scan window complete (%d devices)", results.getCount());
    scanResultsMarkComplete();
    scanResultsUpdateSavedStatus();
}

void djiStartScan() {
    if (camIsOtaQuiet()) return;  // OTA owns the radio — refuse new scans
    DBG("DJI: Starting 5s scan (40%% duty cycle)...");

    // Open the collection gate so scanResultsAdd() accepts entries from
    // the NimBLE callback.  Without this the gate stays closed and the
    // callback's results are dropped.
    scanResultsStart();
    camRegistryClearDiscovered();   // New scan: wipe in-RAM discovered list

    NimBLEScan *pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(&_scanCallbacks, false);
    pScan->setActiveScan(true);
    // ── CRITICAL: 40ms window in 100ms interval = 40% radio duty cycle.
    //    Leaves 60% airtime for the Wi-Fi SoftAP to keep beaconing.
    pScan->setInterval(100);
    pScan->setWindow(40);
    pScan->setMaxResults(0);
    // ONE-SHOT 5-second window.  djiStartScan() is called ONLY by the
    // user-initiated Scan button (via camStartUserScan() in camera_manager).
    // This discovery scan is separate from the manager's saved-camera scan.
    //
    // MUST pass scanCompleteCb explicitly: `start(5, false)` binds to the
    // BLOCKING overload (start(duration, is_continue)) which freezes loop()
    // for the full window and never invokes the completion callback —
    // leaving scanResults' _scanning flag stuck true, so every later Scan
    // request is rejected as "already scanning" and no BLE scan ever runs
    // again until reboot.  The 3-arg overload is non-blocking and fires
    // scanCompleteCb at window end, which resets the flag via
    // scanResultsMarkComplete().
    pScan->start(5, scanCompleteCb, false);
}

// ──────────────────────────────────────────────────────────────────────────────
// Connection and GATT Discovery
// ──────────────────────────────────────────────────────────────────────────────
// P02: NimBLEClient::connect() is synchronous — it blocks the calling task in
// ulTaskNotifyTake(...) for up to m_connectTimeout + 1000 ms. NimBLE-Arduino
// 1.4.1 has no async connect variant, so the blocking half runs on the
// ble_worker task. Only the caller-visible state lives here.
//
// Split point matters:
//   • createClient() stays on loop() — NimBLEDevice::createClient() pushes into
//     an unlocked std::list and is not thread-safe.
//   • connect() itself stores a pointer to a stack local of the calling task in
//     m_pTaskData, which is why the worker must be a persistent task and not a
//     one-shot spawn.
//   • Everything that talks GATT (connect, getService, subscribe, write) runs on
//     the worker, because each of those can block the same way.

/// Named error strings are shared between the job and the release path.
static void connectDisposeClient(NimBLEClient *client) {
    // Only ever called from the worker task (or when it is known idle), so the
    // disconnect cannot race the connect. Recycling is non-blocking by design
    // (see ble_recycler) — the client is parked and reused, never deleted.
    if (!client) return;
    if (client->isConnected()) client->disconnect();
    if (!bleRecycleClient(client)) {
        DBG("DJI: WARN — no recycle slot free, leaking client");
    }
}

void DjiCamera::applyConnectedState(NimBLEClient *client) {
    strlcpy(_telemetry.model, "DJI Osmo Action", sizeof(_telemetry.model));
    _lastRxMs = millis();   // Don't trip the staleness watchdog right after connect

    // Start pairing sequence
    _bleState = BLE_AUTHENTICATING;
    _sessionEstablished = false;
    _authStartMs = millis();   // Fresh timeout — _lastReconnectAttempt is stale
                               // when we got here via direct connect (switching)
    (void)client;
    sendPairingArm();
}

/// Worker-task helper: detach the client from the module state, then hand it to
/// the recycler. Detaching first is essential — loop() must never be able to
/// pass a recycled client back into NimBLE.
void DjiCamera::connectAbort(NimBLEClient *client) {
    if (!client) return;
    if (_pClient == client) {
        _pClient = nullptr;
        _pControlChar   = nullptr;
        _pTelemetryChar = nullptr;
        _pAuthChar      = nullptr;
    }
    connectDisposeClient(client);
}

void DjiCamera::noteConnectFailure(BleConnectOutcome outcome) {
    if (outcome == BLE_CONNECT_FAILED) {
        _lastReconnectAttempt = millis();
        _reconnectBackoffMs = cameraRetryBackoff(_reconnectBackoffMs, BLE_RECONNECT_MAX_MS);
        return;
    }
    // Search ended without the camera (or was handed over): the attempt itself
    // was the wait, so search again right away instead of backing off.
    _reconnectBackoffMs = BLE_RECONNECT_INTERVAL_MS;
    _lastReconnectAttempt = millis() - BLE_RECONNECT_INTERVAL_MS + BLE_RECONNECT_FAST_MS;
}

// Refuse the camera's request for a slow (50 ms) interval while GATT discovery
// and pairing run on the fast setup interval; accept it once the session is up.
bool DjiCamera::onConnParamsUpdateRequest(NimBLEClient *client, const ble_gap_upd_params *params) {
    (void)params;
    return client != _pClient || _sessionEstablished;
}

/// Worker-task half: the blocking connect + GATT discovery.
bool DjiCamera::connectJob() {
    DjiConnectJob *job = &_connectJob;

    _tAttempt = millis();
    const bool connected = job->client->connect(job->address);
    _tLink = millis();

    // The attempt may have been superseded while we were blocked: a brand
    // switch, a UI disconnect, or a new target. Then this client is orphaned by
    // design (_pClient was already detached) and must be disposed here —
    // regardless of whether the connect happened to succeed.
    if (job->gen != _connectGen) {
        connectAbort(job->client);
        DBG("DJI: connect attempt superseded — result discarded");
        return false;
    }

    if (!connected) {
        const BleConnectOutcome outcome = bleLinkClassifyFailure(job->client);
        _bleState = BLE_DISCONNECTED;
        if (outcome == BLE_CONNECT_FAILED) {
            bleLogPush(BLE_LOG_DJI_CONNECT_FAILED);
            strlcpy(_lastError, "connect failed", sizeof(_lastError));
        }
        // NOT_FOUND / CANCELLED are the normal "camera still away" cycle:
        // no error for the UI, no backoff (see ble_link.h).
        noteConnectFailure(outcome);
        return false;
    }

    NimBLERemoteService *pService = job->client->getService(DJI_SERVICE_UUID);
    _tSvc = millis();
    if (!pService) {
        bleLogPush(BLE_LOG_DJI_NOT_OSMO);
        connectAbort(job->client);
        _bleState = BLE_DISCONNECTED;
        // User-facing: tells the user this is not a DJI Osmo camera.
        strlcpy(_lastError, "not a DJI Osmo camera", sizeof(_lastError));
        noteConnectFailure();
        return false;
    }

    _pTelemetryChar = pService->getCharacteristic(DJI_CHAR_FFF3);
    _pAuthChar      = pService->getCharacteristic(DJI_CHAR_FFF4);
    _pControlChar   = pService->getCharacteristic(DJI_CHAR_FFF5);

    if (!_pControlChar || !_pAuthChar) {
        bleLogPush(BLE_LOG_DJI_MISSING_CHARS);
        connectAbort(job->client);
        _bleState = BLE_DISCONNECTED;
        strlcpy(_lastError, "DJI service missing chars", sizeof(_lastError));
        noteConnectFailure();
        return false;
    }

    // Subscribe to notifications on FFF4 (pairing/telemetry) and FFF5
    if (_pAuthChar && _pAuthChar->canNotify()) {
        _pAuthChar->subscribe(true, [this](NimBLERemoteCharacteristic *ch,
                uint8_t *data, size_t len, bool notify) {
            if (_pClient && ch == _pAuthChar) notifyCallback(ch, data, len, notify);
        });
        bleLogPush(BLE_LOG_DJI_SUBSCRIBED_FFF4);
    }
    if (_pControlChar && _pControlChar->canNotify()) {
        _pControlChar->subscribe(true, [this](NimBLERemoteCharacteristic *ch,
                uint8_t *data, size_t len, bool notify) {
            if (_pClient && ch == _pControlChar) notifyCallback(ch, data, len, notify);
        });
        bleLogPush(BLE_LOG_DJI_SUBSCRIBED_FFF5);
    }

    _tSub = millis();
    if (job->gen != _connectGen || !job->client->isConnected()) {
        connectAbort(job->client);
        return false;
    }
    applyConnectedState(job->client);
    return true;
}

/// loop()-task half: prepare the client and hand the blocking work over.
/// Returns true when a job was queued (not when the camera is connected).
bool DjiCamera::connectToCamera() {
    if (!_hasTargetAddress) return false;
    DBG("DJI: Connecting to target %s...", _targetAddress.toString().c_str());
    _bleState = BLE_CONNECTING;
    _lastError[0] = '\0';   // clear on new attempt

    // createClient() is not thread-safe ⇒ stays on this task. The recycler
    // hands back a settled client instead of creating a new one, so a stale
    // GAP event can never hit freed memory (see ble_recycler.h).
    if (!_pClient) {
        _pClient = bleRecycleAcquire();
        if (!_pClient) {
            _bleState = BLE_DISCONNECTED;
            strlcpy(_lastError, "no BLE client slot", sizeof(_lastError));
            DBG("DJI: no BLE client slot available");
            noteConnectFailure();
            return false;
        }
        _pClient->setClientCallbacks(this, false);
    }
    bleLinkPrepareClient(_pClient, true);   // low-duty search + fast setup interval

    _connectGen = _connectGen + 1u;
    _connectJob.client  = _pClient;
    _connectJob.address = _targetAddress;
    _connectJob.gen     = _connectGen;

    _jobActive = true;
    if (!bleWorkerSubmit([](void *ctx) {
            DjiCamera *camera = static_cast<DjiCamera *>(ctx);
            bool result = camera->connectJob();
            camera->_jobActive = false;
            return result;
        }, this)) {
        _jobActive = false;
        // Worker busy or unavailable: keep BLE_CONNECTING out of the picture and
        // let the caller (and its retry/backoff) try again on a later tick.
        _bleState = BLE_DISCONNECTED;
        return false;
    }
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// DJI Protocol Implementation (DUML)
// ──────────────────────────────────────────────────────────────────────────────
// Thin adapter over the host-tested codec (duml_codec.cpp). Keeping the
// callsite name means the BLE plumbing below reads unchanged.
static size_t buildDumlPacket(uint8_t *buffer, size_t bufCap, uint8_t sender, uint8_t receiver,
                               uint16_t msgId, uint8_t flags, uint8_t cmdSet, uint8_t cmdId,
                               const uint8_t *payload, size_t payloadLen) {
    return dumlBuildPacket(buffer, bufCap, sender, receiver, msgId, flags,
                           cmdSet, cmdId, payload, payloadLen);
}

void DjiCamera::sendPairingArm() {
    // Step 1: Write [0x01, 0x00] to fff4 to arm pairing mode
    uint8_t armPairing[] = {0x01, 0x00};
    if (_pAuthChar && _pAuthChar->canWrite()) {
        _pAuthChar->writeValue(armPairing, 2, true); // Write with response
        DBG("DJI: Wrote [0x01, 0x00] to FFF4 to arm pairing");
        _pairingArmedTime = millis();
    }
}

void DjiCamera::sendSetPairingPIN() {
    // Semantics (hardware-verified on Action 5 Pro /
    // Osmo Nano — do NOT "fix" these into dynamic values):
    //
    //  • identifier ("001749319286102") — the camera stores its approval
    //    UNDER this string.  A fixed constant is what makes the "already
    //    paired" fast path (PairingStatus payload[1]==0x01) work, so the
    //    ESP32 reconnects silently instead of re-approval every boot.
    //    Retries must always carry the SAME identifier.
    //  • token ("osmo") — purely cosmetic on cameras: displayed verbatim
    //    on screen (as "OSMO"); the on-screen approve tap is the real
    //    gate.  No numeric PIN needs to be read from the camera.
    uint8_t packet[64];

    // Payload: PackString("001749319286102") + PackString("osmo")
    uint8_t payload[32];
    size_t pIdx = 0;

    const char* id = "001749319286102";
    payload[pIdx++] = strlen(id);
    memcpy(&payload[pIdx], id, strlen(id));
    pIdx += strlen(id);

    const char* pin = "osmo";
    payload[pIdx++] = strlen(pin);
    memcpy(&payload[pIdx], pin, strlen(pin));
    pIdx += strlen(pin);

    // Target: App (0x02) -> WiFi (0x07). Type: flags=0x40, set=0x07, id=0x45
    size_t len = buildDumlPacket(packet, sizeof(packet), 0x02, 0x07, _sequenceCounter++,
                                 0x40, 0x07, 0x45, payload, pIdx);
    if (!len) return;  // V01 guard: never send truncated packet

    // MUST use Write-Without-Response (false) on FFF5
    if (_pControlChar && _pControlChar->canWriteNoResponse()) {
        _pControlChar->writeValue(packet, len, false);
        DBG("DJI: Sent SetPairingPIN DUML packet to FFF5 (%d bytes)", len);
    }
}

void DjiCamera::sendApprovalAck() {
    // ACK for "pairing approved": flags=0xC0, set=0x07, id=0x46, payload=0x00.
    // Called from update() only (E04) — never from the NimBLE host task.
    uint8_t packet[16];
    uint8_t payload[] = {0x00};
    size_t len = buildDumlPacket(packet, sizeof(packet), 0x02, 0x07, _sequenceCounter++,
                                 0xC0, 0x07, 0x46, payload, 1);
    if (!len) return;
    if (_pControlChar && _pControlChar->canWriteNoResponse() &&
        _pControlChar->writeValue(packet, len, false)) {
        DBG("DJI: Sent pairing-approved ACK");
    } else {
        DBG("DJI: WARN — pairing-approved ACK write failed");
    }
}

void DjiCamera::sendKeepAlive() {
    // Send a generic ping to keep the link alive
    uint8_t packet[32];
    uint8_t payload[] = {0x00};
    size_t len = buildDumlPacket(packet, sizeof(packet), 0x02, 0x01, _sequenceCounter++,
                                 0x40, 0x00, 0xF1, payload, 1);
    if (!len) return;
    if (_pControlChar && _pControlChar->canWriteNoResponse()) {
        _pControlChar->writeValue(packet, len, false);
    }
}

void DjiCamera::sendWake() {
    // Wake: 0x53/0x10 to receiver 0x1C, payload 00 00 00 00.
    // The camera answers 01 00 00 00 and starts its status pushes.
    uint8_t packet[24];
    uint8_t payload[] = {0x00, 0x00, 0x00, 0x00};
    size_t len = buildDumlPacket(packet, sizeof(packet), 0x02, 0x1C, _sequenceCounter++,
                                 0x40, 0x53, 0x10, payload, sizeof(payload));
    if (!len) return;
    if (_pControlChar && _pControlChar->canWriteNoResponse() &&
        _pControlChar->writeValue(packet, len, false)) {
        DBG("DJI: Sent session wake (0x53/0x10 -> 0x1C)");
    }
}

void DjiCamera::sendSessionPing() {
    // Session keepalive: 0x00/0x2B to receiver 0xF0, payload 01 01, ~1 Hz.
    uint8_t packet[20];
    uint8_t payload[] = {0x01, 0x01};
    size_t len = buildDumlPacket(packet, sizeof(packet), 0x02, 0xF0, _sequenceCounter++,
                                 0x40, 0x00, 0x2B, payload, sizeof(payload));
    if (!len) return;
    if (_pControlChar && _pControlChar->canWriteNoResponse()) {
        _pControlChar->writeValue(packet, len, false);
    }
}

// ── Battery telemetry (0x0D/0x02) ───────────────────────────────────────────
// Battery push layout (offsets into payload):
//   @1 u16 LE pack voltage mV   @5 i32 LE current mA (+ = charging)
//   @17 i16 LE temperature 0.1 °C   @20 u8 charge %
// Action 4+ push this frame unsolicited (flags 0x00). The Action 2 never
// pushes it but answers a request to the battery module (receiver 0x05) with
// the same layout preceded by a result byte (00 = OK) — hardware-verified:
//   C0 0D 02 | 00 | 00 14 11 .. 44 02 .. 9F 01 02 64  -> 4372 mV, 41.5 °C, 100 %
void DjiCamera::sendBatteryQuery() {
    uint8_t packet[16];
    size_t len = buildDumlPacket(packet, sizeof(packet), 0x02, 0x05, _sequenceCounter++,
                                 0x40, 0x0D, 0x02, nullptr, 0);
    if (!len) return;
    if (_pControlChar && _pControlChar->canWriteNoResponse()) {
        _pControlChar->writeValue(packet, len, false);
    }
}

void DjiCamera::parseBattery(const uint8_t *p, size_t n) {
    // p = battery record in push layout (result byte already stripped).
    if (n < 21) return;
    const uint8_t pct = p[20];
    if (pct > 100) return;
    _telemetry.batteryPercent   = pct;
    _telemetry.batteryMv        = (uint16_t)(p[1] | (p[2] << 8));
    _telemetry.batteryCurrentMa = (int16_t)(p[5] | (p[6] << 8));   // low 16 bits of i32
    _telemetry.batteryTempC10   = (int16_t)(p[17] | (p[18] << 8));
    _telemetry.dataValid = true;
}

void DjiCamera::logFrameIfNew(const uint8_t *pData, size_t length, uint8_t nBytes) {
    // Key = sender + flags + set + id, so replies from different modules to
    // the same command stay distinguishable in the log.
    const uint32_t key = ((uint32_t)pData[4] << 24) | ((uint32_t)pData[8] << 16) |
                         ((uint32_t)pData[9] << 8) | pData[10];
    // Hash the payload only (bytes 11 .. length-3): header seq and CRC change
    // on every frame and would defeat the "changed" check.
    uint32_t h = 2166136261u;
    for (size_t i = 11; i + 2 < length; i++) h = (h ^ pData[i]) * 16777619u;
    const uint32_t now = millis();
    for (uint8_t i = 0; i < _seenCount; i++) {
        SeenFrame &s = _seen[i];
        if (s.key != key) continue;
        if (s.hash == h || now - s.ms < 2000) return;
        s.hash = h; s.ms = now;
        bleLogPush(BLE_LOG_DJI_TELE, pData, nBytes, pData[8], pData[9], pData[10], 0, (int16_t)length);
        return;
    }
    if (_seenCount < sizeof(_seen) / sizeof(_seen[0])) {
        _seen[_seenCount++] = {key, h, now};
    }
    bleLogPush(BLE_LOG_DJI_TELE, pData, nBytes, pData[8], pData[9], pData[10], 0, (int16_t)length);
}

// ──────────────────────────────────────────────────────────────────────────────
// Notification Callback
// ──────────────────────────────────────────────────────────────────────────────
void DjiCamera::notifyCallback(NimBLERemoteCharacteristic *pChar, uint8_t *pData,
                           size_t length, bool isNotify) {
    // E04: this runs on the NimBLE host task. Nothing below may format or
    // print — every log line is handed to the deferred ring instead.
    const uint8_t charIdx = (pChar == _pAuthChar) ? 0 : 1;
    const uint8_t nBytes  = (uint8_t)(length > 80 ? 80 : length);
    _lastRxMs = millis();   // Any inbound traffic proves the link is alive

    if (length < 13 || pData[0] != 0x55) {
        bleLogPush(BLE_LOG_DJI_RAW, pData, nBytes, charIdx, 0, 0, 0, (int16_t)length);
        return;
    }

    uint8_t flags = pData[8];
    uint8_t cmdSet = pData[9];
    uint8_t cmdId = pData[10];

    logFrameIfNew(pData, length, nBytes);

    // Check for Pairing Status Response (flags=0xC0, set=0x07, id=0x45)
    // Payload: [00][status] — status 0x01 = already paired, 0x02 = approval
    // required.  Status lives at pData[12] (payload[1]); guard needs >= 13.
    if (flags == 0xC0 && cmdSet == 0x07 && cmdId == 0x45) {
        if (length >= 13) {
            bleLogPush(BLE_LOG_DJI_PAIR_PAYLOAD, nullptr, 0, pData[11], pData[12]);
            // payload[1] == 0x01 means Already Paired
            if (pData[12] == 0x01) {
                _sessionEstablished = true;
                _wakePending = true;
                _tSession = millis();
                _timingPending = true;
                _bleState = BLE_CONNECTED;
                _authBackoffMs = BLE_RECONNECT_INTERVAL_MS;
                _reconnectBackoffMs = BLE_RECONNECT_INTERVAL_MS;
                bleLogPush(BLE_LOG_DJI_ALREADY_PAIRED);
            }
            // payload[1] == 0x02 means Approval Required
            else if (pData[12] == 0x02) {
                bleLogPush(BLE_LOG_DJI_APPROVAL_REQ);
            }
        }
    }

    // Check for Pairing Approved (flags=0x40, set=0x07, id=0x46)
    if (flags == 0x40 && cmdSet == 0x07 && cmdId == 0x46) {
        _sessionEstablished = true;
        _wakePending = true;
        _tSession = millis();
        _timingPending = true;
        _bleState = BLE_CONNECTED;
        _authBackoffMs = BLE_RECONNECT_INTERVAL_MS;
        _reconnectBackoffMs = BLE_RECONNECT_INTERVAL_MS;
        bleLogPush(BLE_LOG_DJI_APPROVED);

        // E04: no GATT write on the host task — update() sends the ACK on
        // the next loop() tick (also keeps _sequenceCounter single-threaded).
        _approvalAckPending = true;
    }

    // Record-control response (flags=0xC0, set=0x02, id=0x02): first
    // payload byte is the camera's reply code:
    //   00 = OK, d8 = resource busy, d9 = wrong state, df = wrong param,
    //   e3 = bad param, e0 = not supported, silence = receiver missing.
    if (flags == 0xC0 && cmdSet == 0x02 && cmdId == 0x02 && length >= 12) {
        uint8_t reply = pData[11];
        bleLogPush(BLE_LOG_DJI_REPLY, nullptr, 0, reply);
        if (reply == 0x00) {
            // Confirm local state from our last command direction.
            _telemetry.state = _lastCmdWasStart ? CAM_STATE_RECORDING : CAM_STATE_STANDBY;
            _telemetry.dataValid = true;
            if (_lastCmdWasStart && !_recActive) {
                _recActive = true;
                _recStartMs = millis();
            } else if (!_lastCmdWasStart) {
                _recActive = false;
            }
        }
    }

    // Unsolicited Telemetry (flags=0x00)
    if (flags == 0x00) {
        // V04 — don't mark data valid for unknown telemetry sets (zero parse yet).
        if (cmdSet == 0x02) {
            _telemetry.dataValid = true;
        }
    }

    // Battery: unsolicited push (Action 4+, flags 0x00) or the Action 2's
    // reply to sendBatteryQuery() (flags 0xC0, leading result byte).
    if (cmdSet == 0x0D && cmdId == 0x02 && length >= 13 + 21) {
        const uint8_t *payload = pData + 11;
        const size_t   plen    = length - 13;
        if (flags == 0x00) {
            _battPushSeen = true;
            parseBattery(payload, plen);
        } else if (flags == 0xC0 && payload[0] == 0x00 && plen >= 22) {
            parseBattery(payload + 1, plen - 1);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────
void DjiCamera::init() {
    configASSERT(!bleWorkerBusy());
    configASSERT(_pClient == nullptr);
    _pControlChar = _pTelemetryChar = _pAuthChar = nullptr;
    DBG("DJI: Backend ready (NimBLE stack shared)");
    _bleState = BLE_DISCONNECTED;
    // Boot priority: make the first direct connect to the saved camera due
    // right away instead of after one reconnect interval of uptime.
    _authBackoffMs        = BLE_RECONNECT_INTERVAL_MS;
    _reconnectBackoffMs   = BLE_RECONNECT_INTERVAL_MS;
    _lastReconnectAttempt = millis() - _authBackoffMs;
    _telemetry = CameraTelemetry();
    _recActive = false;
    _recStartMs = 0;
    _hasTargetAddress = false;
    _doConnect = false;
    _sessionEstablished = false;
    _pairingArmedTime = 0;
    _lastKeepAlive = 0;
    _approvalAckPending = false;
    _wakePending = false;
    _wakeArmed = false;
    _lastError[0] = '\0';
}

void DjiCamera::release() {
    // H06: hand the NimBLEClient back to the recycler. It is parked and reused
    // for the next connection — never deleted, because NimBLE may still have a
    // GAP event queued for it (see ble_recycler.h).
    if (!_pClient) return;

    NimBLEClient *doomed = _pClient;
    _pClient = nullptr;         // detach first: no other path may touch it now
    _pControlChar   = nullptr;
    _pTelemetryChar = nullptr;
    _pAuthChar      = nullptr;

    _bleState            = BLE_DISCONNECTED;
    _sessionEstablished  = false;
    _hasTargetAddress    = false;   // drop the target so no reconnect fires
    _doConnect           = false;
    _telemetry.dataValid = false;
    _telemetry.state     = CAM_STATE_UNKNOWN;
    _recActive           = false;
    _recStartMs          = 0;
    _approvalAckPending  = false;

    // P02: invalidate an in-flight connect. The worker compares this after its
    // blocking call returns and disposes the client itself in that case — we
    // must not disconnect or recycle a client NimBLE is still working on.
    _connectGen = _connectGen + 1u;
    if (_jobActive) {
        return;
    }

    if (doomed->isConnected()) doomed->disconnect();

    // Deferred: the client is parked and reused once the GAP teardown settled.
    // bleRecycleProcess() (every camUpdate tick) promotes it.
    if (!bleRecycleClient(doomed)) {
        // Recycler exhausted: do not block loop() waiting for GAP teardown.
        // Keep the failure visible rather than silently losing the client.
        DBG("DJI: WARN — no recycle slot free, leaking client");
    }
}

void DjiCamera::update(bool allowConnect) {
    uint32_t now = millis();
    if (_jobActive) return;

    switch (_bleState) {
        case BLE_CONNECTING:
            // The connect job owns the transition out of this state. Only a
            // watchdog lives here: if the worker never comes back (stack
            // overflow, NimBLE wedge) do not stay stuck forever.
            if (!bleWorkerBusy() && _bleState == BLE_CONNECTING) {
                _bleState = BLE_DISCONNECTED;
            }
            break;

        case BLE_DISCONNECTED:
            // OTA quiet: full radio to WiFi — defer direct-connect until clear.
            // _doConnect stays set and runs on the first tick after resume.
            if (camIsOtaQuiet() || !allowConnect) break;
            // Never start a second attempt while the worker is still busy.
            if (bleWorkerBusy()) break;
            // A pending direct-connect (Web UI "Use" / camKick) must be
            // honoured immediately — don't wait out the reconnect interval,
            // and never let startScan() clobber the request.
            if (_doConnect) {
                _lastReconnectAttempt = now;
                _doConnect = false;
                connectToCamera();
                break;
            }
            // Keep this slot's target across range loss; never use the
            // registry's primary MAC for another slot.
            {
                uint32_t interval = _authBackoffMs > _reconnectBackoffMs ?
                    _authBackoffMs : _reconnectBackoffMs;
                if (_hasTargetAddress &&
                    (now - _lastReconnectAttempt) >= interval) {
                    _lastReconnectAttempt = now;
                    connectToCamera();
                }
            }
            break;

        case BLE_SCANNING:
            // Discovery is owned by the manager, not by a connection slot.
            break;

        case BLE_AUTHENTICATING:
            if (_jobActive || camIsOtaQuiet()) break;
            // Wait 200ms after arming, then send the PIN packet
            if (now - _pairingArmedTime >= 200 && _pairingArmedTime > 0) {
                _pairingArmedTime = 0; // Only send once
                sendSetPairingPIN();
            }
            if (now - _authStartMs >= 30000) {
                DBG("DJI: Auth timeout (30s) — resetting");
                if (_pClient) _pClient->disconnect();
                _bleState = BLE_DISCONNECTED;
                _lastReconnectAttempt = now;
                // V08: exponential backoff so a camera stuck at "approve
                // pairing" doesn't get hammered every reconnect interval.
                _authBackoffMs = (_authBackoffMs * 2 > 30000) ? 30000 : _authBackoffMs * 2;
                DBG("DJI: auth backoff now %u ms", _authBackoffMs);
            }
            break;

        case BLE_CONNECTED:
            if (!_pClient || !_pClient->isConnected()) {
                _approvalAckPending = false;
                _bleState = BLE_DISCONNECTED;
                break;
            }
            if (_timingPending.exchange(false)) {
                NimBLEConnInfo ci = _pClient->getConnInfo();
                DBG("DJI: TIMING lost->session %ld ms | attempt->link %lu, svc %lu, subs %lu, pair %lu ms | itvl %.2f ms lat %u to %u ms",
                    _tLost ? (long)(_tSession - _tLost) : -1L,
                    (unsigned long)(_tLink - _tAttempt), (unsigned long)(_tSvc - _tLink),
                    (unsigned long)(_tSub - _tSvc), (unsigned long)(_tSession - _tSub),
                    ci.getConnInterval() * 1.25f, ci.getConnLatency(), ci.getConnTimeout() * 10u);
                _tLost = 0;
                bleLinkRelax(_pClient);   // setup done: back to a relaxed interval
            }
            // E04: deferred ACK for "pairing approved" (set in notifyCallback).
            if (_approvalAckPending.exchange(false)) {
                sendApprovalAck();
            }
            // Session wake, battery poll and 1 Hz session ping. At most one
            // FFF5 write per tick from this block, and every write resets
            // _lastPingMs, so writes stay spaced (>= 100 ms apart).
            if (_sessionEstablished && !camIsOtaQuiet()) {
                if (_wakePending.exchange(false)) {
                    _wakeArmed = true;
                    _wakeDueMs = now + 300;
                    _lastPingMs = now;
                    _battPushSeen = false;
                    // First battery query ~1 s after the wake.
                    _lastBattPollMs = now - DJI_BATTERY_POLL_MS + 1000;
                }
                if (_wakeArmed && (int32_t)(now - _wakeDueMs) >= 0) {
                    _wakeArmed = false;
                    sendWake();
                    _lastPingMs = now;
                } else if (!_battPushSeen && now - _lastBattPollMs >= DJI_BATTERY_POLL_MS &&
                           now - _lastPingMs >= 300) {
                    _lastBattPollMs = now;
                    _lastPingMs = now;
                    sendBatteryQuery();
                } else if (now - _lastPingMs >= 1000) {
                    _lastPingMs = now;
                    sendSessionPing();
                }
            }
            // Liveness watchdog: the camera pushes DUML notifications
            // constantly (~1 Hz GeneralStatus and up).  If nothing arrives
            // for a long while the link is wedged even if the BLE stack
            // hasn't noticed yet — force a reconnect.
            // Paused during OTA quiet (keep-alives are intentionally held).
            if (!camIsOtaQuiet() && now - _lastRxMs >= DJI_LINK_STALE_MS) {
                DBG("DJI: No camera traffic for %d s — forcing reconnect",
                    DJI_LINK_STALE_MS / 1000);
                _pClient->disconnect();
                _bleState = BLE_DISCONNECTED;
                break;
            }
            if (now - _lastKeepAlive >= BLE_KEEPALIVE_INTERVAL_MS) {
                _lastKeepAlive = now;
                // OTA quiet: skip keep-alive so the upload TCP stream is not
                // preempted. Stale-watchdog is also paused below; the link
                // resumes (or cleanly reconnects) after reboot/resume.
                if (!camIsOtaQuiet()) sendKeepAlive();
            }
            // Local rec-time clock while our START is active (see header note:
            // camera gives no decoded rec-time field, so count from accepted
            // command like the GoPro backend does).
            if (_recActive) {
                _telemetry.recTimeSeconds = (now - _recStartMs) / 1000;
                _telemetry.state = CAM_STATE_RECORDING;
                _telemetry.dataValid = true;
            }
            break;
    }
}

bool DjiCamera::sendStartRecord() {
    if (_jobActive || _bleState != BLE_CONNECTED || !_sessionEstablished) return false;

    uint8_t packet[32];
    uint8_t payload[] = {0x01}; // 1 = Start

    // Osmo Action family record control: CmdSet 0x02 (camera control),
    // CmdId 0x02 (record), payload 0x01 = start. Verified against DUML captures.
    // V02: old comment said 0x0A/0x0D (drift, no backing) — corrected.
    size_t len = buildDumlPacket(packet, sizeof(packet), 0x02, 0x01, _sequenceCounter++,
                                 0x40, 0x02, 0x02, payload, sizeof(payload));
    if (!len) return false;

    // Only report success (and flip local state) when the GATT write
    // actually went out — a failed write must keep the manager's per-camera
    // pending command so the start is retried once the link is ready.
    if (_pControlChar && _pControlChar->canWriteNoResponse() &&
        _pControlChar->writeValue(packet, len, false)) {
        DBG("DJI: Sent Start Record (0x02/0x02) to FFF5");
        // Optimistic local state (confirmed by ACK in notifyCallback):
        // without this the OSD/dashboard stay UNKNOWN until the ACK arrives.
        _lastCmdWasStart = true;
        _recActive = true;
        _recStartMs = millis();
        _telemetry.state = CAM_STATE_RECORDING;
        _telemetry.dataValid = true;
        return true;
    }
    return false;
}

bool DjiCamera::sendStopRecord() {
    if (_jobActive || _bleState != BLE_CONNECTED || !_sessionEstablished) return false;

    uint8_t packet[32];
    uint8_t payload[] = {0x00}; // 0 = Stop

    // Osmo Action family record control: CmdSet 0x02, CmdId 0x02,
    // payload 0x00 = stop.
    size_t len = buildDumlPacket(packet, sizeof(packet), 0x02, 0x01, _sequenceCounter++,
                                 0x40, 0x02, 0x02, payload, sizeof(payload));
    if (!len) return false;

    if (_pControlChar && _pControlChar->canWriteNoResponse() &&
        _pControlChar->writeValue(packet, len, false)) {
        DBG("DJI: Sent Stop Record (0x02/0x02) to FFF5");
        _lastCmdWasStart = false;
        _recActive = false;
        _telemetry.state = CAM_STATE_STANDBY;
        _telemetry.dataValid = true;
        return true;
    }
    return false;
}

BleConnectionState DjiCamera::getState() const { return _bleState; }
const CameraTelemetry& DjiCamera::getTelemetry() const { return _telemetry; }
bool DjiCamera::isReady() const { return !_jobActive && (_bleState == BLE_CONNECTED) && _sessionEstablished; }
const char* DjiCamera::getLastError() const { return _lastError; }

void DjiCamera::targetMac(const char *mac, uint8_t addressType) {
    if (!mac || !*mac) return;
    NimBLEScan *pScan = NimBLEDevice::getScan();
    if (pScan && pScan->isScanning()) pScan->stop();
    if (_pClient && _pClient->isConnected()) _pClient->disconnect();
    _targetAddress    = NimBLEAddress(std::string(mac), addressType);
    _hasTargetAddress = true;
    _doConnect        = true;
    DBG("DJI: targeting %s (direct connect)", mac);
}
