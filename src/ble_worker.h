// ============================================================================
// ble_worker.h — BLE connect offload task (P02)
// ============================================================================
// NimBLEClient::connect() is synchronous: it arms a task notification and then
// waits in ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(m_connectTimeout + 1000)).
// NimBLE-Arduino 1.4.1 exposes no async/begin-connect variant, so the only way
// to keep loop() responsive is to run the call on another task.
//
// That matters because a failed connect blocked loop() for ~7.5 s of a 10 s
// budget, which starved MSP polling and — with BLE and WiFi sharing the single
// 2.4 GHz radio — was enough to break the SoftAP's WPA2 4-way handshake
// (Windows logged event 11006 "wireless security error"), so the UI was
// unreachable exactly when the user wanted it.
//
// Connect and GATT discovery run on the worker. Client creation stays on loop()
// while the worker is idle; releases cancel only the owning slot's job.
// Pairing approval does not occupy the worker. Both slots share one job at a
// time because the controller can initiate only one connection at a time.
//
// The task is created once and parks on a semaphore, so a connect attempt
// costs no task churn. Stack headroom covers GATT discovery on top of the
// NimBLE call frames.
// ============================================================================

#ifndef BLE_WORKER_H
#define BLE_WORKER_H

#include <stdint.h>

/// Function executed on the worker task. Return value is the job result.
typedef bool (*BleWorkFn)(void *ctx);

/// Create the worker task. Call once from camInit(); never blocks.
void bleWorkerInit();

/// Queue `fn` for execution on the worker task.
/// @return false when a job is already queued or running — the caller keeps
///         its pending state and retries on the next update tick.
bool bleWorkerSubmit(BleWorkFn fn, void *ctx);

/// True while a job is queued or running.
bool bleWorkerBusy();

/// Wait (bounded) until the worker is idle. ONLY for the explicit user-driven
/// release/switch paths, so a client cannot be freed underneath a connect that
/// is still inside NimBLE. Never call it from a periodic path.
/// @return true when idle before the timeout expired.
bool bleWorkerWaitIdle(uint32_t timeoutMs);

#endif // BLE_WORKER_H
