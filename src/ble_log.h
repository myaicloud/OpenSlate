// ============================================================================
// ble_log.h — Deferred BLE log formatting (E04)
// ============================================================================
// BLE callbacks (scan results, notifications, disconnect events) run on the
// NimBLE host task.  DBG() there is not harmless: log_printf() ends in
// `while (!uart_ll_is_tx_idle(...))` — a hard busy-wait on the UART TX FIFO.
// A ~100 char line at 115200 baud stalls the host task for ~9 ms, starving the
// BLE stack and delaying every GATT event the radio depends on.
//
// So callbacks never format or print.  They copy the handful of numbers/bytes
// they already have into a fixed slot with bleLogPush(); bleLogProcess() (called
// from camUpdate(), i.e. on loop()) formats and prints them.
//
// Mirrors ble_recycler: fixed module-global storage, non-blocking handover,
// refuse-and-count instead of waiting when full.
// ============================================================================

#ifndef BLE_LOG_H
#define BLE_LOG_H

#include <stdint.h>

// One id per call site.  The payload semantics differ per id and are documented
// at the producer in dji_camera.cpp / gopro_camera.cpp.
enum BleLogId {
    BLE_LOG_NONE = 0,
    BLE_LOG_DJI_CONNECTED,
    BLE_LOG_DJI_DISCONNECTED,
    BLE_LOG_DJI_CONNECT_FAILED,
    BLE_LOG_DJI_GATT_DISCOVER,   // services + characteristics found
    BLE_LOG_DJI_SUBSCRIBED_FFF4,
    BLE_LOG_DJI_SUBSCRIBED_FFF5,
    BLE_LOG_DJI_NOT_OSMO,
    BLE_LOG_DJI_MISSING_CHARS,
    BLE_LOG_DJI_RAW,             // a=charIdx  len  data=raw bytes
    BLE_LOG_DJI_DUML,            // a=charIdx  b=flags  c=cmdSet  d=cmdId  len
    BLE_LOG_DJI_PAIR_PAYLOAD,    // a=payload[0]  b=payload[1]
    BLE_LOG_DJI_ALREADY_PAIRED,
    BLE_LOG_DJI_APPROVAL_REQ,
    BLE_LOG_DJI_APPROVED,
    BLE_LOG_DJI_REPLY,           // a=reply code
    BLE_LOG_DJI_TELE,            // b=cmdSet  c=cmdId  len  data=raw bytes
    BLE_LOG_GP_CONNECTED,
    BLE_LOG_GP_DISCONNECTED,
    BLE_LOG_GP_CONNECT_FAILED,
    BLE_LOG_GP_GATT_DISCOVER,
    BLE_LOG_GP_NOT_GOPRO,
    BLE_LOG_GP_MISSING_CHARS,
    BLE_LOG_GP_SUBSCRIBED,
    BLE_LOG_GP_STATUS,           // a=status id  b=value
    BLE_LOG_GP_ACK,              // len
    BLE_LOG_GP_RAW               // len  data=raw bytes
};

/// Clear the ring.  Call once from camInit() before any callback can fire.
void bleLogInit();

/// Hand one record to the ring.  Safe from the NimBLE host task: no I/O, no
/// allocation, critical section is a single fixed-size memcpy.
/// @param n  number of valid bytes in `data` (clamped to BLE_LOG_DATA_LEN)
/// @return false when the ring is full (record dropped and counted).
bool bleLogPush(uint8_t id, const uint8_t *data = nullptr, uint8_t n = 0,
                uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0,
                int16_t len = 0);

/// Drain and print pending records.  Cheap and bounded; call every camUpdate()
/// tick.  Runs on loop() only — this is where the formatting happens.
void bleLogProcess();

#endif // BLE_LOG_H
