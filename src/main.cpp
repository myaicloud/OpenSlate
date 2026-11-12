// ============================================================================
// main.cpp — OpenSlate: Main Entry Point & State Machine
// ============================================================================
// Open-source camera control bridge.
//
// This firmware bridges a Betaflight Flight Controller (MSP / UART) with a
// BLE action camera (DJI Osmo Action or GoPro HERO8+).  It reads RC switch
// and arming state from the FC, triggers Start/Stop recording on the camera
// (manually from the Web UI too), and injects live telemetry into the FC's
// four Custom Message OSD slots.
//
// ─── Main Loop Architecture (non-blocking) ──────────────────────────────────
//
//   ┌────────────────────────────────────────────────────────────┐
//   │                        loop()                              │
//   │                                                            │
//   │  1. camUpdate()          — BLE scan/connect/keep-alive     │
//   │     autoBindUpdate()     — Button → auto-bind cameras      │
//   │  2. mspPollRC()          — MSP_RC request on timer         │
//   │  3. mspReadIncoming()    — Parse UART bytes                │
//   │  4. fcStatusUpdate()     — Arm/battery/identity polling    │
//   │  5. recorderUpdate()     — Switch + arm → record commands  │
//   │  6. osdSlotsUpdate()     — Push 4 custom messages          │
//   │  7. webUpdate()          — Captive portal + REST API       │
//   │  8. updateStatusLED()    — Blink pattern for feedback      │
//   │                                                            │
//   └────────────────────────────────────────────────────────────┘
//
// ============================================================================

#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "msp_protocol.h"
#include "fc_status.h"
#include "camera_manager.h"
#include "recorder.h"
#include "osd_slots.h"
#include "web_server.h"
#include "wifiswitch.h"
#include "cam_registry.h"
#include "scan_results.h"
#include "auto_bind.h"
#include <NimBLEDevice.h>

// Max UART bytes parsed per loop() iteration — bounds the MSP pump so a
// chatty FC can never starve BLE / web / OSD in a single pass.
static const uint8_t kMaxUartBytesPerLoop = 64;
// Non-blocking wait for USB CDC at boot (replaces delay(1000)).
static const uint32_t kUsbCdcWaitMs = 1000;

// ──────────────────────────────────────────────────────────────────────────────
// Internal State
// ──────────────────────────────────────────────────────────────────────────────

static uint32_t _lastMspPoll = 0;        // Last MSP_RC request time

// LED blink state
static uint32_t _lastLedToggle = 0;
static bool     _ledState      = false;

// ──────────────────────────────────────────────────────────────────────────────
// LED Helpers
// ──────────────────────────────────────────────────────────────────────────────

static void ledOn() {
    digitalWrite(STATUS_LED_PIN, LED_ACTIVE_LOW ? LOW : HIGH);
    _ledState = true;
}

static void ledOff() {
    digitalWrite(STATUS_LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);
    _ledState = false;
}

/// Blink the LED at a given interval (non-blocking).
static void ledBlink(uint32_t intervalMs) {
    uint32_t now = millis();
    if (now - _lastLedToggle >= intervalMs) {
        _lastLedToggle = now;
        if (_ledState) ledOff(); else ledOn();
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Status LED Patterns
// ──────────────────────────────────────────────────────────────────────────────
//   • BLE disconnected / scanning : slow blink (1000 ms)
//   • BLE connecting / pairing    : fast blink (200 ms)
//   • BLE connected, standby      : solid ON
//   • Recording desired           : medium blink (500 ms)

static void updateStatusLED() {
    // Auto-bind (button) overrides the normal pattern while active.
    switch (autoBindLedMode()) {
        case AUTOBIND_LED_FAST:    ledBlink(AUTOBIND_LED_BLINK_MS); return;
        case AUTOBIND_LED_OK:      ledOn();  return;
        case AUTOBIND_LED_NOTHING: ledOff(); return;
        default: break;
    }
    BleConnectionState bleState = camGetState();

    switch (bleState) {
        case BLE_DISCONNECTED:
        case BLE_SCANNING:
            ledBlink(1000);
            break;

        case BLE_CONNECTING:
        case BLE_AUTHENTICATING:
            ledBlink(200);
            break;

        case BLE_CONNECTED:
            if (recorderDesiredRecording()) {
                ledBlink(500);  // Recording: medium blink
            } else {
                ledOn();        // Standby: solid
            }
            break;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// MSP — Periodic RC channel polling + incoming byte pump
// ──────────────────────────────────────────────────────────────────────────────

static void mspPollRC() {
    uint32_t now = millis();
    if (now - _lastMspPoll >= MSP_RC_POLL_INTERVAL_MS) {
        _lastMspPoll = now;
        mspSendRequest(MSP_RC);
    }
}

static void mspReadIncoming() {
    MspMessage msg{};  // E13: zero-init (no backing for uninit; fail-closed)

    // Drain the UART buffer byte-by-byte into the MSP parser, but with a
    // per-loop budget so a burst of FC traffic can't starve everything else.
    uint8_t budget = kMaxUartBytesPerLoop;
    while (budget-- > 0 && Serial1.available()) {
        uint8_t byte = Serial1.read();

        if (mspParseByte(byte, msg)) {
            // ── Complete MSP message received ───────────────────────────
            DBG("MSP: received CMD=%u size=%u valid=%d error=%d", 
                msg.cmd, msg.payloadSize, msg.valid ? 1 : 0, msg.isError ? 1 : 0);
            
            fcStatusFeed(msg);   // Arm state / voltage / identity

            if (msg.cmd == MSP_RC && msg.valid && !msg.isError) {
                // E06: pin settings once per message (NVS/flash-backed getter).
                const OpenSlateSettings &cfg = settingsGet();
                uint16_t value = mspGetRcChannel(msg, cfg.auxChannelIndex);
                DBG("MSP: RC ch[%d] = %d µs", cfg.auxChannelIndex, value);
                recorderFeedRcValue(value);

                // Separate channel toggles the Wi-Fi AP (255 = disabled).
                if (cfg.wifiSwitchCh <= 15) {
                    wifiSwitchFeedRc(
                        mspGetRcChannel(msg, cfg.wifiSwitchCh));
                }
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Arduino setup()
// ──────────────────────────────────────────────────────────────────────────────

void setup() {
    // ── USB Serial for debug output ─────────────────────────────────────
    Serial.begin(115200);
    // Non-blocking CDC wait: yield while the host enumerates, but never
    // stall boot if USB is unplugged (field use = no USB at all).
    {
        uint32_t cdcStart = millis();
        while (!Serial && (millis() - cdcStart) < kUsbCdcWaitMs) {
            delay(10);
        }
    }

    DBG("============================================");
    DBG("  OpenSlate %s on %s", FIRMWARE_VERSION, OPENSLATE_TARGET_NAME);
    DBG("  Betaflight <-> DJI Osmo / GoPro bridge");
    DBG("  OpenSlate by FPVRookie - GPL-3.0, see LICENSE and NOTICE");
    DBG("============================================");
    DBG("PINS: FC UART RX=%d TX=%d @%d, LED=%d (%s)",
        FC_UART_RX_PIN, FC_UART_TX_PIN, FC_UART_BAUD, STATUS_LED_PIN,
        LED_ACTIVE_LOW ? "active LOW" : "active HIGH");

    // ── Persistent settings ─────────────────────────────────────────────
    settingsLoad();
    DBG("CONFIG: cam=%s auxCh=%d thr=%dµs deb=%dms roa=%d sod=%d",
        cameraTypeName(settingsGet().camera),
        settingsGet().auxChannelIndex, settingsGet().rcThresholdUs,
        settingsGet().debounceMs,
        settingsGet().recordOnArm, settingsGet().stopOnDisarm);

    // ── Status LED ──────────────────────────────────────────────────────
    pinMode(STATUS_LED_PIN, OUTPUT);
    ledOff();

    // H01: log the reset reason so a brownout (BLE+WiFi current spike) is
    // distinguishable from a real crash. The detector level itself is fixed
    // at sdkconfig time (CONFIG_ESP_BROWNOUT_DET_LVL=7, the strictest tier) —
    // the Arduino core exposes no runtime setter, and lowering it would make
    // the board MORE likely to brown out, not less.
    {
        esp_reset_reason_t rr = esp_reset_reason();
        DBG("BOOT: reset reason = %d%s", (int)rr,
            rr == ESP_RST_BROWNOUT ? " (BROWNOUT — check supply/wiring)" : "");
    }

    // ── MSP / UART ──────────────────────────────────────────────────────
    mspInit(Serial1);
    fcStatusInit();
    DBG("MSP: Initialised");

    // ── Recorder decision engine ────────────────────────────────────────
    recorderInit();
    osdSlotsInit();
    scanResultsInit();

    // ── Camera BLE backend (DJI or GoPro per settings) ──────────────────
    camInit();
    autoBindInit();
    // ── Web UI (SoftAP + captive portal + REST API) ─────────────────────
    wifiSwitchInit();
    webInit();

    DBG("SETUP: Complete — entering main loop");
}

// ──────────────────────────────────────────────────────────────────────────────
// Arduino loop() — Non-blocking state machine
// ──────────────────────────────────────────────────────────────────────────────

#ifdef BENCH_RECONNECT_MS
// Bench mode (only when built with -D BENCH_RECONNECT_MS=<ms>): drop every
// live camera link this often to simulate a stickcam leaving radio range, so
// reconnect timing can be measured unattended (see "DJI: TIMING" lines).
static void benchDropLinks() {
    static uint32_t last = 0;
    if (millis() - last < (uint32_t)BENCH_RECONNECT_MS) return;
    last = millis();
    uint8_t n = 0;
    for (NimBLEClient *c : *NimBLEDevice::getClientList()) {
        if (c && c->isConnected()) { c->disconnect(); n++; }
    }
    if (n) DBG("BENCH: dropped %u camera link(s)", n);
}
#endif

void loop() {
#ifdef BENCH_RECONNECT_MS
    benchDropLinks();
#endif
    // 1. Camera management: scan, connect, keep-alive, auto-reconnect.
    camUpdate();
    autoBindUpdate();   // Button poll + auto-bind scan state machine

    // 2. Send periodic MSP_RC requests to the Flight Controller.
    mspPollRC();

    // 3. Parse any incoming UART bytes from the FC (RC + status/analog).
    mspReadIncoming();

    // 4. FC polling state machine (arm detection, battery, identity).
    fcStatusUpdate();

    // 5. Recording decision engine: switch + record-on-arm + manual.
    recorderUpdate();

    // 6. Push the four Custom Message OSD slots to the FC.
    osdSlotsUpdate();

    // 7. Serve the captive portal + REST API.
    camRegistryProcess();   // Flush cameras discovered by BLE scan callbacks
    wifiSwitchUpdate();
    scanResultsEvictStale();   // Evict stale scan entries (TTL 15s)
    webUpdate();

    // 8. Update status LED blink pattern.
    updateStatusLED();

    // 9. Debug heartbeat — if these timestamps stop advancing, loop() is
    //    wedged somewhere (BLE call, driver, etc.).
    static uint32_t lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= 10000) {
        lastHeartbeat = millis();
        DBG("STATE: BLE=%d rc=%u switch=%s desired=%d armed=%d heap=%u temp=%.1fC",
            camGetState(), recorderLastRcValue(),
            recorderSwitchOn() ? "ON" : "OFF",
            recorderDesiredRecording() ? 1 : 0,
            fcGetTelemetry().armed ? 1 : 0,
            ESP.getFreeHeap(), temperatureRead());
    }

    yield();
}
