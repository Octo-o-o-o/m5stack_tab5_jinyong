/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Frame-phase profiler. One UART line every TAB5_PROF_PERIOD_US so that
 * "it feels slow" can be replaced with numbers before anything else is tuned.
 * Cost per frame is a handful of adds; the log itself is one line per period.
 */

#include "tab5_platform.h"

#include "esp_log.h"

#define TAB5_PROF_PERIOD_US 5000000ULL

static uint64_t s_acc[TAB5_PROF_COUNT];
static uint32_t s_frames;
static uint64_t s_window_start;

void tab5_prof_add(tab5_prof_slot_t slot, uint64_t us)
{
    if ((unsigned)slot >= (unsigned)TAB5_PROF_COUNT) {
        return;
    }
    s_acc[slot] += us;
}

void tab5_prof_end_frame(void)
{
    ++s_frames;
    const uint64_t now = tab5_clock_us();
    if (s_window_start == 0) {
        s_window_start = now;
        return;
    }
    const uint64_t span = now - s_window_start;
    if (span < TAB5_PROF_PERIOD_US) {
        return;
    }
    const uint32_t n = s_frames ? s_frames : 1;
    ESP_LOGI(TAB5_TAG,
             "perf fps=%u.%u render=%u.%ums blit=%u.%ums vsync=%u.%ums frames=%u",
             (unsigned)(s_frames * 1000000ULL / span),
             (unsigned)((s_frames * 10000000ULL / span) % 10U),
             (unsigned)(s_acc[TAB5_PROF_RENDER] / n / 1000U),
             (unsigned)((s_acc[TAB5_PROF_RENDER] / n / 100U) % 10U),
             (unsigned)(s_acc[TAB5_PROF_BLIT] / n / 1000U),
             (unsigned)((s_acc[TAB5_PROF_BLIT] / n / 100U) % 10U),
             (unsigned)(s_acc[TAB5_PROF_VSYNC] / n / 1000U),
             (unsigned)((s_acc[TAB5_PROF_VSYNC] / n / 100U) % 10U),
             (unsigned)s_frames);
    for (int i = 0; i < TAB5_PROF_COUNT; ++i) {
        s_acc[i] = 0;
    }
    s_frames = 0;
    s_window_start = now;
}
