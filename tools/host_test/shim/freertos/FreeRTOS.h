#pragma once
/* Minimal host stand-in for the FreeRTOS surface the port's overlays use. */
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef std::uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
