#include "tab5_platform.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint64_t tab5_clock_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

void tab5_delay_ms(uint32_t ms)
{
    if (ms == 0) {
        vTaskDelay(1);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(ms));
}
