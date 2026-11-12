// ============================================================================
// scan_results.cpp — BLE Scan Results Collector Implementation
// ============================================================================

#include "scan_results.h"
#include "cam_registry.h"
#include "settings.h"
#include "camera_common.h"
#include <freertos/FreeRTOS.h>

// ──────────────────────────────────────────────────────────────────────────────
// Internal state
// ──────────────────────────────────────────────────────────────────────────────
// Written from the NimBLE host task (scanResultsAdd, via the scan callback)
// and read from loop() (Web UI, updateSavedStatus). The whole table is mutated
// per call — evictStale(), findResultByMac(), strlcpy() into entries — so every
// access has to be inside a critical section. Holding a portMUX across a few
// dozen strlcpy()s is fine: there is no I/O, no allocation and no yield.
// ──────────────────────────────────────────────────────────────────────────────

static ScanResult _results[MAX_SCAN_RESULTS];
static uint8_t _count = 0;
static bool _scanning = false;
static bool _scanComplete = false;
static bool _showAll = false;  // "Show All Nearby Devices" mode

static portMUX_TYPE _resultsMux = portMUX_INITIALIZER_UNLOCKED;

static bool macEquals(const char *a, const char *b) {
    return a && b && strcasecmp(a, b) == 0;
}

static int8_t findResultByMac(uint8_t type, const char *mac) {
    for (uint8_t i = 0; i < _count; i++) {
        if (_results[i].type == type && macEquals(_results[i].mac, mac)) {
            return i;
        }
    }
    return -1;
}

/// Drop entries whose lastSeen is older than SCAN_RESULT_TTL_MS.  Cheap
/// O(n) sweep; called on every add.  Keeps the list fresh during a scan
/// so the UI only shows devices that are actually still nearby.
static void evictStale() {
    uint32_t now = millis();
    uint8_t w = 0;
    for (uint8_t r = 0; r < _count; r++) {
        if ((uint32_t)(now - _results[r].lastSeen) <= SCAN_RESULT_TTL_MS) {
            if (w != r) _results[w] = _results[r];
            w++;
        }
    }
    _count = w;
}

/// Find the index of the weakest-RSSI entry, or -1 if the table is empty.
static int8_t findWeakestIdx() {
    if (_count == 0) return -1;
    int8_t worst = 0;
    for (uint8_t i = 1; i < _count; i++) {
        if (_results[i].rssi < _results[worst].rssi) worst = i;
    }
    return worst;
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

void scanResultsInit() {
    portENTER_CRITICAL(&_resultsMux);
    _count = 0;
    _scanning = false;
    _scanComplete = false;
    _showAll = false;
    memset(_results, 0, sizeof(_results));
    portEXIT_CRITICAL(&_resultsMux);
}

void scanResultsStart() {
    // Wipe the previous table and OPEN the collection gate so the next
    // scanResultsAdd() call from the NimBLE callback is accepted.  Without
    // this, _scanning stays false and the gate `if (!_scanning && !
    // _scanComplete) return;` in scanResultsAdd() drops every advertisement.
    portENTER_CRITICAL(&_resultsMux);
    _count        = 0;
    _scanning     = true;
    _scanComplete = false;
    memset(_results, 0, sizeof(_results));
    portEXIT_CRITICAL(&_resultsMux);
}

void scanResultsAdd(uint8_t type, const char *mac, const char *name, int8_t rssi, bool isMatched) {
    if (!mac || !*mac) return;

    // Runs on the NimBLE host task. The gate check and the whole table
    // mutation must be one atomic step: loop() rewrites the same table in
    // scanResultsStart(), scanResultsMarkComplete() and
    // scanResultsUpdateSavedStatus().
    portENTER_CRITICAL(&_resultsMux);

    if (!_scanning && !_scanComplete) {  // Only collect during active scan
        portEXIT_CRITICAL(&_resultsMux);
        return;
    }

    evictStale();

    int8_t idx = findResultByMac(type, mac);
    uint32_t now = millis();

    if (idx >= 0) {
        // Update existing entry (always; this is a fresh sighting).
        _results[idx].rssi = rssi;
        _results[idx].lastSeen = now;
        _results[idx].isMatched = isMatched;
        if (name && *name) {
            strlcpy(_results[idx].name, name, sizeof(_results[idx].name));
        }
        portEXIT_CRITICAL(&_resultsMux);
        return;
    }

    if (_count < MAX_SCAN_RESULTS) {
        // Room available — append.
        ScanResult &r = _results[_count];
        r.type = type;
        strlcpy(r.mac, mac, sizeof(r.mac));
        strlcpy(r.name, name ? name : "", sizeof(r.name));
        r.rssi = rssi;
        r.isMatched = isMatched;
        r.saved = false;
        r.active = false;
        r.lastSeen = now;
        _count++;
        portEXIT_CRITICAL(&_resultsMux);
        return;
    }

    // Table is full.  Evict the weakest-RSSI entry IF the new one is
    // stronger — otherwise drop the new one.  This keeps the strongest
    // nearby devices visible without ever growing beyond MAX_SCAN_RESULTS.
    int8_t worst = findWeakestIdx();
    if (worst < 0 || rssi <= _results[worst].rssi) {
        // Table empty (unreachable here) or new device weaker than the
        // weakest existing one — ignore it.
        portEXIT_CRITICAL(&_resultsMux);
        return;
    }
    ScanResult &w = _results[worst];
    w.type = type;
    strlcpy(w.mac, mac, sizeof(w.mac));
    strlcpy(w.name, name ? name : "", sizeof(w.name));
    w.rssi = rssi;
    w.isMatched = isMatched;
    w.saved = false;
    w.active = false;
    w.lastSeen = now;
    portEXIT_CRITICAL(&_resultsMux);
}

void scanResultsClear() {
    portENTER_CRITICAL(&_resultsMux);
    _count = 0;
    _scanning = true;
    _scanComplete = false;
    memset(_results, 0, sizeof(_results));
    portEXIT_CRITICAL(&_resultsMux);
}

const ScanResult* scanResultsGet(uint8_t &count) {
    portENTER_CRITICAL(&_resultsMux);
    count = _count;
    portEXIT_CRITICAL(&_resultsMux);
    return _results;
}

/// Copy a sorted snapshot of the scan results.  Sorted by RSSI descending
/// (strongest first), with stale entries dropped.  Stable: ties preserve
/// insertion order.  Caller-allocated buffer; bounded by `outMax`.
///
/// The snapshot is taken under the table lock and lives in a module-static
/// buffer, because the returned pointers are consumed from loop() while the
/// NimBLE host task keeps calling scanResultsAdd().  Returning pointers into
/// _results itself would expose the caller to a torn strlcpy() of the name.
/// Only one snapshot exists: all callers run on the loop() task.
uint8_t scanResultsGetSortedByRssi(ScanResult const **out, uint8_t outMax) {
    if (!out || outMax == 0) return 0;

    static ScanResult snapshot[MAX_SCAN_RESULTS];
    uint8_t count;

    portENTER_CRITICAL(&_resultsMux);
    evictStale();
    count = (_count < MAX_SCAN_RESULTS) ? _count : MAX_SCAN_RESULTS;
    memcpy(snapshot, _results, count * sizeof(ScanResult));
    portEXIT_CRITICAL(&_resultsMux);

    // Build a parallel index array and sort it by RSSI desc.
    uint8_t n = (count < outMax) ? count : outMax;
    uint8_t idx[MAX_SCAN_RESULTS];
    for (uint8_t i = 0; i < count; i++) idx[i] = i;
    // Insertion sort — n is small (<=10) so this is fast and stack-only.
    for (uint8_t i = 1; i < count; i++) {
        uint8_t key = idx[i];
        int8_t  krssi = snapshot[key].rssi;
        uint8_t j = i;
        while (j > 0 && snapshot[idx[j - 1]].rssi < krssi) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = key;
    }
    for (uint8_t i = 0; i < n; i++) out[i] = &snapshot[idx[i]];
    return n;
}

void scanResultsMarkComplete() {
    portENTER_CRITICAL(&_resultsMux);
    _scanning = false;
    _scanComplete = true;
    portEXIT_CRITICAL(&_resultsMux);
}

bool scanResultsIsScanning() {
    return _scanning;
}

void scanResultsUpdateSavedStatus() {
    OpenSlateSettings &s = settingsGet();

    // Find active camera index
    int8_t activeIdx = -1;
    for (uint8_t i = 0; i < s.camCount; i++) {
        if (s.cams[i].active) {
            activeIdx = i;
            break;
        }
    }

    portENTER_CRITICAL(&_resultsMux);
    for (uint8_t i = 0; i < _count; i++) {
        _results[i].saved = false;
        _results[i].active = false;

        for (uint8_t j = 0; j < s.camCount; j++) {
            if (s.cams[j].type == _results[i].type &&
                macEquals(s.cams[j].mac, _results[i].mac)) {
                _results[i].saved = true;
                if ((int8_t)j == activeIdx) {
                    _results[i].active = true;
                }
                break;
            }
        }
    }
    portEXIT_CRITICAL(&_resultsMux);
}

// ──────────────────────────────────────────────────────────────────────────────
// "Show All" mode API
// ──────────────────────────────────────────────────────────────────────────────

void scanResultsSetShowAll(bool enabled) {
    _showAll = enabled;
}

bool scanResultsIsShowAll() {
    return _showAll;
}

/// Evict stale entries (lastSeen > SCAN_RESULT_TTL_MS ago).
/// Call from main loop() before webUpdate().
void scanResultsEvictStale() {
    portENTER_CRITICAL(&_resultsMux);
    evictStale();
    portEXIT_CRITICAL(&_resultsMux);
}