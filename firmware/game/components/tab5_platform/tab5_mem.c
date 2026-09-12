#include "tab5_platform.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

void tab5_log_memory(const char *stage)
{
    const size_t intern = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t largest_int = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t largest_spi = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAB5_TAG, "mem[%s] internal=%u (largest %u)  psram=%u (largest %u)",
             stage,
             (unsigned)intern, (unsigned)largest_int,
             (unsigned)spiram, (unsigned)largest_spi);
}

void tab5_mem_psram(size_t *free_bytes, size_t *largest_block)
{
    if (free_bytes != NULL) {
        *free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }
    if (largest_block != NULL) {
        *largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    }
}
