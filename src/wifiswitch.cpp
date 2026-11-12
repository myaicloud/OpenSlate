// ============================================================================
// wifiswitch.cpp — Decide whether the Web-UI Wi-Fi AP is up
// ============================================================================
// Two inputs, both optional:
//   • a spare radio switch (debounced, 600 ms) — OFF keeps the AP down;
//   • "hotspot off while armed" — the AP goes down when the FC arms and comes
//     back on disarm, freeing the shared 2.4 GHz radio for the camera links.
// The AP is up only when neither asks for it to be down. This module is the
// single owner of webStart()/webStop() at runtime.
// ============================================================================

#include "wifiswitch.h"
#include "config.h"
#include "settings.h"
#include "web_server.h"
#include "fc_status.h"

// ──────────────────────────────────────────────────────────────────────────────
// Internal state
// ──────────────────────────────────────────────────────────────────────────────

static bool     _rcValid      = false;
static bool     _rawOn        = false;    // Un-debounced switch position
static bool     _stableOn     = true;     // Debounced position (boot default: on)
static uint32_t _lastRawChange = 0;      // 0 = no transition being qualified

// Minimum time a new raw position must hold before it is accepted. Uses a
// fixed 600 ms floor — Wi-Fi must never flap because of a bumpy thumb.
// (Deliberately independent from the record-switch debounce in settings:
// toggling a radio costs far more than a record edge.)
static const uint32_t kWifiSwitchDebounceMs = 600;

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

void wifiSwitchInit() {
    _rcValid = false;
    _stableOn = true;   // AP starts enabled at boot (lock-out protection)
}

void wifiSwitchFeedRc(uint16_t rcValueUsec) {
    _rcValid = true;
    bool rawOn = (rcValueUsec > settingsGet().rcThresholdUs);
    if (rawOn != _rawOn) {
        // Raw edge — restart the qualification window from now.
        _rawOn = rawOn;
        _lastRawChange = millis();
    }
}

// Debounce the radio switch into _stableOn.
static void debounceSwitch() {
    if (!_rcValid) return;
    // If the raw position flipped back during the window, re-arm.
    if (_rawOn == _stableOn) {
        _lastRawChange = 0;
        return;
    }
    if (_lastRawChange != 0 &&
        millis() - _lastRawChange >= kWifiSwitchDebounceMs) {
        _stableOn      = _rawOn;
        _lastRawChange = 0;
        DBG("WIFI-SWITCH: %s", _stableOn ? "ON" : "OFF");
    }
}

static bool _apApplied = true;   // webInit() brings the AP up at boot

void wifiSwitchUpdate() {
    const OpenSlateSettings &cfg = settingsGet();

    bool switchOn = true;                       // no switch configured
    if (cfg.wifiSwitchCh <= 15) {
        debounceSwitch();
        if (_rcValid) switchOn = _stableOn;
    }
    const FcTelemetry &fc = fcGetTelemetry();
    const bool armedOff = cfg.apOffArmed && fc.fcAlive && fc.armed;

    const bool want = switchOn && !armedOff;
    if (want == _apApplied) return;
    _apApplied = want;
    if (want) {
        DBG("WIFI: hotspot ON (%s)", cfg.apOffArmed ? "disarmed / switch on" : "switch on");
        webStart();    // Bring SoftAP + DNS back up
    } else {
        DBG("WIFI: hotspot OFF (%s)", armedOff ? "FC armed" : "switch off");
        webStop();     // Powers down the whole Wi-Fi radio
    }
}
