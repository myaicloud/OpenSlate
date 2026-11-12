// ============================================================================
// cpu_stats.h — CPU utilisation sampling via FreeRTOS runtime stats
// ============================================================================
// Both targets already compile and link FreeRTOS runtime statistics
// (configGENERATE_RUN_TIME_STATS=1, counter = esp_timer_get_time() truncated to
// uint32_t), so no sdkconfig or platformio.ini change is needed.
//
// "CPU" here means: 100 % minus the share of time the idle task ran on the
// core(s), i.e. the busy fraction.  On the single-core C3 that is the whole
// picture.  On the classic ESP32 (dual core) it is reported per core plus the
// maximum, because NimBLE runs pinned to its own core there and a per-core
// figure is the only one that tells you which side saturated.
//
// The counter is 32-bit microseconds and therefore wraps every ~71.58 minutes.
// All arithmetic is unsigned delta arithmetic across that wrap, so the value
// stays correct; absolute counter reads must never be compared.
// ============================================================================

#ifndef CPU_STATS_H
#define CPU_STATS_H

#include <stdint.h>

struct CpuStats {
    uint8_t  total;      // 0..100 busy, max over cores (single core ⇒ that core)
    uint8_t  core[2];    // per-core busy %, index >= core count is 0
    uint8_t  loopTask;   // 0..100 share of the Arduino loop task (max core)
    uint8_t  cores;      // number of populated entries in core[]
    uint32_t sampledAt;  // millis() of the last sample
};

/// Reset counters. Call once from camInit(); the first sample only latches
/// baselines, so percentages stay 0 until the second sample.
void cpuStatsInit();

/// Take a sample if CPU_SAMPLE_INTERVAL_MS has elapsed. Cheap to call every
/// loop() tick. Does a small fixed-array uxTaskGetSystemState(), so it is
/// deliberately throttled rather than run per tick.
void cpuStatsUpdate();

/// Last sampled values. Never blocks.
const CpuStats &cpuStatsGet();

#endif // CPU_STATS_H
