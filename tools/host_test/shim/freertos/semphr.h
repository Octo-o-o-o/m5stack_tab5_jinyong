#pragma once
#include "freertos/FreeRTOS.h"

struct HostSem {
    std::mutex m;
    std::condition_variable cv;
    int count = 0;
    int max = 1;
};
typedef HostSem *SemaphoreHandle_t;

inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { auto *s = new HostSem; s->count = 1; return s; }
inline SemaphoreHandle_t xSemaphoreCreateBinary(void) { auto *s = new HostSem; s->count = 0; return s; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks) {
    std::unique_lock<std::mutex> lk(s->m);
    if (ticks == portMAX_DELAY) { s->cv.wait(lk, [s] { return s->count > 0; }); }
    else if (!s->cv.wait_for(lk, std::chrono::milliseconds(ticks), [s] { return s->count > 0; })) { return pdFALSE; }
    --s->count; return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    std::lock_guard<std::mutex> lk(s->m);
    if (s->count < s->max) { ++s->count; }
    s->cv.notify_one(); return pdTRUE;
}
