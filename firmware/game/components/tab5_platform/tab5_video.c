/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tab5_platform.h"

#include "bsp/display.h"
#include "bsp/m5stack_tab5.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static esp_lcd_panel_handle_t s_panel;
static int s_width;
static int s_height;
static uint16_t *s_fbs[2];
static int s_fb_count;
static int s_draw_i;
static size_t s_fb_bytes;
static SemaphoreHandle_t s_scan_done;
/* A flip is in flight: the buffer we are about to draw into may still be on
 * the scanner. Waiting is deferred to just before the next frame is drawn so
 * the scan overlaps the game's logic + software render. */
static bool s_flip_pending;
/* ESP32-P4 Pixel Processing Accelerator. NULL => fall back to the CPU blit. */
static ppa_client_handle_t s_ppa;

/* 8x8 ASCII 0x20-0x5F (space through underscore). Bit0 is leftmost. */
static const uint8_t kFont[64][8] = {
    {0,0,0,0,0,0,0,0}, /* space */
    {0x10,0x10,0x10,0x10,0x10,0x00,0x10,0x00},
    {0x28,0x28,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x28,0x28,0x7C,0x28,0x7C,0x28,0x28,0x00},
    {0x10,0x3C,0x50,0x38,0x14,0x78,0x10,0x00},
    {0x60,0x64,0x08,0x10,0x20,0x4C,0x0C,0x00},
    {0x20,0x50,0x50,0x20,0x54,0x48,0x34,0x00},
    {0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x08,0x10,0x20,0x20,0x20,0x10,0x08,0x00},
    {0x20,0x10,0x08,0x08,0x08,0x10,0x20,0x00},
    {0x00,0x10,0x54,0x38,0x54,0x10,0x00,0x00},
    {0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x20},
    {0x00,0x00,0x00,0x7C,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00},
    {0x00,0x04,0x08,0x10,0x20,0x40,0x00,0x00},
    {0x38,0x44,0x4C,0x54,0x64,0x44,0x38,0x00}, /* 0 */
    {0x10,0x30,0x10,0x10,0x10,0x10,0x38,0x00},
    {0x38,0x44,0x04,0x18,0x20,0x40,0x7C,0x00},
    {0x38,0x44,0x04,0x18,0x04,0x44,0x38,0x00},
    {0x08,0x18,0x28,0x48,0x7C,0x08,0x08,0x00},
    {0x7C,0x40,0x78,0x04,0x04,0x44,0x38,0x00},
    {0x18,0x20,0x40,0x78,0x44,0x44,0x38,0x00},
    {0x7C,0x04,0x08,0x10,0x20,0x20,0x20,0x00},
    {0x38,0x44,0x44,0x38,0x44,0x44,0x38,0x00},
    {0x38,0x44,0x44,0x3C,0x04,0x08,0x30,0x00},
    {0x00,0x00,0x10,0x00,0x00,0x10,0x00,0x00},
    {0x00,0x00,0x10,0x00,0x00,0x10,0x10,0x20},
    {0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x00},
    {0x00,0x00,0x7C,0x00,0x7C,0x00,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x00},
    {0x38,0x44,0x04,0x08,0x10,0x00,0x10,0x00},
    {0x38,0x44,0x5C,0x54,0x5C,0x40,0x38,0x00},
    {0x38,0x44,0x44,0x7C,0x44,0x44,0x44,0x00}, /* A */
    {0x78,0x44,0x44,0x78,0x44,0x44,0x78,0x00},
    {0x38,0x44,0x40,0x40,0x40,0x44,0x38,0x00},
    {0x70,0x48,0x44,0x44,0x44,0x48,0x70,0x00},
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x7C,0x00},
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x40,0x00},
    {0x38,0x44,0x40,0x5C,0x44,0x44,0x38,0x00},
    {0x44,0x44,0x44,0x7C,0x44,0x44,0x44,0x00},
    {0x38,0x10,0x10,0x10,0x10,0x10,0x38,0x00},
    {0x1C,0x08,0x08,0x08,0x08,0x48,0x30,0x00},
    {0x44,0x48,0x50,0x60,0x50,0x48,0x44,0x00},
    {0x40,0x40,0x40,0x40,0x40,0x40,0x7C,0x00},
    {0x44,0x6C,0x54,0x54,0x44,0x44,0x44,0x00},
    {0x44,0x64,0x54,0x4C,0x44,0x44,0x44,0x00},
    {0x38,0x44,0x44,0x44,0x44,0x44,0x38,0x00},
    {0x78,0x44,0x44,0x78,0x40,0x40,0x40,0x00},
    {0x38,0x44,0x44,0x44,0x54,0x48,0x34,0x00},
    {0x78,0x44,0x44,0x78,0x50,0x48,0x44,0x00},
    {0x38,0x44,0x40,0x38,0x04,0x44,0x38,0x00},
    {0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0x00},
    {0x44,0x44,0x44,0x44,0x44,0x44,0x38,0x00},
    {0x44,0x44,0x44,0x44,0x44,0x28,0x10,0x00},
    {0x44,0x44,0x44,0x54,0x54,0x54,0x28,0x00},
    {0x44,0x44,0x28,0x10,0x28,0x44,0x44,0x00},
    {0x44,0x44,0x28,0x10,0x10,0x10,0x10,0x00},
    {0x7C,0x04,0x08,0x10,0x20,0x40,0x7C,0x00},
    {0x38,0x20,0x20,0x20,0x20,0x20,0x38,0x00},
    {0x00,0x40,0x20,0x10,0x08,0x04,0x00,0x00},
    {0x38,0x08,0x08,0x08,0x08,0x08,0x38,0x00},
    {0x10,0x28,0x44,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7C},
};

static void grab_fbs(void)
{
    if (s_fb_count > 0 || s_panel == NULL) {
        return;
    }
    void *fb0 = NULL;
    void *fb1 = NULL;
    s_fb_bytes = (size_t)s_width * (size_t)s_height * sizeof(uint16_t);
    if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1) == ESP_OK
        && fb0 != NULL && fb1 != NULL) {
        s_fbs[0] = (uint16_t *)fb0;
        s_fbs[1] = (uint16_t *)fb1;
        s_fb_count = 2;
        /* DMA starts on fb0. Game presents write fb1 first, then flip. */
        s_draw_i = 1;
        ESP_LOGI(TAB5_TAG, "dpi double fb %p %p", fb0, fb1);
        return;
    }
    if (esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &fb0) == ESP_OK && fb0 != NULL) {
        s_fbs[0] = (uint16_t *)fb0;
        s_fb_count = 1;
        s_draw_i = 0;
        ESP_LOGW(TAB5_TAG, "dpi single fb %p (tear on present)", fb0);
    }
}

static uint16_t *fb_ptr(void)
{
    grab_fbs();
    if (s_fb_count <= 0) {
        return NULL;
    }
    return s_fbs[s_draw_i];
}

/*
 * The DPI scanner and the PPA both read the frame buffer by DMA. Anything the
 * CPU writes there sits in the write-back cache until it happens to be evicted.
 * Flush explicitly instead of relying on a big fill to push it out.
 */
static void fb_writeback(uint16_t *fb)
{
    if (fb == NULL || s_fb_bytes == 0) {
        return;
    }
    (void)esp_cache_msync(fb, s_fb_bytes,
                          ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

static void fill_one_rgb565(uint16_t *fb, uint16_t color)
{
    if (fb == NULL) {
        return;
    }
    const size_t n = (size_t)s_width * (size_t)s_height;
    const uint32_t pair = ((uint32_t)color << 16) | (uint32_t)color;
    uint32_t *p = (uint32_t *)fb;
    for (size_t i = 0; i < n / 2; ++i) {
        p[i] = pair;
    }
    if (n & 1u) {
        fb[n - 1] = color;
    }
    fb_writeback(fb);
}

static bool IRAM_ATTR on_fb_complete(esp_lcd_panel_handle_t panel,
                                    esp_lcd_dpi_panel_event_data_t *edata,
                                    void *ctx)
{
    (void)panel;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    if (s_scan_done != NULL) {
        xSemaphoreGiveFromISR(s_scan_done, &woken);
    }
    return woken == pdTRUE;
}

static void flip_drawn(void)
{
    if (s_fb_count < 2 || s_panel == NULL) {
        return;
    }
    (void)esp_lcd_panel_draw_bitmap(s_panel, 0, 0, s_width, s_height, s_fbs[s_draw_i]);
    s_draw_i ^= 1;
    /* Drop completions that happened before this flip, then let the caller run.
     * The buffer we write next is still on the scanner until this scan ends;
     * that wait happens in wait_flip_done() right before the next frame is
     * drawn, so the scan overlaps game logic and the software render. */
    if (s_scan_done != NULL) {
        (void)xSemaphoreTake(s_scan_done, 0);
    }
    s_flip_pending = true;
}

static void wait_flip_done(void)
{
    if (!s_flip_pending) {
        return;
    }
    s_flip_pending = false;
    if (s_scan_done == NULL) {
        return;
    }
    const uint64_t t0 = tab5_clock_us();
    (void)xSemaphoreTake(s_scan_done, pdMS_TO_TICKS(40));
    tab5_prof_add(TAB5_PROF_VSYNC, tab5_clock_us() - t0);
}

esp_err_t tab5_video_start(void)
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
        ESP_LOGE(TAB5_TAG, "bsp_display_new_with_handles failed: %s", esp_err_to_name(err));
        return err;
    }
    s_panel = handles.panel;
    if (s_panel == NULL) {
        return ESP_FAIL;
    }

    s_scan_done = xSemaphoreCreateBinary();
    if (s_scan_done != NULL) {
        esp_lcd_dpi_panel_event_callbacks_t cbs;
        memset(&cbs, 0, sizeof(cbs));
        cbs.on_frame_buf_complete = on_fb_complete;
        if (esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, NULL) != ESP_OK) {
            ESP_LOGW(TAB5_TAG, "dpi fb complete cb failed");
        }
    }

    {
        /* PPA SRM does ARGB8888 -> scale (1/16 steps) -> rotate -> RGB565 by
         * DMA. It replaces the whole CPU rotate+1.5x blit. If registration
         * fails the present path silently keeps using the CPU blit. */
        ppa_client_config_t ppa_cfg;
        memset(&ppa_cfg, 0, sizeof(ppa_cfg));
        ppa_cfg.oper_type = PPA_OPERATION_SRM;
        ppa_cfg.max_pending_trans_num = 1;
        if (ppa_register_client(&ppa_cfg, &s_ppa) != ESP_OK) {
            s_ppa = NULL;
            ESP_LOGW(TAB5_TAG, "PPA unavailable, present stays on CPU");
        } else {
            ESP_LOGI(TAB5_TAG, "PPA SRM client ready");
        }
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_disp_on_off(s_panel, true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_brightness_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_backlight_on());
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_brightness_set(70));
    grab_fbs();
    /* Black, not a tinted background: this is the only non-black fill in the
     * firmware, so any frame buffer still holding it shows up as a coloured
     * flash if it ever reaches the panel again during a transition. */
    tab5_video_fill_rgb565(TAB5_RGB565(0, 0, 0));
    /* Boot text on the scanned buffer (fb0). */
    if (s_fb_count >= 2) {
        s_draw_i = 0;
    }
    tab5_video_draw_text(24, 80, "TAB5 JINYONG", TAB5_RGB565(255, 220, 80));
    tab5_video_draw_text(24, 120, "LOADING", TAB5_RGB565(220, 220, 220));
    tab5_video_draw_text(24, 160, "HOLD SIDEWAYS TO PLAY", TAB5_RGB565(160, 180, 200));
    if (s_fb_count >= 2) {
        s_draw_i = 1;
    }
    ESP_LOGI(TAB5_TAG, "display started via official BSP (%dx%d RGB565) fbs=%d",
             s_width, s_height, s_fb_count);
    return ESP_OK;
}

void tab5_video_fill_rgb565(uint16_t color)
{
    grab_fbs();
    for (int i = 0; i < s_fb_count; ++i) {
        fill_one_rgb565(s_fbs[i], color);
    }
}

void tab5_video_draw_text(int x, int y, const char *text, uint16_t color)
{
    uint16_t *fb = fb_ptr();
    if (fb == NULL || text == NULL) {
        return;
    }
    int cx = x;
    for (const char *p = text; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n') {
            y += 10;
            cx = x;
            continue;
        }
        if (ch >= 'a' && ch <= 'z') {
            ch = (unsigned char)(ch - 32);
        }
        if (ch < 0x20 || ch > 0x5F) {
            ch = '?';
        }
        const uint8_t *glyph = kFont[ch - 0x20];
        for (int row = 0; row < 8; ++row) {
            const int py = y + row;
            if (py < 0 || py >= s_height) {
                continue;
            }
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                if ((bits & (0x80 >> col)) == 0) {
                    continue;
                }
                const int px = cx + col;
                if (px < 0 || px >= s_width) {
                    continue;
                }
                fb[py * s_width + px] = color;
            }
        }
        cx += 8;
    }
    fb_writeback(fb);
}

static inline uint16_t argb_to_rgb565(uint32_t p)
{
    return TAB5_RGB565((uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p);
}

/*
 * 90° CW: landscape(lx,ly) → portrait(ly, land_w-1-lx).
 * Portrait scanlines are sequential in fb. Walk those, not landscape rows.
 */
static void present_fit_nn(const uint32_t *src, int width, int height,
                           uint16_t *fb, int dest_w, int dest_h, int ox, int oy,
                           int land_w, int land_h)
{
    const int pitch = s_width;
    int last_sx = -1;
    uint16_t col[480];
    const int cache_col = (height <= 480);

    /* Increasing py matches typical DSI scan; reverse dx so the live FB
     * is painted with the scan instead of against it. */
    for (int dx = dest_w - 1; dx >= 0; --dx) {
        const int sx = (int)((int64_t)dx * width / dest_w);
        const int lx = ox + dx;
        const int py = land_w - 1 - lx;
        if ((unsigned)py >= (unsigned)s_height) {
            continue;
        }
        uint16_t *dst = fb + py * pitch;
        const int y0 = oy < 0 ? 0 : oy;
        const int y1 = oy + dest_h > land_h ? land_h : oy + dest_h;

        if (cache_col && sx != last_sx) {
            const uint32_t *p = src + sx;
            for (int sy = 0; sy < height; ++sy) {
                col[sy] = argb_to_rgb565(p[sy * width]);
            }
            last_sx = sx;
        }

        for (int ly = y0; ly < y1; ++ly) {
            const int px = ly;
            if ((unsigned)px >= (unsigned)pitch) {
                continue;
            }
            const int dy = ly - oy;
            const int sy = (int)((int64_t)dy * height / dest_h);
            dst[px] = cache_col ? col[sy] : argb_to_rgb565(src[sy * width + sx]);
        }
    }
}

/* Tab5 hot path: 640x480 → 960x720, 3/2 nearest, sequential FB stores. */
static void present_640x480_tab5(const uint32_t *src, uint16_t *fb)
{
    static uint16_t sy_of[720];
    static int ready;
    if (!ready) {
        for (int i = 0; i < 720; ++i) {
            sy_of[i] = (uint16_t)(i * 2 / 3);
        }
        ready = 1;
    }

    int last_sx = -1;
    uint16_t col[480];
    /* Portrait scan py 160..1119 ↔ landscape dx 959..0. */
    for (int py = 160; py <= 1119; ++py) {
        const int dx = 1119 - py;
        const int sx = dx * 2 / 3;
        if (sx != last_sx) {
            const uint32_t *p = src + sx;
            for (int sy = 0; sy < 480; ++sy) {
                col[sy] = argb_to_rgb565(p[sy * 640]);
            }
            last_sx = sx;
        }
        uint32_t *dst32 = (uint32_t *)(fb + py * 720);
        for (int px = 0; px < 720; px += 2) {
            dst32[px / 2] = (uint32_t)col[sy_of[px]]
                | ((uint32_t)col[sy_of[px + 1]] << 16);
        }
    }
}

/*
 * Hardware present. Landscape space is (land_w = panel_h) x (land_h = panel_w);
 * the CPU path maps landscape (lx,ly) to panel (ly, land_w-1-lx), i.e. a 90
 * degree counter-clockwise rotation of the source picture, which is exactly
 * PPA_SRM_ROTATION_ANGLE_90.
 *
 * Returns false when the requested geometry is not expressible by the PPA, so
 * the caller can fall back without any visual difference in aspect handling.
 */
static bool ppa_present(const uint32_t *src, int width, int height, uint16_t *fb,
                        int dest_w, int land_w, int land_h)
{
    if (s_ppa == NULL || s_fb_bytes == 0) {
        return false;
    }
    /* PPA scaling is n/16. Snap the letterbox to a ratio the hardware has. */
    const int s16 = (int)(((int64_t)dest_w * 16) / width);
    if (s16 < 1 || s16 > 255) {
        return false;
    }
    const int bw = (int)((int64_t)width * s16 / 16);   /* landscape width  */
    const int bh = (int)((int64_t)height * s16 / 16);  /* landscape height */
    if (bw <= 0 || bh <= 0 || bw > land_w || bh > land_h) {
        return false;
    }
    const int lox = (land_w - bw) / 2;
    const int loy = (land_h - bh) / 2;

    ppa_srm_oper_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.in.buffer = src;
    cfg.in.pic_w = (uint32_t)width;
    cfg.in.pic_h = (uint32_t)height;
    cfg.in.block_w = (uint32_t)width;
    cfg.in.block_h = (uint32_t)height;
    cfg.in.srm_cm = PPA_SRM_COLOR_MODE_ARGB8888;
    cfg.out.buffer = fb;
    cfg.out.buffer_size = (uint32_t)s_fb_bytes;
    cfg.out.pic_w = (uint32_t)s_width;
    cfg.out.pic_h = (uint32_t)s_height;
    cfg.out.block_offset_x = (uint32_t)loy;               /* panel x = landscape y */
    cfg.out.block_offset_y = (uint32_t)(land_w - lox - bw); /* panel y = land_w-1-lx */
    cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
    cfg.scale_x = (float)s16 / 16.0f;
    cfg.scale_y = cfg.scale_x;
    cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    cfg.mode = PPA_TRANS_MODE_BLOCKING;

    if (ppa_do_scale_rotate_mirror(s_ppa, &cfg) != ESP_OK) {
        ESP_LOGW(TAB5_TAG, "PPA blit %dx%d s16=%d failed, dropping to CPU",
                 width, height, s16);
        s_ppa = NULL;
        return false;
    }
    return true;
}

void tab5_video_present_argb(const uint32_t *src, int width, int height)
{
    if (src == NULL || width <= 0 || height <= 0) {
        return;
    }
    grab_fbs();
    /* Only now do we need the back buffer to be off the scanner. */
    wait_flip_done();
    uint16_t *fb = fb_ptr();
    if (fb == NULL) {
        return;
    }
    const uint64_t t0 = tab5_clock_us();

    /* Landscape virtual size after 90° CW: W=panel_h (1280), H=panel_w (720). */
    const int land_w = s_height;
    const int land_h = s_width;

    int dest_w;
    int dest_h;
    if ((int64_t)land_w * height <= (int64_t)land_h * width) {
        dest_w = land_w;
        dest_h = (int)((int64_t)land_w * height / width);
    } else {
        dest_h = land_h;
        dest_w = (int)((int64_t)land_h * width / height);
    }
    if (dest_w < 1) {
        dest_w = 1;
    }
    if (dest_h < 1) {
        dest_h = 1;
    }
    if (dest_w > land_w) {
        dest_w = land_w;
    }
    if (dest_h > land_h) {
        dest_h = land_h;
    }

    const int ox = (land_w - dest_w) / 2;
    const int oy = (land_h - dest_h) / 2;

    static int last_w, last_h, last_dw, last_dh;
    if (last_w != width || last_h != height || last_dw != dest_w || last_dh != dest_h) {
        tab5_video_fill_rgb565(TAB5_RGB565(0, 0, 0));
        last_w = width;
        last_h = height;
        last_dw = dest_w;
        last_dh = dest_h;
        ESP_LOGI(TAB5_TAG, "present %dx%d -> %dx%d letterbox (%d,%d) land=%dx%d",
                 width, height, dest_w, dest_h, ox, oy, land_w, land_h);
    }

    if (!ppa_present(src, width, height, fb, dest_w, land_w, land_h)) {
        if (width == 640 && height == 480 && dest_w == 960 && dest_h == 720
            && ox == 160 && oy == 0 && land_w == 1280 && land_h == 720
            && s_width == 720 && s_height == 1280) {
            present_640x480_tab5(src, fb);
        } else {
            present_fit_nn(src, width, height, fb, dest_w, dest_h, ox, oy, land_w, land_h);
        }
        fb_writeback(fb);
    }
    tab5_prof_add(TAB5_PROF_BLIT, tab5_clock_us() - t0);
    flip_drawn();
}
