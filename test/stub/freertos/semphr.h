#ifndef SEMPHR_STUB_H
#define SEMPHR_STUB_H
typedef void* SemaphoreHandle_t;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static int token; return &token; }
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) { return 1; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
#endif
