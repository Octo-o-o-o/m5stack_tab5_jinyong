# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later

idf_component_register(
    SRCS
        "app_main.cpp"
        "fallback_ui.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash
        esp_timer
        hojy_core
        tab5_platform
        espressif__m5stack_tab5_noglib
)
