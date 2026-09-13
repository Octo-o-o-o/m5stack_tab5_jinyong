/*
 * SPDX-FileCopyrightText: 2021 Soar Qin <soarchin@gmail.com>
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "application.hh"

#include "renderer.hh"
#include "esp_log.h"
#include "tab5_platform.h"

#include <algorithm>
#include <limits>

namespace hojy::app {

Application::Application(int width, int height, double animationSpeed):
    window_(width, height),
    scheduler_(FixedTickMicros, CompatibilityDivisor),
    compatibilityScheduler_(60.0, LegacyLogicRateHz * std::max(0.0, animationSpeed)) {
    simulationTime_ = window_.currTime();
    lastWallTime_ = wallTimeMicros();
    window_.setSimulationTime(simulationTime_);
    ESP_LOGI(TAB5_TAG, "Application constructed ready=%d %dx%d",
             (int)window_.ready(), width, height);
}

std::uint64_t Application::wallTimeMicros() {
    /* Same clock as SDL_GetPerformanceCounter / input event timestamps. */
    return tab5_clock_us();
}

int Application::run() {
    if (!window_.ready()) {
        ESP_LOGE(TAB5_TAG, "Window not ready (palette/EFT load failed)");
        return 1;
    }
    ESP_LOGI(TAB5_TAG, "entering game loop");
    running_ = true;
    while (running_ && !window_.quitRequested()) {
        inputCollector_.collect(inputQueue_);

        const auto now = wallTimeMicros();
        const auto elapsed = now >= lastWallTime_ ? now - lastWallTime_ : 0;
        lastWallTime_ = now;
        const auto batch = scheduler_.advance(elapsed);

        auto deliverInput = [this, now]() {
            /* Drain by wall clock, not simulationTime_. Slow present
             * makes sim lag; keys would otherwise sit in the queue forever. */
            for (const auto &event : inputQueue_.drainThrough(now)) {
                window_.dispatchInput(event);
            }
        };
        if (batch.fixedTicks == 0) {
            deliverInput();
        }

        for (std::uint32_t tick = 0; tick < batch.fixedTicks; ++tick) {
            if (simulationTime_ > std::numeric_limits<std::uint64_t>::max() - FixedTickMicros) {
                simulationTime_ = std::numeric_limits<std::uint64_t>::max();
            } else {
                simulationTime_ += FixedTickMicros;
            }
            window_.setSimulationTime(simulationTime_);
            deliverInput();
            window_.updateFixed();
            const auto compatibilityTicks = compatibilityScheduler_.advance();
            for (std::uint32_t compatibilityTick = 0;
                 compatibilityTick < compatibilityTicks; ++compatibilityTick) {
                window_.compatibilityUpdate();
            }
            if (window_.quitRequested()) { break; }
        }

        /*
         * Decide BEFORE rendering. Rendering a frame that limit_fps will then
         * refuse to present is pure waste, and when a render costs slightly
         * less than the frame interval it happens on roughly every third loop.
         * Window::flush() no longer re-asks, so canRender() advances the
         * schedule exactly once per loop.
         */
        auto *renderer = window_.renderer();
        if (renderer != nullptr && !renderer->canRender()) {
            const auto next = renderer->nextRenderTime();
            const auto idle = wallTimeMicros();
            if (next > idle) {
                const auto ms = (next - idle) / 1000ULL;
                tab5_delay_ms(static_cast<std::uint32_t>(ms > 1000ULL ? 1000ULL : ms));
            }
            continue;
        }

        const auto renderStart = wallTimeMicros();
        window_.render();
        tab5_prof_add(TAB5_PROF_RENDER, wallTimeMicros() - renderStart);
        if (!window_.flush()) {
            continue;
        }
    }
    return 0;
}

void Application::stop() {
    running_ = false;
    window_.requestQuit();
}

}
