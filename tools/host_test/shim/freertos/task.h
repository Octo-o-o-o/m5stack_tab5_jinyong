#pragma once
#include "freertos/FreeRTOS.h"

typedef std::thread *TaskHandle_t;
inline void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
inline void vTaskDelete(void *) {}
inline BaseType_t xTaskCreate(void (*fn)(void *), const char *, std::uint32_t,
                              void *arg, UBaseType_t, TaskHandle_t *out) {
    auto *t = new std::thread(fn, arg);
    t->detach();
    if (out) { *out = t; }
    return pdPASS;
}
