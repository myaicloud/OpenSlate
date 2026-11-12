// ============================================================================
// ble_recycler.cpp — NimBLEClient pool: park, settle, reuse (H06)
// ============================================================================
// See ble_recycler.h for why a client is never deleted.
//
// One entry per connection slot, capped at the controller's connection limit.
// A client is never freed, so the pool is the hard upper bound on NimBLEClient
// objects: acquire() returns nullptr while a parked client is still settling,
// and the caller retries on a later tick. Clients are interchangeable, so a
// slot that released its own client simply takes whichever one is ready.
// ============================================================================

#include "ble_recycler.h"
#include "config.h"   // BLE_CLIENT_SETTLE_MS, CAMERA_CONNECTION_SLOTS

// Never exceed the controller's connection limit: createClient() logs a warning
// above it, and a client beyond it could never connect anyway.
static const uint8_t kSlotCount = NIMBLE_MAX_CONNECTIONS;
static_assert(kSlotCount >= CAMERA_CONNECTION_SLOTS,
              "pool must hold one client per connection slot");

enum PoolState { POOL_FREE, POOL_IN_USE, POOL_PARKED, POOL_READY };

struct PoolEntry {
    NimBLEClient *client;
    uint8_t       state;
    uint32_t      parkedMs;
};

static PoolEntry _pool[kSlotCount] = {};
static portMUX_TYPE _poolMux = portMUX_INITIALIZER_UNLOCKED;

bool bleRecycleClient(NimBLEClient *client) {
    if (client == nullptr) return true;

    portENTER_CRITICAL(&_poolMux);
    for (uint8_t i = 0; i < kSlotCount; i++) {
        if (_pool[i].client == client) {
            // Double release: keep the original timestamp so a redundant call
            // cannot extend the settle window.
            if (_pool[i].state != POOL_PARKED) {
                _pool[i].state    = POOL_PARKED;
                _pool[i].parkedMs = millis();
            }
            portEXIT_CRITICAL(&_poolMux);
            return true;
        }
    }
    for (uint8_t i = 0; i < kSlotCount; i++) {
        if (_pool[i].state == POOL_FREE) {
            _pool[i].client   = client;
            _pool[i].state    = POOL_PARKED;
            _pool[i].parkedMs = millis();
            portEXIT_CRITICAL(&_poolMux);
            return true;
        }
    }
    portEXIT_CRITICAL(&_poolMux);
    return false;
}

void bleRecycleProcess() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < kSlotCount; i++) {
        portENTER_CRITICAL(&_poolMux);
        NimBLEClient *c = _pool[i].client;
        uint8_t state   = _pool[i].state;
        uint32_t parked = _pool[i].parkedMs;
        portEXIT_CRITICAL(&_poolMux);

        if (state != POOL_PARKED) continue;
        if (c->isConnected()) continue;                     // teardown running
        if (now - parked < BLE_CLIENT_SETTLE_MS) continue;  // GAP event pending

        portENTER_CRITICAL(&_poolMux);
        if (_pool[i].client == c && _pool[i].state == POOL_PARKED) {
            _pool[i].state = POOL_READY;
        }
        portEXIT_CRITICAL(&_poolMux);
    }
}

NimBLEClient *bleRecycleAcquire() {
    for (uint8_t i = 0; i < kSlotCount; i++) {
        portENTER_CRITICAL(&_poolMux);
        bool ready = (_pool[i].state == POOL_READY);
        NimBLEClient *c = _pool[i].client;
        if (ready) _pool[i].state = POOL_IN_USE;
        portEXIT_CRITICAL(&_poolMux);
        if (ready) return c;
    }

    // Nothing settled yet: grow the pool while it has room. createClient() is
    // not thread-safe, so this must stay on loop().
    for (uint8_t i = 0; i < kSlotCount; i++) {
        portENTER_CRITICAL(&_poolMux);
        bool free = (_pool[i].state == POOL_FREE);
        portEXIT_CRITICAL(&_poolMux);
        if (!free) continue;

        NimBLEClient *c = NimBLEDevice::createClient();
        if (!c) return nullptr;

        portENTER_CRITICAL(&_poolMux);
        _pool[i].client   = c;
        _pool[i].state    = POOL_IN_USE;
        _pool[i].parkedMs = 0;
        portEXIT_CRITICAL(&_poolMux);
        return c;
    }
    return nullptr;
}
