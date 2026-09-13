/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fallback_ui.h"

#include "tab5_platform.h"

void fallback_show(const char *line1, const char *line2, const char *line3)
{
    tab5_video_fill_rgb565(TAB5_RGB565(0, 0, 0));
    tab5_video_draw_text(24, 80, "TAB5 JINYONG", TAB5_RGB565(255, 220, 80));
    if (line1) {
        tab5_video_draw_text(24, 120, line1, TAB5_RGB565(220, 220, 220));
    }
    if (line2) {
        tab5_video_draw_text(24, 140, line2, TAB5_RGB565(180, 200, 220));
    }
    if (line3) {
        tab5_video_draw_text(24, 160, line3, TAB5_RGB565(160, 160, 160));
    }
}
