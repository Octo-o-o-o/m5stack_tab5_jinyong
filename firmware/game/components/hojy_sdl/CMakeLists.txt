# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later

idf_component_register(
    SRCS
        "sdl_misc.c"
        "sdl_video.c"
        "sdl_events.c"
        "sdl_audio.c"
        "sdl_gfx.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        tab5_platform
        esp_timer
        esp_psram
)

# This component is the software rasterizer. Pixel loops only.
target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
