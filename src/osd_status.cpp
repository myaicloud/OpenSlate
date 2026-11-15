// ============================================================================
// osd_status.cpp — Multi-camera summary line and pilot alert line
// ============================================================================
// See osd_status.h. Pure C++, compiled both for the ESP32 and the host tests.
// ============================================================================

#include "osd_status.h"
#include <stdio.h>
#include <string.h>

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────

/// Append `part` only if the whole part still fits; never cut it in half, so
/// the OSD never shows a truncated battery or time.
static bool appendPart(char *buf, size_t len, const char *part) {
    size_t used = strlen(buf);
    size_t add  = strlen(part);
    if (used + add + 1 > len) return false;
    memcpy(buf + used, part, add + 1);
    return true;
}

static void camTag(const OsdStatusInput &in, uint8_t i, char *out, size_t len) {
    if (in.cams[i].tag[0]) snprintf(out, len, "%s", in.cams[i].tag);
    else                   snprintf(out, len, "C%u", (unsigned)(i + 1));
}

static void formatTime(uint16_t seconds, char *out, size_t len) {
    snprintf(out, len, "%02u:%02u", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
}

// ──────────────────────────────────────────────────────────────────────────────
// Summary line
// ──────────────────────────────────────────────────────────────────────────────

void osdFormatSummary(const OsdStatusInput &in, char *buf, size_t len) {
    if (!buf || len == 0) return;
    buf[0] = '\0';

    uint8_t count = in.camCount > OSD_STATUS_MAX_CAMS ? OSD_STATUS_MAX_CAMS : in.camCount;
    if (count == 0) {
        appendPart(buf, len, "CAM NONE");
        return;
    }

    uint8_t expected = 0, ready = 0, recording = 0;
    uint16_t longest = 0;
    for (uint8_t i = 0; i < count; i++) {
        const OsdCamInfo &c = in.cams[i];
        if (!c.expected) continue;
        expected++;
        if (c.ready) ready++;
        if (c.recording) {
            recording++;
            if (c.recSeconds > longest) longest = c.recSeconds;
        }
    }

    const bool recMode = in.recDesired || recording > 0;
    char part[12];
    if (recMode) snprintf(part, sizeof(part), "R%u/%u", (unsigned)recording, (unsigned)expected);
    else         snprintf(part, sizeof(part), "CAM %u/%u", (unsigned)ready, (unsigned)expected);
    appendPart(buf, len, part);

    // One battery per expected camera, in registry order. 100 % is shown as
    // 99 so every value is two characters wide and the line stays stable.
    for (uint8_t i = 0; i < count; i++) {
        const OsdCamInfo &c = in.cams[i];
        if (!c.expected) continue;
        if (c.ready && c.battery <= 100) {
            snprintf(part, sizeof(part), " %u", (unsigned)(c.battery > 99 ? 99 : c.battery));
        } else {
            snprintf(part, sizeof(part), " --");
        }
        if (!appendPart(buf, len, part)) return;
    }

    if (recMode) {
        char t[10];
        if (recording > 0) formatTime(longest, t, sizeof(t));
        else               snprintf(t, sizeof(t), "--:--");
        snprintf(part, sizeof(part), " %s", t);
        appendPart(buf, len, part);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Alert line
// ──────────────────────────────────────────────────────────────────────────────

enum AlertKind : uint8_t {
    ALERT_REC_FAIL,
    ALERT_NOT_REC,
    ALERT_LOST,
    ALERT_STOP_WAIT,
    ALERT_STILL_REC,
    ALERT_LOW_BATT,
    ALERT_KIND_COUNT,
};

static bool camHasAlert(const OsdStatusInput &in, const OsdCamInfo &c, AlertKind kind) {
    if (!c.expected) return false;
    const bool settled = in.sinceRecEdgeMs >= OSD_NOT_REC_GRACE_MS;
    switch (kind) {
        case ALERT_REC_FAIL:
            return in.recDesired && c.ready && !c.recording && c.recRejected;
        case ALERT_NOT_REC:
            return in.recDesired && c.ready && !c.recording && !c.recRejected && settled;
        case ALERT_LOST:
            return in.recDesired && !c.ready;
        case ALERT_STOP_WAIT:
            return !in.recDesired && c.stopPending;
        case ALERT_STILL_REC:
            return !in.recDesired && c.ready && c.recording && !c.stopPending && settled;
        case ALERT_LOW_BATT:
            return c.ready && c.battery <= 100 && c.battery <= in.lowBatteryPct;
        default:
            return false;
    }
}

static void formatAlert(const OsdStatusInput &in, uint8_t i, AlertKind kind,
                        char *out, size_t len) {
    char tag[OSD_CAM_TAG_LEN + 2];
    camTag(in, i, tag, sizeof(tag));
    switch (kind) {
        case ALERT_REC_FAIL:  snprintf(out, len, "%s REC FAIL", tag); break;
        case ALERT_NOT_REC:   snprintf(out, len, "%s NOT REC", tag); break;
        case ALERT_LOST:      snprintf(out, len, "%s LOST", tag); break;
        case ALERT_STOP_WAIT: snprintf(out, len, "%s STOP WAIT", tag); break;
        case ALERT_STILL_REC: snprintf(out, len, "%s STILL REC", tag); break;
        case ALERT_LOW_BATT:
            snprintf(out, len, "%s BAT %u", tag, (unsigned)in.cams[i].battery);
            break;
        default: out[0] = '\0'; break;
    }
}

uint8_t osdCollectAlerts(const OsdStatusInput &in,
                         char out[][OSD_ALERT_TEXT_LEN + 1], uint8_t maxAlerts) {
    uint8_t n = 0;
    uint8_t count = in.camCount > OSD_STATUS_MAX_CAMS ? OSD_STATUS_MAX_CAMS : in.camCount;
    bool anyBusy = false;   // a camera still recording or waiting for STOP

    for (uint8_t k = 0; k < ALERT_KIND_COUNT; k++) {
        for (uint8_t i = 0; i < count; i++) {
            const OsdCamInfo &c = in.cams[i];
            if (c.expected && (c.stopPending || c.recording)) anyBusy = true;
            if (!camHasAlert(in, c, (AlertKind)k)) continue;
            if (n < maxAlerts) formatAlert(in, i, (AlertKind)k, out[n++], OSD_ALERT_TEXT_LEN + 1);
        }
    }

    if (!in.fcAlive && n < maxAlerts) {
        snprintf(out[n++], OSD_ALERT_TEXT_LEN + 1, "FC NOLINK");
    }
    // Landing confirmation: every camera is stopped, safe to power them off.
    if (in.showAllStopped && !in.recDesired && !anyBusy && count > 0 && n < maxAlerts) {
        snprintf(out[n++], OSD_ALERT_TEXT_LEN + 1, "ALL STOPPED");
    }
    return n;
}

// ──────────────────────────────────────────────────────────────────────────────
// Tag normalisation
// ──────────────────────────────────────────────────────────────────────────────

size_t osdNormaliseTag(const char *src, char *dst, size_t dstLen) {
    if (!dst || dstLen == 0) return 0;
    size_t o = 0;
    size_t max = dstLen - 1 < OSD_CAM_TAG_LEN ? dstLen - 1 : OSD_CAM_TAG_LEN;
    for (size_t i = 0; src && src[i] && o < max; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) dst[o++] = c;
    }
    dst[o] = '\0';
    return o;
}
