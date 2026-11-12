// ============================================================================
// ble_link.cpp — Shared reconnect tuning for the camera BLE backends
// ============================================================================
// See ble_link.h for the rationale. All timing constants live in config.h.
// ============================================================================

#include "ble_link.h"
#include "config.h"

void bleLinkPrepareClient(NimBLEClient *client, bool fastSetup) {
    if (!client) return;
    client->setConnectTimeout(BLE_CONNECT_TIMEOUT_MS / 1000);
    // Units: interval 1.25 ms, supervision timeout 10 ms, scan 0.625 ms.
    client->setConnectionParams(fastSetup ? BLE_SETUP_ITVL_MIN : BLE_GAP_INITIAL_CONN_ITVL_MIN,
                                fastSetup ? BLE_SETUP_ITVL_MAX : BLE_GAP_INITIAL_CONN_ITVL_MAX,
                                0, BLE_SUPERVISION_TIMEOUT,
                                BLE_SEARCH_SCAN_ITVL, BLE_SEARCH_SCAN_WINDOW);
}

BleConnectOutcome bleLinkClassifyFailure(NimBLEClient *client) {
    const int err = client ? client->getLastError() : 0;
    if (err == BLE_HS_EAPP)     return BLE_CONNECT_CANCELLED;
    if (err == BLE_HS_ETIMEOUT) return BLE_CONNECT_NOT_FOUND;
    return BLE_CONNECT_FAILED;
}

void bleLinkRelax(NimBLEClient *client) {
    if (!client || !client->isConnected()) return;
    client->updateConnParams(BLE_RUN_ITVL_MIN, BLE_RUN_ITVL_MAX, 0,
                             BLE_SUPERVISION_TIMEOUT);
}

bool bleLinkSearching() {
    return ble_gap_conn_active() != 0;
}

bool bleLinkYield() {
    if (!bleLinkSearching()) return false;
    return ble_gap_conn_cancel() == 0;
}
