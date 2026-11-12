// ============================================================================
// ble_build_config.h — NimBLE overrides after the Arduino SDK defaults
// ============================================================================

#ifndef BLE_BUILD_CONFIG_H
#define BLE_BUILD_CONFIG_H

#include <sdkconfig.h>

#undef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 2
#undef CONFIG_BT_NIMBLE_MAX_BONDS
#define CONFIG_BT_NIMBLE_MAX_BONDS 4

// CR04: disable NimBLE's automatic connection re-attempt (on by default in the
// Arduino-ESP32 sdkconfig). When a connect fails with 0x3E (typical right after
// an ESP reset while the camera still holds the old link), ble_gap.c re-issues
// the connect with a stack-local cookie (`cb_arg = &conn`) and ble_gap_connect()
// stores that pointer as the connection's callback argument. Every GAP event of
// the re-attempted link then reaches NimBLEClient::handleGapEvent() with a
// dangling stack address instead of the client, and the next DISCONNECT
// dereferences garbage (esp_timer_stop(0xFFFFFFFF) -> Load access fault).
// The camera backends already retry with their own backoff.
#undef CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT

#endif // BLE_BUILD_CONFIG_H
