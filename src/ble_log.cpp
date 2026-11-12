// ============================================================================
// ble_log.cpp — Deferred BLE log formatting (E04)
// ============================================================================
// See ble_log.h for the rationale.
//
// Slot accounting: BLE notifications arrive at ~1-10 Hz per characteristic and
// loop() drains the ring every tick (well under a millisecond apart), so a
// backlog of two records is the realistic worst case. Eight slots absorb a
// burst without ever making the producer wait; a full ring drops the record and
// counts it, because blocking the NimBLE host task is the bug we are fixing.
// ============================================================================

#include "ble_log.h"
#include "config.h"   // DBG

#include <string.h>
#include <freertos/FreeRTOS.h>

// Raw payload kept per record.  80 bytes covers a full DJI telemetry push
// (68-byte frames on the Action 2) so every offset is visible in the log.
static const uint8_t kDataLen   = 80;
static const uint8_t kSlotCount = 8;

struct BleLogRecord {
    uint8_t id;
    uint8_t n;
    uint8_t a, b, c, d;
    int16_t len;              // original frame length (may exceed n)
    uint8_t data[kDataLen];
};

static BleLogRecord _ring[kSlotCount];
static uint8_t      _head = 0;   // producer writes here
static uint8_t      _tail = 0;   // consumer reads here
static uint32_t     _dropped = 0;

// Guards _head/_tail and the slot being copied.  Held for one fixed-size
// memcpy only — never across a call that can yield.
static portMUX_TYPE _ringMux = portMUX_INITIALIZER_UNLOCKED;

void bleLogInit() {
    portENTER_CRITICAL(&_ringMux);
    _head = _tail = 0;
    _dropped = 0;
    portEXIT_CRITICAL(&_ringMux);
}

bool bleLogPush(uint8_t id, const uint8_t *data, uint8_t n,
                uint8_t a, uint8_t b, uint8_t c, uint8_t d, int16_t len) {
    if (id == BLE_LOG_NONE) return true;
    if (n > kDataLen) n = kDataLen;

    bool ok = false;
    portENTER_CRITICAL(&_ringMux);
    uint8_t next = (uint8_t)((_head + 1) % kSlotCount);
    if (next != _tail) {
        BleLogRecord &r = _ring[_head];
        r.id = id;
        r.n  = n;
        r.a  = a;
        r.b  = b;
        r.c  = c;
        r.d  = d;
        r.len = len;
        if (n && data) memcpy(r.data, data, n);
        _head = next;
        ok = true;
    } else {
        _dropped++;
    }
    portEXIT_CRITICAL(&_ringMux);
    return ok;
}

// Format n raw bytes as "AA BB CC ".  Drain-side only (loop()), so the
// snprintf loop the callbacks used to run is now off the host task.
static void hexDump(char *out, size_t cap, const uint8_t *src, uint8_t n) {
    size_t o = 0;
    if (cap == 0) return;
    out[0] = '\0';
    for (uint8_t i = 0; i < n && o + 4 < cap; i++) {
        o += (size_t)snprintf(out + o, cap - o, "%02X ", src[i]);
    }
}

void bleLogProcess() {
    for (;;) {
        BleLogRecord rec;

        portENTER_CRITICAL(&_ringMux);
        if (_tail == _head) {
            uint32_t dropped = _dropped;
            _dropped = 0;
            portEXIT_CRITICAL(&_ringMux);
            if (dropped) DBG("BLE: log ring full — %u record(s) dropped", (unsigned)dropped);
            return;
        }
        rec   = _ring[_tail];
        _tail = (uint8_t)((_tail + 1) % kSlotCount);
        portEXIT_CRITICAL(&_ringMux);

        switch (rec.id) {
            case BLE_LOG_DJI_CONNECTED:    DBG("DJI: Connected to camera"); break;
            case BLE_LOG_DJI_DISCONNECTED: DBG("DJI: Disconnected from camera"); break;
            case BLE_LOG_DJI_CONNECT_FAILED: DBG("DJI: Connection failed!"); break;
            case BLE_LOG_DJI_GATT_DISCOVER: DBG("DJI: Connected! Discovering GATT services..."); break;
            case BLE_LOG_DJI_SUBSCRIBED_FFF4: DBG("DJI: Subscribed to FFF4 notifications"); break;
            case BLE_LOG_DJI_SUBSCRIBED_FFF5: DBG("DJI: Subscribed to FFF5 notifications"); break;
            case BLE_LOG_DJI_NOT_OSMO:     DBG("DJI: Service 0xFFF0 not found! Disconnecting."); break;
            case BLE_LOG_DJI_MISSING_CHARS: DBG("DJI: Missing FFF4 or FFF5 characteristics!"); break;
            case BLE_LOG_DJI_DUML:
                DBG("DJI: <<< DUML [%s] flags=0x%02X set=0x%02X id=0x%02X len=%d",
                    rec.a ? "FFF4" : "FFF5", rec.b, rec.c, rec.d, (int)rec.len);
                break;
            case BLE_LOG_DJI_RAW: {
                char hex[4 * kDataLen + 1];
                hexDump(hex, sizeof(hex), rec.data, rec.n);
                DBG("DJI: <<< NON-DUML [%s] (%d bytes): %s",
                    rec.a ? "FFF4" : "FFF5", (int)rec.len, hex);
                break;
            }
            case BLE_LOG_DJI_PAIR_PAYLOAD:
                DBG("DJI: Pairing Status Payload: %02X %02X", rec.a, rec.b);
                break;
            case BLE_LOG_DJI_ALREADY_PAIRED:
                DBG("DJI: *** ALREADY PAIRED - SESSION ESTABLISHED ***");
                break;
            case BLE_LOG_DJI_APPROVAL_REQ:
                DBG("DJI: Approval required - TAP APPROVE ON CAMERA SCREEN!");
                break;
            case BLE_LOG_DJI_APPROVED:
                DBG("DJI: *** PAIRING APPROVED - SESSION ESTABLISHED ***");
                break;
            case BLE_LOG_DJI_REPLY:
                switch (rec.a) {
                    case 0x00: DBG("DJI: record command OK"); break;
                    case 0xd8: DBG("DJI: record cmd: resource not ready"); break;
                    case 0xd9: DBG("DJI: record cmd: wrong state (already rec?)"); break;
                    case 0xdf: DBG("DJI: record cmd: wrong parameter"); break;
                    case 0xe3: DBG("DJI: record cmd: bad/missing parameter"); break;
                    case 0xe0: DBG("DJI: record cmd: NOT SUPPORTED by camera"); break;
                    default:   DBG("DJI: record cmd reply 0x%02X", rec.a); break;
                }
                break;
            case BLE_LOG_DJI_TELE: {
                char hex[4 * kDataLen + 1];
                hexDump(hex, sizeof(hex), rec.data, rec.n);
                DBG("DJI: FRAME flags=0x%02X set=0x%02X id=0x%02X len=%d %s",
                    rec.a, rec.b, rec.c, (int)rec.len, hex);
                break;
            }
            case BLE_LOG_GP_CONNECTED:    DBG("GP: Connected to camera"); break;
            case BLE_LOG_GP_DISCONNECTED: DBG("GP: Disconnected from camera"); break;
            case BLE_LOG_GP_CONNECT_FAILED: DBG("GP: Connection failed!"); break;
            case BLE_LOG_GP_GATT_DISCOVER: DBG("GP: Discovering GATT services..."); break;
            case BLE_LOG_GP_NOT_GOPRO:    DBG("GP: Service 0xFEA6 not found! Disconnecting."); break;
            case BLE_LOG_GP_MISSING_CHARS: DBG("GP: Missing required characteristics!"); break;
            case BLE_LOG_GP_SUBSCRIBED:   DBG("GP: Subscribed to command/query responses"); break;
            case BLE_LOG_GP_STATUS:
                DBG("GP: status 0x%02X = %u", rec.a, rec.b);
                break;
            case BLE_LOG_GP_ACK:
                DBG("GP: command ack (%d bytes)", (int)rec.len);
                break;
            case BLE_LOG_GP_RAW: {
                char hex[4 * kDataLen + 1];
                hexDump(hex, sizeof(hex), rec.data, rec.n);
                DBG("GP: <<< notify (%d bytes): %s", (int)rec.len, hex);
                break;
            }
            default: break;
        }
    }
}
