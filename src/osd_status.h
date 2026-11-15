// ============================================================================
// osd_status.h — Multi-camera summary line and pilot alert line for the OSD
// ============================================================================
// Pure formatting logic (no Arduino, no BLE) so it is unit-tested on the host.
// osd_slots.cpp fills an OsdStatusInput snapshot from the camera manager and
// the FC state, then calls the two formatters below.
//
//   Summary  "CAM 2/2 85 62"     on the ground: connected / expected, batteries
//            "R2/2 85 62 02:41"  recording: recording / expected, batteries, time
//   Alerts   ""                  nothing to report (slot stays empty)
//            "STK NOT REC"       one message at a time, highest priority first
// ============================================================================

#ifndef OSD_STATUS_H
#define OSD_STATUS_H

#include <stddef.h>
#include <stdint.h>

#define OSD_STATUS_MAX_CAMS   4    // == MAX_SAVED_CAMERAS
#define OSD_CAM_TAG_LEN       4    // Short camera label shown in the OSD
#define OSD_ALERT_TEXT_LEN    16   // == Betaflight custom message length

/// One saved camera, as the OSD sees it. Order = saved-registry order.
struct OsdCamInfo {
    char     tag[OSD_CAM_TAG_LEN + 1]; // "" => "C<n>" fallback
    bool     expected;     // Counts towards the "/N" denominator and alerts
    bool     ready;        // Link up and able to take commands
    bool     recording;    // Camera reports (or acknowledged) recording
    bool     recRejected;  // Camera refused the last START
    bool     stopPending;  // A STOP is still waiting to be delivered
    uint8_t  battery;      // 0..100, 255 = unknown
    uint16_t recSeconds;   // Recording time while recording
};

struct OsdStatusInput {
    OsdCamInfo cams[OSD_STATUS_MAX_CAMS];
    uint8_t    camCount;          // Saved cameras (0..OSD_STATUS_MAX_CAMS)
    bool       recDesired;        // Recorder wants all cameras recording
    uint32_t   sinceRecEdgeMs;    // Time since recDesired last changed
    bool       fcAlive;
    bool       showAllStopped;    // Short "all stopped" confirmation window
    uint8_t    lowBatteryPct;     // Alert at or below this camera battery
};

/// Grace after a START/STOP edge before "NOT REC" / "STILL REC" (ACK, status poll).
#define OSD_NOT_REC_GRACE_MS  4000

/// Summary line, at most `len - 1` characters (len should be 17).
void osdFormatSummary(const OsdStatusInput &in, char *buf, size_t len);

/// Collect the active alerts, highest priority first. Returns the count.
uint8_t osdCollectAlerts(const OsdStatusInput &in,
                         char out[][OSD_ALERT_TEXT_LEN + 1], uint8_t maxAlerts);

/// Normalise a user-entered camera label: A-Z, 0-9, at most 4 characters,
/// lowercase folded to uppercase, everything else dropped. Returns length.
size_t osdNormaliseTag(const char *src, char *dst, size_t dstLen);

#endif // OSD_STATUS_H
