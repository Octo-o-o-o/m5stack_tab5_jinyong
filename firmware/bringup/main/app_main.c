#include "bringup.h"

#include "bsp/m5stack_tab5.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include <stdio.h>

static void log_boot(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_bytes = 0;
    if (esp_flash_get_size(NULL, &flash_bytes) != ESP_OK) {
        flash_bytes = 0;
    }

    ESP_LOGI(BRINGUP_TAG, "tab5_jinyong bring-up");
    ESP_LOGI(BRINGUP_TAG, "IDF %s  chip=%s cores=%d rev=%d  flash=%u MB",
             IDF_VER,
             CONFIG_IDF_TARGET,
             chip.cores,
             chip.revision,
             (unsigned)(flash_bytes / (1024 * 1024)));
    ESP_LOGI(BRINGUP_TAG, "reset reason %d", (int)esp_reset_reason());
}

void app_main(void)
{
    log_boot();
    bringup_log_memory("boot");

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs);

    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_i2c_init());
    bringup_probe_panel();
    bringup_log_memory("after_i2c");

    if (bringup_display_start() == ESP_OK) {
        bringup_display_draw_pattern();
    } else {
        ESP_LOGE(BRINGUP_TAG, "display init failed; continuing (keyboard/SD still useful)");
    }
    bringup_log_memory("after_display");

    ESP_ERROR_CHECK_WITHOUT_ABORT(bringup_keyboard_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(bringup_sd_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(bringup_audio_start());
    bringup_audio_beep_or_stub();
    bringup_log_memory("idle");

    ESP_LOGI(BRINGUP_TAG, "enter poll loop; press keyboard keys (I2C 0x6D)");

    while (true) {
        bringup_keyboard_poll();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
