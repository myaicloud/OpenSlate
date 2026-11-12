// ============================================================================
// camera_manager.h — Runtime camera backend dispatcher
// ============================================================================
// Owns the shared NimBLE stack and two independent connection slots.
// Legacy command/telemetry calls still address the selected primary camera.
// ============================================================================

#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include "camera_common.h"
#include "settings.h"

/// Initialise the BLE stack and connection slots (call once from setup()).
void camInit();

/// Call from loop(). Drives both slots and discovery of visible saved cameras.
void camUpdate();

/// Set the recording intent for every assigned camera. Each camera receives an
/// absolute START/STOP as soon as it is ready; a camera that is offline keeps
/// the command pending and gets it right after reconnecting.
void camSetRecordingDesired(bool on);
bool camRecordingDesired();

BleConnectionState   camGetState();
const CameraTelemetry& camGetTelemetry();

/// True when the primary camera can accept commands right now.
bool camIsReady();
/// Number of assigned cameras that are ready (0..CAMERA_CONNECTION_SLOTS).
uint8_t camConnectedCount();
/// Number of cameras currently holding a connection slot (0..2).
uint8_t camAssignedCount();
bool camSavedIsReady(uint8_t saved);
const char* camGetLastError();

/// Human-readable name of the active backend ("DJI Osmo" / "GoPro").
const char* camGetName();

/// Select the discovery brand and persist it without dropping existing links.
void camSetCamera(CameraType type);

/// Prioritise the selected saved camera. Resolve its presence/address type in
/// a short scan before connecting; never evict a live slot for an offline one.
void camKick();

/// User-initiated discovery scan.  Switches the backend to the brand pill
/// the user picked and calls that backend's startScan() once.  This is the
/// Separate from automatic saved-camera scans; discovery results never
/// enter the saved registry without explicit user approval.
void camStartUserScan();
/// Reserve the scanner for a user action; refuses while connecting or pairing.
/// Stops an internal known-camera scan without affecting existing links.
bool camPrepareUserScan();

/// Disconnect both slots and suspend automatic connections until camKick/reboot.
void camDisconnect();

/// OTA radio-quiet mode: while true, backends must not start scans, attempt
/// reconnects, or send keep-alives — the single 2.4GHz radio belongs to WiFi
/// so the firmware upload doesn't stall (iPhone handshake timeouts show up
/// as bogus "incorrect password"). Set by the OTA upload handler, cleared
/// on END/ABORT/response. Query from dji/gopro update paths.
void camSetOtaQuiet(bool quiet);
bool camIsOtaQuiet();

/// Sanitize device name to prevent XSS injection via BLE advertisements.
/// Only allows alphanumeric, space, dash, underscore, dot. Replaces others with '?'.
void sanitizeDeviceName(char* dest, const char* src, size_t maxSize);

#endif // CAMERA_MANAGER_H
