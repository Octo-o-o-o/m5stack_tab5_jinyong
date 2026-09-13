/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "bringup.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

void bringup_log_memory(const char *stage)
{
    const size_t intern_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t intern_large = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t psram_large = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI(BRINGUP_TAG,
             "mem[%s] internal free=%u largest=%u  psram free=%u largest=%u",
             stage,
             (unsigned)intern_free,
             (unsigned)intern_large,
             (unsigned)psram_free,
             (unsigned)psram_large);
}
