// ============================================================================
// osd_slots.cpp — Betaflight Custom Message 1..4 content manager
// ============================================================================
// Slot content options (OsdSlotContent in settings.h):
//   OFF / CAM_STATUS / REC_TIME / BATTERY / LINK / FC_BATT / ARM_STATE /
//   CAM_COUNT / SUMMARY / ALERTS
//
// SUMMARY and ALERTS cover every saved camera (see osd_status.h); the others
// describe the primary camera only.
//
// Betaflight limits each custom message to 16 characters (MAX_NAME_LENGTH),
// same as pilot/craft name.
// ============================================================================

#include "osd_slots.h"
#include "config.h"
#include "settings.h"
#include "camera_manager.h"
#include "fc_status.h"
#include "recorder.h"
#include "msp_protocol.h"
#include "osd_status.h"

// ──────────────────────────────────────────────────────────────────────────────
// Internal state
// ──────────────────────────────────────────────────────────────────────────────

static uint32_t _lastPush        = 0;
static char     _lastSent[4][OSD_MAX_TEXT_LEN + 1] = {"", "", "", ""};
static bool     _firstRun        = true;

// Multi-camera lines, rebuilt once per push so two slots never disagree and
// the alert rotation advances exactly once per interval.
static char     _summaryText[OSD_MAX_TEXT_LEN + 1] = "";
static char     _alertText[OSD_MAX_TEXT_LEN + 1]   = "";
static bool     _lastDesired     = false;
static uint32_t _desiredEdgeMs   = 0;
static bool     _stoppedEdgeSeen = false;   // a recording ended since boot
static uint8_t  _alertIndex      = 0;
static uint32_t _alertShownMs    = 0;

static_assert(OSD_STATUS_MAX_CAMS == MAX_SAVED_CAMERAS,
              "OSD status and camera registry sizes must match");

/// Snapshot every saved camera plus recorder/FC state for osd_status.
static void buildStatusInput(OsdStatusInput &in, uint32_t now) {
    memset(&in, 0, sizeof(in));
    const OpenSlateSettings &cfg = settingsGet();
    in.camCount = cfg.camCount > OSD_STATUS_MAX_CAMS ? OSD_STATUS_MAX_CAMS : cfg.camCount;
    for (uint8_t i = 0; i < in.camCount; i++) {
        OsdCamInfo &c = in.cams[i];
        CamSavedStatus st;
        camSavedStatus(i, st);
        strlcpy(c.tag, cfg.cams[i].tag, sizeof(c.tag));
        // With more saved cameras than connection slots, only the ones that
        // hold a slot are expected; otherwise a missing camera must count.
        c.expected    = st.assigned || cfg.camCount <= CAMERA_CONNECTION_SLOTS;
        c.ready       = st.ready;
        c.recording   = st.ready && st.tel.state == CAM_STATE_RECORDING;
        c.recRejected = st.recRejected;
        c.stopPending = st.stopPending;
        c.battery     = st.tel.dataValid ? st.tel.batteryPercent : 255;
        c.recSeconds  = st.tel.recTimeSeconds;
    }
    in.recDesired     = camRecordingDesired();
    in.sinceRecEdgeMs = now - _desiredEdgeMs;
    in.fcAlive        = fcGetTelemetry().fcAlive;
    in.showAllStopped = _stoppedEdgeSeen && !in.recDesired &&
                        in.sinceRecEdgeMs < OSD_ALL_STOPPED_MS;
    in.lowBatteryPct  = OSD_LOW_CAM_BATT_PCT;
}

/// Rebuild the summary and alert lines (call once per push interval).
static void updateStatusLines(uint32_t now) {
    bool desired = camRecordingDesired();
    if (desired != _lastDesired) {
        _lastDesired   = desired;
        _desiredEdgeMs = now;
        if (!desired) _stoppedEdgeSeen = true;
    }

    OsdStatusInput in;
    buildStatusInput(in, now);
    osdFormatSummary(in, _summaryText, sizeof(_summaryText));

    static char alerts[8][OSD_ALERT_TEXT_LEN + 1];
    uint8_t n = osdCollectAlerts(in, alerts, 8);
    if (n == 0) {
        _alertText[0] = '\0';
        _alertIndex = 0;
        return;
    }
    if (_alertText[0] == '\0') {
        _alertIndex = 0;            // a new problem shows up at once
        _alertShownMs = now;
    } else if (now - _alertShownMs >= OSD_ALERT_ROTATE_MS) {
        _alertIndex++;
        _alertShownMs = now;
    }
    strlcpy(_alertText, alerts[_alertIndex % n], sizeof(_alertText));
}

// ──────────────────────────────────────────────────────────────────────────────
// Content formatters
// ──────────────────────────────────────────────────────────────────────────────

static void formatMmSs(uint16_t seconds, char *out, size_t len) {
    snprintf(out, len, "%02u:%02u", seconds / 60, seconds % 60);
}

/// Build the string for one slot into `buf` (max OSD_MAX_TEXT_LEN chars).
/// NOTE: Takes a telemetry snapshot once per call so repeated backend reads
/// can't tear a single slot's text (e.g. state changes mid-format).
static void buildSlotString(uint8_t content, char *buf, size_t bufLen) {
    const CameraTelemetry tel = camGetTelemetry();  // snapshot (copy)
    const FcTelemetry fc = fcGetTelemetry();        // snapshot (copy)
    const BleConnectionState bleState = camGetState();

    switch (content) {

        case OSD_SLOT_CAM_STATUS: {
            if (bleState == BLE_DISCONNECTED ||
                bleState == BLE_SCANNING) {
                snprintf(buf, bufLen, "%s",
                         bleState == BLE_SCANNING ? "CAM SCAN" : "CAM OFF");
            } else if (!tel.dataValid && bleState != BLE_CONNECTED) {
                snprintf(buf, bufLen, "CAM PAIR");
            } else {
                uint16_t t = tel.recTimeSeconds;
                switch (tel.state) {
                    case CAM_STATE_RECORDING:
                        if (tel.batteryPercent <= 100) {
                            snprintf(buf, bufLen, "REC %u%% %02u:%02u",
                                     tel.batteryPercent, t / 60, t % 60);
                        } else {
                            // NOTE: must use a temp buffer — snprintf() with
                            // overlapping src/dst is undefined behaviour.
                            char timeBuf[8];
                            formatMmSs(t, timeBuf, sizeof(timeBuf));
                            snprintf(buf, bufLen, "REC %s", timeBuf);
                        }
                        break;
                    case CAM_STATE_STANDBY:
                        if (tel.batteryPercent <= 100) {
                            snprintf(buf, bufLen, "STBY %u%%", tel.batteryPercent);
                        } else {
                            snprintf(buf, bufLen, "STBY");
                        }
                        break;
                    default:
                        // Telemetry unknown but link is up (typical for DJI —
                        // battery/state parse is still experimental, and GoPro
                        // before first status notify). Show READY, not "???",
                        // so the OSD/dashboard read as connected. BAT/REC slots
                        // honestly keep showing "--" until real values arrive.
                        snprintf(buf, bufLen, "CAM READY");
                        break;
                }
            }
            break;
        }

        case OSD_SLOT_REC_TIME: {
            if (!camIsReady()) { snprintf(buf, bufLen, "REC --:--"); break; }
            uint16_t t = tel.recTimeSeconds;
            snprintf(buf, bufLen, "REC %02u:%02u", t / 60, t % 60);
            break;
        }

        case OSD_SLOT_BATTERY: {
            if (tel.dataValid && tel.batteryPercent <= 100) {
                snprintf(buf, bufLen, "BAT %u%%", tel.batteryPercent);
            } else {
                snprintf(buf, bufLen, "BAT --");
            }
            break;
        }

        case OSD_SLOT_LINK: {
            switch (bleState) {
                case BLE_CONNECTED:     snprintf(buf, bufLen, "LINK READY"); break;
                case BLE_AUTHENTICATING:snprintf(buf, bufLen, "LINK PAIR"); break;
                case BLE_CONNECTING:    snprintf(buf, bufLen, "LINK CONN"); break;
                case BLE_SCANNING:      snprintf(buf, bufLen, "LINK SCAN"); break;
                default:                snprintf(buf, bufLen, "LINK OFF"); break;
            }
            break;
        }

        case OSD_SLOT_FC_BATT: {
            if (fc.vbat10 > 0) {
                snprintf(buf, bufLen, "FC %2u.%1uV", fc.vbat10 / 10, fc.vbat10 % 10);
            } else {
                snprintf(buf, bufLen, "FC --.-V");
            }
            break;
        }

        case OSD_SLOT_ARM_STATE: {
            if (!fc.fcAlive)     snprintf(buf, bufLen, "FC NOLINK");
            else if (fc.armed)   snprintf(buf, bufLen, "ARMED");
            else                 snprintf(buf, bufLen, "DISARMED");
            break;
        }

        case OSD_SLOT_CAM_COUNT: {
            // Ready cameras out of the assigned slots, so the pilot can see
            // when the stickcam is back after the flight.
            uint8_t ready = camConnectedCount();
            uint8_t assigned = camAssignedCount();
            if (assigned == 0) snprintf(buf, bufLen, "CAMS 0");
            else               snprintf(buf, bufLen, "CAMS %u/%u", ready, assigned);
            break;
        }

        case OSD_SLOT_SUMMARY:
            strlcpy(buf, _summaryText, bufLen);
            break;

        case OSD_SLOT_ALERTS:
            strlcpy(buf, _alertText, bufLen);
            break;

        case OSD_SLOT_OFF:
        default:
            buf[0] = '\0';
            break;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

void osdSlotsInit() {
    for (int i = 0; i < 4; i++) _lastSent[i][0] = '\0';
}

const char* osdSlotText(uint8_t slot) {
    if (slot >= 4) return "";
    return _lastSent[slot];
}

void osdSlotsUpdate() {
    uint32_t now = millis();
    if (!_firstRun && now - _lastPush < OSD_UPDATE_INTERVAL_MS) return;
    bool forcePush = _firstRun;
    _firstRun = false;
    _lastPush = now;

    const OpenSlateSettings &cfg = settingsGet();
    updateStatusLines(now);

    for (uint8_t slot = 0; slot < 4; slot++) {
        // Only push a slot when its text actually changed.
        char buf[OSD_MAX_TEXT_LEN + 1];
        buildSlotString(cfg.osdSlot[slot], buf, sizeof(buf));

        if (forcePush || strcmp(buf, _lastSent[slot]) != 0) {
            strlcpy(_lastSent[slot], buf, sizeof(_lastSent[slot]));
            mspSendSetText(MSP2TEXT_CUSTOM_MSG_0 + slot, buf);
        }
    }
}
