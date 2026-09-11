#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRINGUP_TAG "jinyong_bringup"

/* Official Tab5 Keyboard: Ext.Port1, not the internal G31/G32 bus. */
#define BRINGUP_KB_I2C_ADDR 0x6D
#define BRINGUP_KB_SDA      0
#define BRINGUP_KB_SCL      1
#define BRINGUP_KB_INT      50

void bringup_log_memory(const char *stage);

void bringup_probe_panel(void);

esp_err_t bringup_display_start(void);
void bringup_display_draw_pattern(void);

esp_err_t bringup_keyboard_start(void);
void bringup_keyboard_poll(void);

esp_err_t bringup_sd_start(void);

esp_err_t bringup_audio_start(void);
void bringup_audio_beep_or_stub(void);

#ifdef __cplusplus
}
#endif
