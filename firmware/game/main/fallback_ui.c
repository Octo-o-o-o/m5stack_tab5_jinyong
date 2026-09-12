#include "fallback_ui.h"

#include "tab5_platform.h"

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))

void fallback_show(const char *line1, const char *line2, const char *line3)
{
    tab5_video_fill_rgb565(RGB565(0, 0, 0));
    tab5_video_draw_text(24, 80, "TAB5 JINYONG", RGB565(255, 220, 80));
    if (line1) {
        tab5_video_draw_text(24, 120, line1, RGB565(220, 220, 220));
    }
    if (line2) {
        tab5_video_draw_text(24, 140, line2, RGB565(180, 200, 220));
    }
    if (line3) {
        tab5_video_draw_text(24, 160, line3, RGB565(160, 160, 160));
    }
    tab5_video_draw_text(24, 200, "NEED SD /JINYONG + GAME DATA", RGB565(255, 160, 80));
    tab5_video_draw_text(24, 220, "FLASH STILL REQUIRES AUTHORIZATION", RGB565(120, 140, 160));
}
