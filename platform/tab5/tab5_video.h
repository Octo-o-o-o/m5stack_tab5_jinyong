#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Low-resolution game FB. Final blit is nearest-neighbor with letterbox. */

enum {
    TAB5_VIDEO_GAME_WIDTH = 640,
    TAB5_VIDEO_GAME_HEIGHT = 480,
};

typedef struct {
    uint16_t *pixels; /* RGB565 */
    int width;
    int height;
    int stride_px;
} tab5_game_fb_t;

#ifdef __cplusplus
}
#endif
