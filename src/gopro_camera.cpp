// ============================================================================
// gopro_camera.cpp — Open GoPro BLE client (HERO8 Black 2019 and newer)
// ============================================================================
// Wire protocol: the official Open GoPro BLE API.
//
// ─── GATT layout (base UUID b5f9XXXX-aa8d-11e3-9046-0002a5d5c51b) ───────────
//   Service FEA6 "Control & Query"
//     GP-0072 Command            (write)
//     GP-0073 Command Response   (notify)
//     GP-0074 Settings           (write)   ← keep-alive
//     GP-0075 Settings Response  (notify)
//     GP-0076 Query              (write)   ← status registration / polling
//     GP-0077 Query Response     (notify)  ← battery %, encoding state
//
// ─── Frames ─────────────────────────────────────────────────────────────────
//   Command request : [0x03][payloadLen][payload…]
//   Setting request : [0x02][payloadLen][payload…]
//   Query   request : [0x01][payloadLen][payload…]
//   Shutter ON      : 03 1A {%230%22shutter%22%3Atrue}
//   Shutter OFF     : 03 1B {%230%22shutter%22%3Afalse}
//   Keep-alive      : 02 01 42                       (every ~3 s)
//   Register status : 01 04 53 0A 0D 46
//                     (0x53=register, ids: 10 Encoding, 13 Rec Duration,
//                      70 Internal Battery Percentage)
//
// ─── Pairing ────────────────────────────────────────────────────────────────
//   First connection requires tapping "Pair" on the camera screen.  We bond,
//   so subsequent connections are automatic.  Camera must be in pairing /
//   connectable mode (Connections → Wireless Connections → GoPro Quik).
// ============================================================================

#include "gopro_camera.h"
#include "cam_registry.h"
#include "scan_results.h"
#include "settings.h"
#include "camera_manager.h"
#include "ble_recycler.h"
#include "ble_log.h"
#include "ble_worker.h"
#include "camera_slot_policy.h"
#include "ble_link.h"
#include <NimBLEDevice.h>
#include <atomic>

// ──────────────────────────────────────────────────────────────────────────────
// GATT UUIDs
// ──────────────────────────────────────────────────────────────────────────────

static NimBLEUUID GP_SERVICE_UUID("0000fea6-0000-1000-8000-00805f9b34fb");
static NimBLEUUID GP_CHAR_COMMAND("b5f90072-aa8d-11e3-9046-0002a5d5c51b");
static NimBLEUUID GP_CHAR_CMD_RESPONSE("b5f90073-aa8d-11e3-9046-0002a5d5c51b");
static NimBLEUUID GP_CHAR_SETTINGS("b5f90074-aa8d-11e3-9046-0002a5d5c51b");
static NimBLEUUID GP_CHAR_QUERY("b5f90076-aa8d-11e3-9046-0002a5d5c51b");
static NimBLEUUID GP_CHAR_QUERY_RESPONSE("b5f90077-aa8d-11e3-9046-0002a5d5c51b");

// Registered status IDs (all uint8 values)
#define GP_STATUS_BATTERY_BARS   0x02   // enum 0..4 (fallback if % unsupported)
#define GP_STATUS_ENCODING       0x0A   // 1 = recording
#define GP_STATUS_BATTERY_PCT    0x46   // 0..100 %

// ──────────────────────────────────────────────────────────────────────────────
// Internal state
// ──────────────────────────────────────────────────────────────────────────────

class GpCamera : public CameraBackend, private NimBLEClientCallbacks {
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
    void onAuthenticationComplete(ble_gap_conn_desc *desc) override;
    bool connectToCamera();
    bool connectJob();
    bool discoverJob();
    void connectAbort(NimBLEClient *client);
    void notifyCallback(NimBLERemoteCharacteristic *pChar, uint8_t *pData,
                        size_t length, bool isNotify);
    void parseQueryResponse(const uint8_t *data, size_t len);
    void applyStatus(uint8_t id, uint32_t value);
    bool writeCommand(const uint8_t *data, size_t len);
    bool sendShutter(bool on);
    void sendKeepAlive();
    void sendRegisterStatusUpdates();
    void sendStatusQuery();
    void noteConnectFailure(BleConnectOutcome outcome = BLE_CONNECT_FAILED);

    std::atomic<BleConnectionState> _bleState{BLE_DISCONNECTED};
    CameraTelemetry _telemetry;
    NimBLEClient *_pClient = nullptr;
    NimBLERemoteCharacteristic *_pCommandChar = nullptr;
    NimBLERemoteCharacteristic *_pSettingsChar = nullptr;
    NimBLERemoteCharacteristic *_pQueryChar = nullptr;
    NimBLEAddress _targetAddress;
    bool _hasTargetAddress = false;
    bool _doConnect = false;
    std::atomic<uint32_t> _connectGen{0};
    std::atomic<bool> _jobActive{false};
    std::atomic<bool> _securityReady{false};
    std::atomic<bool> _securityFailed{false};
    struct GpConnectJob {
        NimBLEClient *client;
        NimBLEAddress address;
        uint32_t gen;
    } _connectJob;
    uint32_t _reconnectBackoffMs = BLE_RECONNECT_INTERVAL_MS;
    uint32_t _lastReconnectAttempt = 0;
    uint32_t _lastKeepAlive = 0;
    uint32_t _lastStatusPoll = 0;
    uint32_t _connectedAtMs = 0;
    bool _registeredStatuses = false;
    uint32_t _recStartMs = 0;
    char _lastError[48] = "";
};

CameraBackend& gpCamera(uint8_t slot) {
    static GpCamera cameras[CAMERA_CONNECTION_SLOTS];
    configASSERT(slot < CAMERA_CONNECTION_SLOTS);
    return cameras[slot];
}

// ──────────────────────────────────────────────────────────────────────────────
// Forward declarations
// Note: gpStartScan() is declared in gopro_camera.h (non-static) so it can
// be called from camera_manager.cpp via camStartUserScan().
// ──────────────────────────────────────────────────────────────────────────────

void              gpStartScan();
static bool isGoProDevice(NimBLEAdvertisedDevice *device);

// ──────────────────────────────────────────────────────────────────────────────
// BLE Callbacks
// ──────────────────────────────────────────────────────────────────────────────
// onConnect/onDisconnect run on whichever task drove the GAP event — with the
// P02 worker that is the ble_worker task for the connect half, and the NimBLE
// host task for the disconnect half. Neither may log directly, so both hand a
// record to the deferred ring (E04).
void GpCamera::onConnect(NimBLEClient *pClient) {
    if (pClient != _pClient) return;
    bleLogPush(BLE_LOG_GP_CONNECTED);
}
void GpCamera::onDisconnect(NimBLEClient *pClient) {
    if (pClient != _pClient) return;
    bleLogPush(BLE_LOG_GP_DISCONNECTED);
    _pCommandChar = nullptr;
    _pSettingsChar = nullptr;
    _pQueryChar = nullptr;
    _registeredStatuses = false;
    _telemetry.dataValid = false;
    _telemetry.state = CAM_STATE_UNKNOWN;
    _recStartMs = 0;
    _securityReady = false;
    _lastReconnectAttempt = millis();
    _bleState = BLE_DISCONNECTED;
}

void GpCamera::onAuthenticationComplete(ble_gap_conn_desc *desc) {
    if (!_pClient || desc->conn_handle != _pClient->getConnId()) return;
    _securityReady = desc->sec_state.encrypted;
    _securityFailed = !desc->sec_state.encrypted;
}

// ──────────────────────────────────────────────────────────────────────────────
// RTOS-SAFE scan callback. Runs on the NimBLE host task — any blocking call
// (Preferences, Serial.printf, snprintf to a long string) will starve the
// BLE stack and trip the RTOS watchdog.
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
class GpScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *advertisedDevice) override {
        // Check if device matches our filter OR if "Show All" mode is enabled.
        bool isMatched = isGoProDevice(advertisedDevice);
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
        scanResultsAdd(CAMERA_GOPRO, macStr.c_str(), nameStr.c_str(), rssi, isMatched);

        // Pending registry write: just set a flag + copy into a small
        // struct. No NVS, no allocation, no std::string persistence.
        camRegistryRemember(CAMERA_GOPRO, macStr.c_str(), nameStr.c_str());

    }
};
static GpScanCallbacks _scanCallbacks;

// ──────────────────────────────────────────────────────────────────────────────
// Device identification — GoPros advertise service 0xFEA6 and/or a name like
// "GoPro 1234" / legacy "GP12345678".  When settings.scanAll == true, every
// advertiser with a valid address is shown so the user can identify their
// camera manually.
// ──────────────────────────────────────────────────────────────────────────────

static NimBLEUUID GOPRO_ADV_SERVICE((uint16_t)0xFEA6);

static bool isGoProDevice(NimBLEAdvertisedDevice *device) {
    // "Show all nearby devices" — accept any advertiser with a valid address.
    if (settingsGet().scanAll) {
        std::string addr = device->getAddress().toString();
        return !addr.empty();
    }

    // Signal 1: advertised service 0xFEA6 (Open GoPro Control & Query).
    if (device->isAdvertisingService(GOPRO_ADV_SERVICE)) return true;

    // Signal 2: name prefix match.
    if (device->haveName()) {
        std::string name = device->getName();
        if (name.rfind("GoPro", 0) == 0 || name.rfind("GOPRO", 0) == 0 ||
            name.rfind("GP", 0) == 0) {
            return true;
        }
    }
    return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// Scan management
// ──────────────────────────────────────────────────────────────────────────────

/// No-op completion handler — required so NimBLE uses the *non-blocking*
/// scan API (the two-argument overload blocks the main loop forever when
/// duration == 0).
static void scanCompleteCb(NimBLEScanResults results) {
    DBG("GP: Scan window complete (%d devices)", results.getCount());
    scanResultsMarkComplete();
    scanResultsUpdateSavedStatus();
}

void gpStartScan() {
    if (camIsOtaQuiet()) return;  // OTA owns the radio — refuse new scans
    DBG("GP: Starting 5s scan (40%% duty cycle)...");

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
    // ONE-SHOT 5-second window.  gpStartScan() is called ONLY by the
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
// Connection & GATT discovery
// ──────────────────────────────────────────────────────────────────────────────
// P02: NimBLEClient::connect() is synchronous — it blocks the calling task in
// ulTaskNotifyTake(...) for up to m_connectTimeout + 1000 ms. Connect and GATT
// discovery run on the worker; pairing approval is awaited asynchronously.
//
// Split point: createClient() stays on loop() (its std::list push is unlocked
// and not thread-safe); connect() itself stores a pointer to a stack local of
// the calling task in m_pTaskData, which is why the worker is a persistent task
// rather than a one-shot spawn.

static void connectDisposeClient(NimBLEClient *client) {
    // Only ever called from the worker task (or when it is known idle), so the
    // disconnect cannot race the connect. Recycling is non-blocking by design
    // (see ble_recycler) — the client is parked and reused, never deleted.
    if (!client) return;
    if (client->isConnected()) client->disconnect();
    if (!bleRecycleClient(client)) {
        DBG("GP: WARN — no recycle slot free, leaking client");
    }
}

// Keep a flight camera's identity across range loss, but throttle failures.
void GpCamera::noteConnectFailure(BleConnectOutcome outcome) {
    if (outcome != BLE_CONNECT_FAILED) {
        // Search ended without the camera (or was handed over): retry right away.
        _reconnectBackoffMs = BLE_RECONNECT_INTERVAL_MS;
        _lastReconnectAttempt = millis() - BLE_RECONNECT_INTERVAL_MS + BLE_RECONNECT_FAST_MS;
        return;
    }
    _lastReconnectAttempt = millis();
    _reconnectBackoffMs = cameraRetryBackoff(_reconnectBackoffMs, BLE_RECONNECT_MAX_MS);
}

/// Worker-task helper: detach the client from the module state, then hand it to
/// the recycler. Detaching first is essential — loop() must never be able to
/// pass a recycled client back into NimBLE.
void GpCamera::connectAbort(NimBLEClient *client) {
    if (!client) return;
    if (_pClient == client) {
        _pClient = nullptr;
        _pCommandChar  = nullptr;
        _pSettingsChar = nullptr;
        _pQueryChar    = nullptr;
    }
    connectDisposeClient(client);
}

/// Worker-task half: connect and initiate asynchronous pairing.
bool GpCamera::connectJob() {
    GpConnectJob *job = &_connectJob;

    const bool connected = job->client->connect(job->address);

    // The attempt may have been superseded while we were blocked: a brand
    // switch, a UI disconnect, or a new target. Then this client is orphaned by
    // design (_pClient was already detached) and must be disposed here —
    // regardless of whether the connect happened to succeed.
    if (job->gen != _connectGen) {
        connectAbort(job->client);
        DBG("GP: connect attempt superseded — result discarded");
        return false;
    }

    if (!connected) {
        const BleConnectOutcome outcome = bleLinkClassifyFailure(job->client);
        _bleState = BLE_DISCONNECTED;
        if (outcome == BLE_CONNECT_FAILED) {
            bleLogPush(BLE_LOG_GP_CONNECT_FAILED);
            strlcpy(_lastError, "connect failed", sizeof(_lastError));
        }
        noteConnectFailure(outcome);
        return false;
    }

    // Start pairing without parking the only connect worker until the user
    // approves. Other slots can connect while this camera waits for approval.
    _connectedAtMs = millis();
    _bleState = BLE_AUTHENTICATING;
    int rc = NimBLEDevice::startSecurity(job->client->getConnId());
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        DBG("GP: security initiation failed (%d)", rc);
        strlcpy(_lastError, "GoPro security failed", sizeof(_lastError));
        connectAbort(job->client);
        noteConnectFailure();
        _bleState = BLE_DISCONNECTED;
        return false;
    }
    return true;
}

bool GpCamera::discoverJob() {
    GpConnectJob *job = &_connectJob;
    if (job->gen != _connectGen || !job->client->isConnected()) {
        connectAbort(job->client);
        return false;
    }
    bleLogPush(BLE_LOG_GP_GATT_DISCOVER);
    NimBLERemoteService *pService = job->client->getService(GP_SERVICE_UUID);
    if (!pService) {
        bleLogPush(BLE_LOG_GP_NOT_GOPRO);
        connectAbort(job->client);
        _bleState = BLE_DISCONNECTED;
        // User-facing: tells the user this is not a GoPro camera.
        strlcpy(_lastError, "not a GoPro camera", sizeof(_lastError));
        noteConnectFailure();
        return false;
    }

    _pCommandChar  = pService->getCharacteristic(GP_CHAR_COMMAND);
    _pSettingsChar = pService->getCharacteristic(GP_CHAR_SETTINGS);
    _pQueryChar    = pService->getCharacteristic(GP_CHAR_QUERY);

    NimBLERemoteCharacteristic *pCmdResp  = pService->getCharacteristic(GP_CHAR_CMD_RESPONSE);
    NimBLERemoteCharacteristic *pQueryRsp = pService->getCharacteristic(GP_CHAR_QUERY_RESPONSE);

    if (!_pCommandChar || !_pSettingsChar || !_pQueryChar) {
        bleLogPush(BLE_LOG_GP_MISSING_CHARS);
        connectAbort(job->client);
        _bleState = BLE_DISCONNECTED;
        strlcpy(_lastError, "GoPro service missing chars", sizeof(_lastError));
        noteConnectFailure();
        return false;
    }

    if (pCmdResp && pCmdResp->canNotify()) {
        pCmdResp->subscribe(true, [this](NimBLERemoteCharacteristic *ch,
                uint8_t *data, size_t len, bool notify) {
            if (_pClient && ch->getRemoteService()->getClient() == _pClient)
                notifyCallback(ch, data, len, notify);
        });
    }
    if (pQueryRsp && pQueryRsp->canNotify()) {
        pQueryRsp->subscribe(true, [this](NimBLERemoteCharacteristic *ch,
                uint8_t *data, size_t len, bool notify) {
            if (_pClient && ch->getRemoteService()->getClient() == _pClient)
                notifyCallback(ch, data, len, notify);
        });
    }
    bleLogPush(BLE_LOG_GP_SUBSCRIBED);

    strlcpy(_telemetry.model, "GoPro HERO", sizeof(_telemetry.model));

    if (job->gen != _connectGen || !job->client->isConnected()) {
        connectAbort(job->client);
        return false;
    }

    // Security approval finished before discovery. Status registration can
    // still need retries after pairing; the periodic status path below handles
    // that independently of connection readiness.
    _bleState       = BLE_CONNECTED;
    _connectedAtMs  = millis();
    _lastKeepAlive  = 0;         // Send keep-alive immediately
    _lastStatusPoll = 0;

    sendKeepAlive();
    sendRegisterStatusUpdates();
    _reconnectBackoffMs = BLE_RECONNECT_INTERVAL_MS;
    return true;
}

/// loop()-task half: prepare the client and hand the blocking work over.
/// Returns true when a job was queued (not when the camera is connected).
bool GpCamera::connectToCamera() {
    if (!_hasTargetAddress) return false;
    DBG("GP: Connecting to %s...", _targetAddress.toString().c_str());
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
            DBG("GP: no BLE client slot available");
            noteConnectFailure();
            return false;
        }
        _pClient->setClientCallbacks(this, false);
    }
    bleLinkPrepareClient(_pClient, false);   // low-duty search, default interval

    _connectGen = _connectGen + 1u;
    _connectJob.client  = _pClient;
    _connectJob.address = _targetAddress;
    _connectJob.gen     = _connectGen;
    _securityReady = false;
    _securityFailed = false;

    _jobActive = true;
    if (!bleWorkerSubmit([](void *ctx) {
            GpCamera *camera = static_cast<GpCamera *>(ctx);
            bool result = camera->connectJob();
            camera->_jobActive = false;
            return result;
        }, this)) {
        _jobActive = false;
        // Worker busy or unavailable: let the caller's retry/backoff try again
        // on a later tick.
        _bleState = BLE_DISCONNECTED;
        return false;
    }
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Protocol helpers
// ──────────────────────────────────────────────────────────────────────────────

/// Write to the Command characteristic (with response).
bool GpCamera::writeCommand(const uint8_t *data, size_t len) {
    if (_pCommandChar && _pCommandChar->canWrite()) {
        // Propagate the GATT write result: the manager relies on false to
        // keep the per-camera pending command and retry once the link is ready.
        return _pCommandChar->writeValue((uint8_t *)data, len, true);
    }
    return false;
}

bool GpCamera::sendShutter(bool on) {
    // Encoded JSON payloads per Open GoPro data protocol.
    static const char SHUTTER_ON[]  = "{%230%22shutter%22%3Atrue}";
    static const char SHUTTER_OFF[] = "{%230%22shutter%22%3Afalse}";

    const char *payload = on ? SHUTTER_ON : SHUTTER_OFF;
    size_t plen = strlen(payload);

    // V05: no backing for unchecked copy (fixed strings safe today, fragile on edit).
    uint8_t frame[40];
    if (plen + 2 > sizeof(frame)) return false;
    frame[0] = 0x03;         // Command request header
    frame[1] = (uint8_t)plen;
    memcpy(&frame[2], payload, plen);

    if (writeCommand(frame, plen + 2)) {
        DBG("GP: Sent shutter=%s", on ? "ON" : "OFF");
        return true;
    }
    DBG("GP: Failed to send shutter command");
    return false;
}

void GpCamera::sendKeepAlive() {
    if (!_pSettingsChar || !_pSettingsChar->canWrite()) return;
    const uint8_t ka[3] = {0x02, 0x01, 0x42};  // Setting request | len 1 | 0x42
    _pSettingsChar->writeValue((uint8_t *)ka, sizeof(ka), true);
}

void GpCamera::sendRegisterStatusUpdates() {
    if (!_pQueryChar || !_pQueryChar->canWrite()) return;
    // [0x01|len|0x53 register|ids…] — Encoding + Battery %
    const uint8_t reg[5] = {0x01, 0x03, 0x53,
                            GP_STATUS_ENCODING, GP_STATUS_BATTERY_PCT};
    _pQueryChar->writeValue((uint8_t *)reg, sizeof(reg), true);
    _registeredStatuses = true;
    DBG("GP: Registered status updates");
}

void GpCamera::sendStatusQuery() {
    if (!_pQueryChar || !_pQueryChar->canWrite()) return;
    // Get Status Values (id 0x13) for the same set of statuses.
    const uint8_t q[5] = {0x01, 0x03, 0x13,
                          GP_STATUS_ENCODING, GP_STATUS_BATTERY_PCT};
    _pQueryChar->writeValue((uint8_t *)q, sizeof(q), true);
}

// ──────────────────────────────────────────────────────────────────────────────
// Query response parsing
// ──────────────────────────────────────────────────────────────────────────────
// Notifications arrive as [header][len?][entries…].  Entry layouts seen in
// the wild differ between firmware generations, so we walk the buffer
// tolerantly looking for [statusId][size][value] or [statusId][value].

void GpCamera::applyStatus(uint8_t id, uint32_t value) {
    switch (id) {
        case GP_STATUS_ENCODING:
            if (value == 1 && _telemetry.state != CAM_STATE_RECORDING) {
                _recStartMs = millis();   // Start local rec-time clock
            }
            _telemetry.state =
                value ? CAM_STATE_RECORDING : CAM_STATE_STANDBY;
            _telemetry.dataValid = true;
            break;

        case GP_STATUS_BATTERY_PCT:
            if (value <= 100) _telemetry.batteryPercent = value;
            _telemetry.dataValid = true;
            break;

        case GP_STATUS_BATTERY_BARS:
            if (_telemetry.batteryPercent > 100) {
                // Only use bars if percentage isn't available.
                static const uint8_t barsToPct[5] = {5, 25, 50, 75, 95};
                if (value < 5) _telemetry.batteryPercent = barsToPct[value];
            }
            break;

        default:
            break;
    }
}

// Notifications arrive as [header][len?][entries…], entries are pairs of
// [statusId][uint8 value] for the statuses we register.  Header bytes seen in
// the wild: 0xB3 (async update), 0x93/0x53/0x13/0xD3 (query responses).
void GpCamera::parseQueryResponse(const uint8_t *data, size_t len) {
    size_t i = 0;

    // Skip leading header/length bytes.
    while (i + 1 < len) {
        uint8_t b = data[i];
        if (b == 0xB3 || b == 0x93 || b == 0xD3 || b == 0x53 || b == 0x13) {
            i++;
            continue;
        }
        break;
    }
    if (i < len && (len - i - 1) == data[i]) {
        i++;  // Looks like an explicit length byte — skip it too.
    }

    // Walk [id][u8 value] pairs.
    while (i + 1 < len) {
        uint8_t id = data[i];
        uint8_t val = data[i + 1];
        i += 2;
        applyStatus(id, val);
        // E04: runs on the NimBLE host task ⇒ deferred ring, never DBG here.
        bleLogPush(BLE_LOG_GP_STATUS, nullptr, 0, id, val);
    }
}

void GpCamera::notifyCallback(NimBLERemoteCharacteristic *pChar, uint8_t *pData,
                           size_t length, bool isNotify) {
    // E04: this runs on the NimBLE host task. Nothing below may format or
    // print — every log line is handed to the deferred ring instead.
    if (!length) return;

    if (pChar == _pQueryChar || pData[0] == 0xB3 || pData[0] == 0x93) {
        parseQueryResponse(pData, length);
        return;
    }

    // Command responses (0x83 frames): treat any response as link activity.
    if (pData[0] == 0x83) {
        bleLogPush(BLE_LOG_GP_ACK, nullptr, 0, 0, 0, 0, 0, (int16_t)length);
        return;
    }

    bleLogPush(BLE_LOG_GP_RAW, pData, (uint8_t)(length > 32 ? 32 : length),
               0, 0, 0, 0, (int16_t)length);
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

void GpCamera::init() {
    configASSERT(!bleWorkerBusy());
    configASSERT(_pClient == nullptr);
    _pCommandChar = _pSettingsChar = _pQueryChar = nullptr;
    DBG("GP: Backend ready");
    _bleState = BLE_DISCONNECTED;
    // Boot priority: first direct connect to the saved camera is due at once.
    _lastReconnectAttempt = millis() - BLE_RECONNECT_INTERVAL_MS;
    _telemetry = CameraTelemetry();
    _registeredStatuses = false;
    _hasTargetAddress = false;
    _doConnect = false;
    _reconnectBackoffMs = BLE_RECONNECT_INTERVAL_MS;
    _securityReady = false;
    _securityFailed = false;
    _recStartMs = 0;
    _lastError[0] = '\0';
}

void GpCamera::release() {
    // H06: hand the NimBLEClient back to the recycler. It is parked and reused
    // for the next connection — never deleted, because NimBLE may still have a
    // GAP event queued for it (see ble_recycler.h).
    if (!_pClient) return;

    NimBLEClient *doomed = _pClient;
    _pClient = nullptr;         // detach first: no other path may touch it now
    _pCommandChar  = nullptr;
    _pSettingsChar = nullptr;
    _pQueryChar    = nullptr;

    _bleState            = BLE_DISCONNECTED;
    _hasTargetAddress    = false;   // drop the target so no reconnect fires
    _doConnect           = false;
    _registeredStatuses  = false;
    _telemetry.dataValid = false;
    _telemetry.state     = CAM_STATE_UNKNOWN;
    _recStartMs          = 0;

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
        DBG("GP: WARN — no recycle slot free, leaking client");
    }
}

void GpCamera::update(bool allowConnect) {
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
            if (camIsOtaQuiet() || !allowConnect) break;
            // Never start a second attempt while the worker is still busy.
            if (bleWorkerBusy()) break;
            // Honour pending direct-connect (Web UI "Use" / camKick) at once
            // instead of stalling until the next scan interval.
            if (_doConnect) {
                _lastReconnectAttempt = now;
                _doConnect = false;
                connectToCamera();
                break;
            }
            // Reconnect this slot's own MAC, independently of the primary.
            {
                if (_hasTargetAddress &&
                    (now - _lastReconnectAttempt) >= _reconnectBackoffMs) {
                    _lastReconnectAttempt = now;
                    connectToCamera();
                }
            }
            break;

        case BLE_SCANNING:
            // Discovery is owned by the manager.
            break;

        case BLE_AUTHENTICATING:
            if (camIsOtaQuiet()) break;
            if (_securityFailed || now - _connectedAtMs >= 30000) {
                DBG("GP: pairing failed or timed out");
                strlcpy(_lastError, "GoPro pairing failed or timed out", sizeof(_lastError));
                if (_pClient) _pClient->disconnect();
                noteConnectFailure();
                _bleState = BLE_DISCONNECTED;
            } else if (_securityReady && allowConnect && !bleWorkerBusy()) {
                _jobActive = true;
                if (!bleWorkerSubmit([](void *ctx) {
                        GpCamera *camera = static_cast<GpCamera *>(ctx);
                        bool result = camera->discoverJob();
                        if (!result && camera->_bleState == BLE_AUTHENTICATING)
                            camera->_bleState = BLE_DISCONNECTED;
                        camera->_jobActive = false;
                        return result;
                    }, this)) _jobActive = false;
            }
            break;

        case BLE_CONNECTED: {
            if (_jobActive || camIsOtaQuiet()) break;
            if (!_pClient || !_pClient->isConnected()) {
                _bleState = BLE_DISCONNECTED;
                break;
            }

            // Local rec-time clock while the camera reports encoding.
            if (_telemetry.state == CAM_STATE_RECORDING && _recStartMs != 0) {
                _telemetry.recTimeSeconds = (now - _recStartMs) / 1000;
            }

            // Keep-alive every ~3 s (Open GoPro best practice).
            // Skipped during OTA quiet so the upload TCP stream is not
            // preempted (link resumes after reboot/resume).
            if (now - _lastKeepAlive >= GOPRO_KEEPALIVE_INTERVAL_MS) {
                _lastKeepAlive = now;
                if (!camIsOtaQuiet()) sendKeepAlive();
            }

            // Re-register + poll statuses shortly after connecting (the first
            // registration can be lost while the pairing prompt is pending).
            if (now - _connectedAtMs >= 4000 && !_registeredStatuses) {
                sendRegisterStatusUpdates();
            }
            if (now - _lastStatusPoll >= GOPRO_STATUS_POLL_MS) {
                _lastStatusPoll = now;
                sendStatusQuery();
            }
            break;
        }
    }
}

bool GpCamera::sendStartRecord() {
    if (_jobActive || _bleState != BLE_CONNECTED) return false;
    // V07: must propagate write result (was always true — no backing, oversight).
    // The manager relies on false to keep the pending command for retry.
    return sendShutter(true);
}

bool GpCamera::sendStopRecord() {
    if (_jobActive || _bleState != BLE_CONNECTED) return false;
    return sendShutter(false);
}

BleConnectionState GpCamera::getState() const { return _bleState; }
const CameraTelemetry& GpCamera::getTelemetry() const { return _telemetry; }
bool GpCamera::isReady() const { return !_jobActive && (_bleState == BLE_CONNECTED); }
const char* GpCamera::getLastError() const { return _lastError; }

void GpCamera::targetMac(const char *mac, uint8_t addressType) {
    if (!mac || !*mac) return;
    NimBLEScan *pScan = NimBLEDevice::getScan();
    if (pScan && pScan->isScanning()) pScan->stop();
    if (_pClient && _pClient->isConnected()) _pClient->disconnect();
    _targetAddress    = NimBLEAddress(std::string(mac), addressType);
    _hasTargetAddress = true;
    _doConnect        = true;
    _reconnectBackoffMs = BLE_RECONNECT_INTERVAL_MS;
    DBG("GP: targeting %s (direct connect)", mac);
}
