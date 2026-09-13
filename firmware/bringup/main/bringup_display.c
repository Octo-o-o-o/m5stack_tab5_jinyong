/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "bringup.h"

#include "bsp/display.h"
#include "bsp/m5stack_tab5.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include <string.h>

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))

static esp_lcd_panel_handle_t s_panel;
static int s_width;
static int s_height;

static void fill_rect_rgb565(uint16_t *fb, int stride, int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > s_width) {
        w = s_width - x;
    }
    if (y + h > s_height) {
        h = s_height - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int row = 0; row < h; ++row) {
        uint16_t *dst = fb + (y + row) * stride + x;
        for (int col = 0; col < w; ++col) {
            dst[col] = color;
        }
    }
}

static void draw_into_fb(uint16_t *fb, int stride)
{
    /* Color bars */
    const uint16_t bars[] = {
        RGB565(255, 0, 0),
        RGB565(255, 255, 0),
        RGB565(0, 255, 0),
        RGB565(0, 255, 255),
        RGB565(0, 0, 255),
        RGB565(255, 0, 255),
        RGB565(255, 255, 255),
        RGB565(32, 32, 32),
    };
    const int bar_h = s_height / 10;
    for (int i = 0; i < 8; ++i) {
        fill_rect_rgb565(fb, stride, 0, i * bar_h, s_width, bar_h, bars[i]);
    }

    /* Checkerboard in the lower band */
    const int cell = 32;
    const int y0 = bar_h * 8;
    for (int y = y0; y < s_height; y += cell) {
        for (int x = 0; x < s_width; x += cell) {
            const int on = ((x / cell) + (y / cell)) & 1;
            fill_rect_rgb565(fb, stride, x, y, cell, cell,
                             on ? RGB565(200, 200, 200) : RGB565(40, 40, 40));
        }
    }

    /* 4:3 game box: 640x480, nearest-integer, centered. Black letterbox around it
     * is implicit if the panel is 16:9. Do not stretch. */
    const int gw = 640;
    const int gh = 480;
    const int gx = (s_width - gw) / 2;
    const int gy = (s_height - gh) / 2;
    fill_rect_rgb565(fb, stride, gx, gy, gw, gh, RGB565(0, 0, 0));
    fill_rect_rgb565(fb, stride, gx + 8, gy + 8, gw - 16, gh - 16, RGB565(16, 48, 96));
    fill_rect_rgb565(fb, stride, gx, gy, gw, 4, RGB565(255, 220, 0));
    fill_rect_rgb565(fb, stride, gx, gy + gh - 4, gw, 4, RGB565(255, 220, 0));
    fill_rect_rgb565(fb, stride, gx, gy, 4, gh, RGB565(255, 220, 0));
    fill_rect_rgb565(fb, stride, gx + gw - 4, gy, 4, gh, RGB565(255, 220, 0));
}

esp_err_t bringup_display_start(void)
{
    s_width = BSP_LCD_H_RES;
    s_height = BSP_LCD_V_RES;

    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_feature_enable(BSP_FEATURE_LCD, true));

    bsp_display_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.dsi_bus.lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS;

    bsp_lcd_handles_t handles;
    memset(&handles, 0, sizeof(handles));

    esp_err_t err = bsp_display_new_with_handles(&cfg, &handles);
    if (err != ESP_OK) {
        ESP_LOGE(BRINGUP_TAG, "bsp_display_new_with_handles failed: %s", esp_err_to_name(err));
        return err;
    }

    s_panel = handles.panel;
    if (s_panel == NULL) {
        ESP_LOGE(BRINGUP_TAG, "BSP returned a null panel handle");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_disp_on_off(s_panel, true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_brightness_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_backlight_on());
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_brightness_set(70));

    ESP_LOGI(BRINGUP_TAG, "display started via official BSP (%dx%d RGB565)", s_width, s_height);
    return ESP_OK;
}

void bringup_display_draw_pattern(void)
{
    if (s_panel == NULL) {
        ESP_LOGW(BRINGUP_TAG, "skip pattern: no panel");
        return;
    }

    void *fb0 = NULL;
    const esp_err_t fb_err = esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &fb0);
    if (fb_err == ESP_OK && fb0 != NULL) {
        draw_into_fb((uint16_t *)fb0, s_width);
        ESP_LOGI(BRINGUP_TAG, "pattern written into DPI framebuffer %p (no extra 720p alloc)", fb0);
        return;
    }

    ESP_LOGW(BRINGUP_TAG, "DPI fb get failed (%s); drawing in RGB565 strips",
             esp_err_to_name(fb_err));

    const int strip_h = 16;
    const size_t bytes = (size_t)s_width * (size_t)strip_h * sizeof(uint16_t);
    uint16_t *strip = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (strip == NULL) {
        strip = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    }
    if (strip == NULL) {
        ESP_LOGE(BRINGUP_TAG, "cannot allocate %u-byte strip", (unsigned)bytes);
        return;
    }

    for (int y = 0; y < s_height; y += strip_h) {
        const int h = (y + strip_h <= s_height) ? strip_h : (s_height - y);
        /* Cheap strip pattern so we still have a visible test if DPI fb is hidden. */
        const uint16_t color = ((y / strip_h) & 1) ? RGB565(0, 80, 160) : RGB565(160, 80, 0);
        for (int i = 0; i < s_width * h; ++i) {
            strip[i] = color;
        }
        const esp_err_t draw = esp_lcd_panel_draw_bitmap(s_panel, 0, y, s_width, y + h, strip);
        if (draw != ESP_OK) {
            ESP_LOGE(BRINGUP_TAG, "draw_bitmap y=%d failed: %s", y, esp_err_to_name(draw));
            break;
        }
    }
    heap_caps_free(strip);
}
