// ============================================================================
// cpu_stats.cpp — CPU utilisation sampling via FreeRTOS runtime stats
// ============================================================================
// See cpu_stats.h for the rationale.
//
// Idle task counters are read through the per-core "ForCore" API because
// TaskStatus_t carries no xCoreID in this configuration
// (configTASKLIST_INCLUDE_COREID is not defined), so attribute-by-core through
// uxTaskGetSystemState() alone is not possible.
// ============================================================================

#include "cpu_stats.h"
#include "config.h"   // CPU_SAMPLE_INTERVAL_MS, DBG

#include <Arduino.h>          // millis()
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Both targets expose this unconditionally (NUMBER_OF_CORES 1 on the C3,
// 2 on the classic ESP32); the header guard in idf_additions.h requires
// configGENERATE_RUN_TIME_STATS == 1, which is set on both.
#ifndef configNUMBER_OF_CORES
#define configNUMBER_OF_CORES 1
#endif

static const UBaseType_t kMaxTasks  = 24;   // comfortably above uxTaskGetNumberOfTasks()
static const uint8_t     kMaxCores  = 2;

static CpuStats _stats;

static bool          _haveBaseline = false;
static uint32_t      _prevTotal    = 0;
static uint32_t      _prevIdle[kMaxCores] = {0, 0};
static uint32_t      _prevLoop     = 0;
static TaskHandle_t  _loopHandle   = nullptr;
static uint32_t      _lastSampleMs = 0;

// uxTaskGetSystemState() needs a caller-provided array. Static so it never
// touches the heap and never inflates the loop task's stack by ~1 KB.
static TaskStatus_t _taskSnap[kMaxTasks];

static uint8_t busyFromDelta(uint32_t total, uint32_t idle) {
    if (total == 0) return 0;
    if (idle >= total) return 0;          // clock skew / wrap edge — report idle
    uint32_t busy = total - idle;
    uint32_t pct  = (busy * 100U) / total;
    return (uint8_t)(pct > 100U ? 100U : pct);
}

void cpuStatsInit() {
    // Called from camInit(), i.e. from loop()'s task — so this handle is the
    // Arduino loop task without having to match its name.
    _loopHandle   = xTaskGetCurrentTaskHandle();
    _haveBaseline = false;
    _lastSampleMs = 0;
    _stats.total  = 0;
    _stats.loopTask = 0;
    _stats.cores  = (uint8_t)configNUMBER_OF_CORES;
    for (uint8_t c = 0; c < kMaxCores; c++) {
        _stats.core[c] = 0;
        _prevIdle[c]   = 0;
    }
}

static uint32_t loopTaskCounter() {
    if (!_loopHandle) return 0;
    uint32_t n = uxTaskGetSystemState(_taskSnap, kMaxTasks, nullptr);
    for (uint32_t i = 0; i < n; i++) {
        if (_taskSnap[i].xHandle == _loopHandle) return (uint32_t)_taskSnap[i].ulRunTimeCounter;
    }
    return 0;
}

void cpuStatsUpdate() {
    uint32_t now = millis();
    if (_haveBaseline && (uint32_t)(now - _lastSampleMs) < CPU_SAMPLE_INTERVAL_MS) return;

    uint32_t total = (uint32_t)portGET_RUN_TIME_COUNTER_VALUE();
    uint32_t idle[kMaxCores] = {0, 0};
    uint8_t  cores = (uint8_t)configNUMBER_OF_CORES;
    for (uint8_t c = 0; c < cores && c < kMaxCores; c++) {
        idle[c] = (uint32_t)ulTaskGetIdleRunTimeCounterForCore((BaseType_t)c);
    }
    uint32_t loopCnt = loopTaskCounter();

    _lastSampleMs = now;

    if (!_haveBaseline) {
        // First sample only latches the baseline — percentages need a delta.
        _haveBaseline = true;
        _prevTotal = total;
        _prevLoop  = loopCnt;
        for (uint8_t c = 0; c < kMaxCores; c++) _prevIdle[c] = idle[c];
        return;
    }

    // Unsigned deltas: the counter is 32-bit microseconds and wraps every
    // ~71.58 min. Delta arithmetic across the wrap stays correct.
    uint32_t dTotal = total - _prevTotal;
    _prevTotal = total;

    uint8_t maxBusy = 0;
    for (uint8_t c = 0; c < kMaxCores; c++) {
        if (c < cores) {
            uint8_t busy = busyFromDelta(dTotal, idle[c] - _prevIdle[c]);
            _stats.core[c] = busy;
            if (busy > maxBusy) maxBusy = busy;
        } else {
            _stats.core[c] = 0;
        }
        _prevIdle[c] = idle[c];
    }
    _stats.total = maxBusy;
    _stats.cores = cores;

    uint32_t dLoop = loopCnt - _prevLoop;
    _prevLoop = loopCnt;
    _stats.loopTask = busyFromDelta(dTotal, dTotal > dLoop ? dTotal - dLoop : 0);

    _stats.sampledAt = now;
}

const CpuStats &cpuStatsGet() { return _stats; }
