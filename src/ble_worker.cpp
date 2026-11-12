// ============================================================================
// ble_worker.cpp — BLE connect offload task (P02)
// ============================================================================
// See ble_worker.h for the rationale.
//
// Single job slot: submitting while busy is refused rather than queued, because
// a connect attempt that is already in flight must finish (its state is applied
// by the job itself) before a new one can be started. Refusal is what the
// backends' _doConnect retry loop expects.
// ============================================================================

#include "ble_worker.h"
#include "config.h"   // DBG

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// NimBLE's connect path plus service/characteristic discovery and, for GoPro,
// secureConnection() (which pairs and therefore does crypto). 8 KB is generous
// on both targets; the C3 has ~95 KB free heap.
static const uint32_t kWorkerStackBytes = 8192;
static const UBaseType_t kWorkerPriority = 6;   // above loop() (1), below host (>=10)

static TaskHandle_t     _task    = nullptr;
static SemaphoreHandle_t _idle   = nullptr;   // signalled when the task parks

static BleWorkFn _pendingFn  = nullptr;
static void     *_pendingCtx = nullptr;
static volatile bool _busy   = false;

static void workerLoop(void *) {
    for (;;) {
        // Park until a job arrives. Taking with portMAX_DELAY is fine here:
        // this is the worker's own task, not loop().
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        BleWorkFn fn  = _pendingFn;
        void     *ctx = _pendingCtx;

        if (fn) fn(ctx);

        // Publish idle BEFORE signalling so a waiter that wakes up can trust
        // the job (and everything it mutated) is complete.
        _pendingFn  = nullptr;
        _pendingCtx = nullptr;
        _busy       = false;
        if (_idle) xSemaphoreGive(_idle);
    }
}

void bleWorkerInit() {
    if (_task) return;
    if (!_idle) {
        _idle = xSemaphoreCreateBinary();
        configASSERT(_idle);
    }
    if (xTaskCreate(workerLoop, "bleWork", kWorkerStackBytes, nullptr,
                    kWorkerPriority, &_task) != pdPASS) {
        DBG("BLE: WARN — connect worker task creation failed");
        _task = nullptr;
    }
}

bool bleWorkerSubmit(BleWorkFn fn, void *ctx) {
    if (!fn) return false;
    if (!_task) return false;          // init failed — callers fall back to sync
    if (_busy) return false;           // one attempt in flight at a time

    _pendingFn  = fn;
    _pendingCtx = ctx;
    _busy       = true;
    xTaskNotifyGive(_task);
    return true;
}

bool bleWorkerBusy() { return _busy; }

bool bleWorkerWaitIdle(uint32_t timeoutMs) {
    if (!_task || !_busy) return true;
    if (!_idle) return false;
    // 0-timeout means "poll once" — never wait forever on a user action.
    if (timeoutMs == 0) return !_busy;
    xSemaphoreTake(_idle, pdMS_TO_TICKS(timeoutMs));
    return !_busy;
}
