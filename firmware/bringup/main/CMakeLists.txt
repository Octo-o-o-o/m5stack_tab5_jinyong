# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later

idf_component_register(
    SRCS
        "app_main.c"
        "bringup_mem.c"
        "bringup_probe.c"
        "bringup_display.c"
        "bringup_keyboard.c"
        "bringup_sd.c"
        "bringup_audio.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        fatfs
        esp_lcd
        esp_timer
        esp_psram
        nvs_flash
        spi_flash
        esp_hw_support
        espressif__m5stack_tab5_noglib
        espressif__esp_codec_dev
)
