/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_TAG "jinyong"

#define TAB5_KB_I2C_ADDR 0x6D
#define TAB5_KB_SDA      0
#define TAB5_KB_SCL      1
#define TAB5_KB_INT      50

#define TAB5_SD_GAME_ROOT "/sdcard/jinyong"

#define TAB5_RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))

typedef struct {
    uint8_t modifier;
    uint8_t keycode;
    bool pressed;
} tab5_hid_event_t;

void tab5_log_memory(const char *stage);
/* Free PSRAM and the largest single block available, in bytes. Either pointer
 * may be NULL. Used so an out-of-memory message can carry the actual numbers
 * instead of sending the user to hunt for a serial cable. */
void tab5_mem_psram(size_t *free_bytes, size_t *largest_block);

void tab5_probe_panel(void);

esp_err_t tab5_video_start(void);
/* Present an ARGB8888 (HOJY 0xAARRGGBB) buffer with nearest + letterbox.
 * Rotates 90° so a landscape 640×480 game is upright on the 720×1280 panel. */
void tab5_video_present_argb(const uint32_t *src, int width, int height);
void tab5_video_fill_rgb565(uint16_t color);
void tab5_video_draw_text(int x, int y, const char *text, uint16_t color);

esp_err_t tab5_input_start(void);
/* Drain I2C HID packets. Returns number of events written (max cap). */
int tab5_input_poll(tab5_hid_event_t *out, int cap);

esp_err_t tab5_fs_start(void);
bool tab5_fs_has_game(void);
esp_err_t tab5_fs_chdir_game(void);

esp_err_t tab5_audio_start(void);
bool tab5_audio_ready(void);
esp_err_t tab5_audio_open(int sample_rate, int channels);
esp_err_t tab5_audio_write(const int16_t *pcm, size_t bytes);

uint64_t tab5_clock_us(void);
void tab5_delay_ms(uint32_t ms);

/* Frame-phase profiler. Cheap accumulators; one UART line every few seconds.
 * Exists so "it feels slow" can be replaced with numbers before tuning. */
typedef enum {
    TAB5_PROF_RENDER = 0,
    TAB5_PROF_BLIT,
    TAB5_PROF_VSYNC,
    TAB5_PROF_COUNT
} tab5_prof_slot_t;

void tab5_prof_add(tab5_prof_slot_t slot, uint64_t us);
void tab5_prof_end_frame(void);

#ifdef __cplusplus
}
#endif
