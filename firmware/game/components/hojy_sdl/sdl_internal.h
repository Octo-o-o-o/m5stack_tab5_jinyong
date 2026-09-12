#pragma once

#include "SDL.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Window {
    int w;
    int h;
    char title[64];
};

struct SDL_Texture {
    int w;
    int h;
    int access;
    int pitch;
    uint32_t *pixels;
    int blend;
    uint8_t cr, cg, cb, ca;
};

struct SDL_Renderer {
    SDL_Window *window;
    SDL_Texture *target;
    uint32_t *back;
    int w;
    int h;
    uint8_t r, g, b, a;
    int blend;
};

void sdl_set_error(const char *fmt, ...);
uint32_t sdl_pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void sdl_unpack(uint32_t p, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);
void sdl_fill_argb(uint32_t *dst, int dw, int dh, int x, int y, int w, int h,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void sdl_plot(uint32_t *dst, int dw, int dh, int x, int y,
              uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void sdl_pump_keyboard(void);

#ifdef __cplusplus
}
#endif
