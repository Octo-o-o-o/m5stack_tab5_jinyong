/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fallback_ui.h"
#include "tab5_platform.h"

#include "bsp/m5stack_tab5.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <cstdio>
#include <exception>

extern int main(int argc, char **argv);

[[noreturn]] static void halt()
{
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

static void game_task(void *)
{
    tab5_log_memory("before_hojy_main");
    char arg0[] = "hojy";
    char *argv[] = {arg0, nullptr};
    /*
     * Nothing may leave this task entry. An uncaught exception reaches
     * std::terminate, which on ESP-IDF is abort(): the panic handler prints to
     * a UART nobody has attached and reboots straight away, so the whole event
     * reads as "the device restarted by itself". Catch it and put the reason on
     * the panel instead -- out-of-memory in a menu handler looked exactly like
     * a hardware fault until it was read off a serial cable.
     */
    int rc = -1;
    static char reason[96];
    reason[0] = '\0';
    try {
        rc = main(1, argv);
    } catch (const std::exception &e) {
        std::snprintf(reason, sizeof(reason), "%s", e.what());
        ESP_LOGE(TAB5_TAG, "hojy main threw: %s", e.what());
    } catch (...) {
        std::snprintf(reason, sizeof(reason), "UNKNOWN EXCEPTION");
        ESP_LOGE(TAB5_TAG, "hojy main threw a non-standard exception");
    }
    if (reason[0] != '\0') {
        tab5_log_memory("hojy_main_threw");
        fallback_show("GAME STOPPED", reason, "POWER CYCLE TO RESTART");
        halt();
    }
    ESP_LOGE(TAB5_TAG, "hojy main returned %d", rc);
    fallback_show("HOJY MAIN EXITED", "CHECK UART FOR MISSING FILES", "INSERT PREPARED /JINYONG");
    halt();
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAB5_TAG, "tab5_jinyong game firmware");
    tab5_log_memory("boot");

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs);

    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_i2c_init());
    tab5_probe_panel();
    tab5_log_memory("after_i2c");

    if (tab5_video_start() != ESP_OK) {
        ESP_LOGE(TAB5_TAG, "display init failed");
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(tab5_input_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(tab5_fs_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(tab5_audio_start());
    tab5_log_memory("after_hw");

    if (!tab5_fs_has_game()) {
        ESP_LOGW(TAB5_TAG, "no prepared game on SD at %s", TAB5_SD_GAME_ROOT);
        fallback_show("NO GAME DATA ON SD", "COPY PREPARED TREE TO /JINYONG",
                      "THEN REBOOT");
        /* Keep logging keys so the keyboard can still be checked without a card. */
        for (;;) {
            tab5_hid_event_t ev[4];
            const int n = tab5_input_poll(ev, 4);
            for (int i = 0; i < n; ++i) {
                ESP_LOGI(TAB5_TAG, "kb I2C-HID mod=0x%02x key=0x%02x %s",
                         ev[i].modifier, ev[i].keycode,
                         ev[i].pressed ? "press" : "release");
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    if (tab5_fs_chdir_game() != ESP_OK) {
        fallback_show("CHDIR /JINYONG FAILED", nullptr, nullptr);
        halt();
    }

    /* ESP-IDF xTaskCreate stack depth is in bytes. */
    const BaseType_t ok = xTaskCreate(game_task, "hojy", 64 * 1024, nullptr, 4, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(TAB5_TAG, "cannot create game task");
        fallback_show("GAME TASK CREATE FAILED", "NOT ENOUGH RAM?", nullptr);
    }
}
