// ============================================================================
// ble_recycler.h — NimBLEClient pool: park, settle, reuse (H06)
// ============================================================================
// NimBLEDevice::deleteClient() is NOT safe here. It frees the NimBLEClient as
// soon as isConnected() turns false, but isConnected() is only
// "m_conn_id != BLE_HS_CONN_HANDLE_NONE" and handleGapEvent() clears that field
// in the middle of the BLE_GAP_EVENT_DISCONNECT case — before it calls
// onDisconnect() and before it returns. A GAP event that is still queued (or
// dispatched a moment later) then dereferences freed memory:
//
//     ble_npl_callout_stop(&client->m_dcTimer)   // handle == 0xFFFFFFFF
//     -> esp_timer_stop(0xFFFFFFFF)              // Load access fault
//
// That crash was observed on the C3 as a Guru Meditation / task-WDT reboot
// whenever a camera dropped off the link.
//
// The only structural fix is to keep the object alive: a NimBLEClient is parked
// here and handed back out for the next connection instead of being deleted. A
// stale GAP event then hits a live object, where the conn_handle check in
// handleGapEvent() discards it harmlessly.
//
// A backend hands its client over with bleRecycleClient(); bleRecycleProcess()
// promotes it to reusable once the disconnect has settled; bleRecycleAcquire()
// takes it back. Kept global rather than per-backend so a client released
// during a camera-type switch can be reused by the other backend.
// ============================================================================

#ifndef BLE_RECYCLER_H
#define BLE_RECYCLER_H

#include <NimBLEDevice.h>

/// Park `client` for reuse. Takes ownership: the caller must not touch the
/// pointer afterwards. Returns false if every slot is occupied (the caller
/// then owns the leak, not a hung loop).
bool bleRecycleClient(NimBLEClient *client);

/// Promote parked clients whose disconnect has settled to reusable. Cheap;
/// call every camUpdate() tick. Never blocks, never frees.
void bleRecycleProcess();

/// Take a reusable client, or create a new one while the pool has room.
/// Returns nullptr when every parked client is still settling — the caller
/// must then retry on a later tick instead of forcing another client.
/// Must be called from loop() (NimBLEDevice::createClient() is not
/// thread-safe).
NimBLEClient *bleRecycleAcquire();

#endif // BLE_RECYCLER_H
