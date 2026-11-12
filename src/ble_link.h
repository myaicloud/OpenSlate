// ============================================================================
// ble_link.h — Shared reconnect tuning for the camera BLE backends
// ============================================================================
// Use case: one camera on the copter next to OpenSlate, a stickcam a few metres
// away that is out of range for the whole flight (2–8 min) and must come back
// as fast as possible after landing — without the search disturbing the copter
// camera's link while flying.
//
//   • Search: one long, low-duty connect attempt instead of short full-duty
//     attempts separated by growing pauses. The controller listens a quarter of
//     the time and connects the moment the camera advertises again.
//   • Setup: request a short connection interval while GATT discovery and
//     pairing run (each step is a request/response round trip), then relax.
//   • Fairness: one BLE worker serves both slots, so a long search attempt is
//     cancelled (ble_gap_conn_cancel) whenever another slot, a known-camera
//     scan or the user needs the worker.
// ============================================================================

#ifndef BLE_LINK_H
#define BLE_LINK_H

#include <NimBLEDevice.h>

/// Why a connect attempt ended without a link.
enum BleConnectOutcome : uint8_t {
    BLE_CONNECT_CANCELLED,   // we cancelled it to hand the worker to someone else
    BLE_CONNECT_NOT_FOUND,   // camera never advertised during the whole attempt
    BLE_CONNECT_FAILED,      // camera answered but the link failed (e.g. 0x3E)
};

/// Apply the search profile (and, with fastSetup, the fast setup interval) to a
/// client before connect(). Without fastSetup the NimBLE default 20-40 ms
/// interval is kept — for backends that do not relax the link afterwards.
void bleLinkPrepareClient(NimBLEClient *client, bool fastSetup);

/// Classify a failed client->connect() from the client's last error.
BleConnectOutcome bleLinkClassifyFailure(NimBLEClient *client);

/// After the session is up: relax the connection interval to save airtime.
void bleLinkRelax(NimBLEClient *client);

/// Cancel a pending (still searching) connect attempt so the worker frees up.
/// Returns true when an attempt was cancelled. Safe from the loop() task.
bool bleLinkYield();

/// True while the controller is still searching for a peer to connect to.
bool bleLinkSearching();

#endif // BLE_LINK_H
