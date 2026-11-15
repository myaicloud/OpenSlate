// ============================================================================
// recorder.cpp — Recording decision engine
// ============================================================================

#include "recorder.h"
#include "config.h"
#include "settings.h"
#include "fc_status.h"
#include "camera_manager.h"

// ──────────────────────────────────────────────────────────────────────────────
// Internal state
// ──────────────────────────────────────────────────────────────────────────────

// RC switch tracking
static bool     _switchIsOn       = false;   // Current debounced state
static bool     _switchRawIsOn    = false;   // Raw (un-debounced) state
static uint32_t _switchLastChange = 0;       // millis() of last raw change
static uint16_t _lastRcValue      = 0;       // Last read RC channel value (µs)
static bool     _rcDataValid      = false;   // True after first valid MSP_RC

// Record-on-arm latch + suppression after manual stop
static bool     _roaLatched       = false;   // Latched by arming
static bool     _roaSuppress      = false;   // Manual stop while armed

// Manual (Web UI) latch. Without this, a manual start is undone by the very
// next recorderUpdate(), which recomputes the desired state from the switch
// and the RoA latch alone. Holding it here keeps recording until an explicit
// stop, switch-off or disarm.
static bool     _manualLatched    = false;

// Manual (Web UI) stop while the record switch is ON. Without this latch the
// next recorderUpdate() recomputes desired = switch and restarts recording at
// once. Cleared by the next switch edge, the next arming or a manual start, so
// the following flight is never left unrecorded.
static bool     _manualStopLatched = false;

// Desired state. The camera manager owns the per-camera pending command and
// its retry backoff, so a camera that reconnects later still receives the
// absolute START/STOP.
static bool     _desired          = false;

static bool     _lastArmed        = false;
static bool     _armValid         = false;  // true after first valid arm sample

// ──────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ──────────────────────────────────────────────────────────────────────────────

static void sendStart() {
    camSetRecordingDesired(true);
    DBG("REC: start requested");
}

static void sendStop() {
    camSetRecordingDesired(false);
    DBG("REC: stop requested");
}

/// Clear the record-on-arm latch when the pilot signals "stop" intent.
static void clearRoaIntent() {
    if (_roaLatched)  DBG("REC: record-on-arm latch cleared");
    if (_manualLatched) DBG("REC: manual latch cleared");
    _roaLatched  = false;
    _roaSuppress = false;
    _manualLatched = false;
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

void recorderInit() {
    _switchIsOn = false;
    _switchRawIsOn = false;
    _switchLastChange = 0;
    _lastRcValue = 0;
    _rcDataValid = false;
    _roaLatched = false;
    _roaSuppress = false;
    _manualLatched = false;
    _manualStopLatched = false;
    _desired = false;
    _lastArmed = false;
    _armValid = false;
}

void recorderFeedRcValue(uint16_t rcValueUsec) {
    _lastRcValue = rcValueUsec;
    _rcDataValid = true;

    bool rawOn = (rcValueUsec > settingsGet().rcThresholdUs);
    if (rawOn != _switchRawIsOn) {
        _switchRawIsOn    = rawOn;
        _switchLastChange = millis();
    }
}

void recorderUpdate() {
    const OpenSlateSettings &cfg = settingsGet();
    uint32_t now = millis();

    // ── Debounce the RC switch ──────────────────────────────────────────
    if (_rcDataValid &&
        _switchRawIsOn != _switchIsOn &&
        (now - _switchLastChange >= cfg.debounceMs)) {
        _switchIsOn = _switchRawIsOn;

        DBG("SWITCH: ch%d %s (%u µs)", cfg.auxChannelIndex,
            _switchIsOn ? "ON" : "OFF", _lastRcValue);

        // Explicit user intent via the switch always clears the RoA latch.
        clearRoaIntent();
        if (_manualStopLatched) DBG("REC: manual stop released by switch");
        _manualStopLatched = false;
    }

    // ── Arm-state edges ─────────────────────────────────────────────────
    const FcTelemetry &fc = fcGetTelemetry();

    if (fc.fcAlive && (!_armValid || fc.armed != _lastArmed)) {
        bool rising = fc.armed && !_lastArmed;
        _lastArmed = fc.armed;
        _armValid   = true;

        DBG("FC: %s", fc.armed ? "ARMED" : "DISARMED");

        if (rising && _manualStopLatched) {
            DBG("REC: manual stop released by arming");
            _manualStopLatched = false;
        }
        if (rising && cfg.recordOnArm && !_switchIsOn && !_roaSuppress) {
            DBG("REC: record-on-arm engaged");
            _roaLatched = true;
        }
        if (!rising && cfg.stopOnDisarm) {
            clearRoaIntent();   // Disarm stops recording (default behaviour)
        }
        if (!fc.armed) {
            _roaSuppress = false;  // Fresh arming may latch again
        }
    }

    // ── Compute desired state ───────────────────────────────────────────
    bool newDesired = (_switchIsOn && !_manualStopLatched) || _roaLatched || _manualLatched;

    if (newDesired != _desired) {
        _desired = newDesired;
        if (_desired) sendStart(); else sendStop();
    }
}

void recorderManualStart() {
    // Manual start: behave like a fresh desired=true edge.
    clearRoaIntent();
    _manualStopLatched = false;
    _manualLatched = true;        // Survive the next recorderUpdate() recompute
    if (settingsGet().recordOnArm) {
        _roaLatched = true;       // Keep it running until switch-off/manual stop
    }
    if (!_desired) _desired = true;
    sendStart();
}

void recorderManualStop() {
    clearRoaIntent();             // Drops the manual latch as well
    _desired = false;
    if (_switchIsOn) {
        _manualStopLatched = true;
        DBG("REC: manual stop holds until the switch moves or the FC arms");
    }
    // If armed with record-on-arm enabled and no stop-on-disarm, prevent an
    // immediate re-latch until the FC disarms or the switch is used.
    if (settingsGet().recordOnArm && fcGetTelemetry().armed &&
        !settingsGet().stopOnDisarm) {
        _roaSuppress = true;
    }
    sendStop();
}

bool recorderSwitchOn()         { return _switchIsOn; }
bool recorderDesiredRecording() { return _desired; }
uint16_t recorderLastRcValue()  { return _lastRcValue; }
