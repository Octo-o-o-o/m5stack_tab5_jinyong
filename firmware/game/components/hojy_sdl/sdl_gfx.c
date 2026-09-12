#include "SDL2_gfxPrimitives.h"
#include "sdl_internal.h"

static uint32_t *ren_px(SDL_Renderer *r, int *w, int *h)
{
    if (r == NULL) {
        return NULL;
    }
    if (r->target) {
        *w = r->target->w;
        *h = r->target->h;
        return r->target->pixels;
    }
    *w = r->w;
    *h = r->h;
    return r->back;
}

static void norm(Sint16 *a, Sint16 *b)
{
    if (*a > *b) {
        const Sint16 t = *a;
        *a = *b;
        *b = t;
    }
}

int boxRGBA(SDL_Renderer *r, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2,
            Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    int w = 0, h = 0;
    uint32_t *px = ren_px(r, &w, &h);
    if (px == NULL) {
        return -1;
    }
    norm(&x1, &x2);
    norm(&y1, &y2);
    sdl_fill_argb(px, w, h, x1, y1, x2 - x1 + 1, y2 - y1 + 1, red, green, blue, alpha);
    return 0;
}

int rectangleRGBA(SDL_Renderer *r, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2,
                  Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    int w = 0, h = 0;
    uint32_t *px = ren_px(r, &w, &h);
    if (px == NULL) {
        return -1;
    }
    norm(&x1, &x2);
    norm(&y1, &y2);
    sdl_fill_argb(px, w, h, x1, y1, x2 - x1 + 1, 1, red, green, blue, alpha);
    sdl_fill_argb(px, w, h, x1, y2, x2 - x1 + 1, 1, red, green, blue, alpha);
    sdl_fill_argb(px, w, h, x1, y1, 1, y2 - y1 + 1, red, green, blue, alpha);
    sdl_fill_argb(px, w, h, x2, y1, 1, y2 - y1 + 1, red, green, blue, alpha);
    return 0;
}

int filledCircleRGBA(SDL_Renderer *r, Sint16 x, Sint16 y, Sint16 rad,
                     Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    int w = 0, h = 0;
    uint32_t *px = ren_px(r, &w, &h);
    if (px == NULL || rad < 0) {
        return -1;
    }
    const int r2 = rad * rad;
    for (int dy = -rad; dy <= rad; ++dy) {
        for (int dx = -rad; dx <= rad; ++dx) {
            if (dx * dx + dy * dy <= r2) {
                sdl_plot(px, w, h, x + dx, y + dy, red, green, blue, alpha);
            }
        }
    }
    return 0;
}

int circleRGBA(SDL_Renderer *r, Sint16 x, Sint16 y, Sint16 rad,
               Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    int w = 0, h = 0;
    uint32_t *px = ren_px(r, &w, &h);
    if (px == NULL || rad < 0) {
        return -1;
    }
    int dx = rad, dy = 0, err = 0;
    while (dx >= dy) {
        sdl_plot(px, w, h, x + dx, y + dy, red, green, blue, alpha);
        sdl_plot(px, w, h, x + dy, y + dx, red, green, blue, alpha);
        sdl_plot(px, w, h, x - dy, y + dx, red, green, blue, alpha);
        sdl_plot(px, w, h, x - dx, y + dy, red, green, blue, alpha);
        sdl_plot(px, w, h, x - dx, y - dy, red, green, blue, alpha);
        sdl_plot(px, w, h, x - dy, y - dx, red, green, blue, alpha);
        sdl_plot(px, w, h, x + dy, y - dx, red, green, blue, alpha);
        sdl_plot(px, w, h, x + dx, y - dy, red, green, blue, alpha);
        ++dy;
        if (err <= 0) {
            err += 2 * dy + 1;
        }
        if (err > 0) {
            --dx;
            err -= 2 * dx + 1;
        }
    }
    return 0;
}

int roundedBoxRGBA(SDL_Renderer *r, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2, Sint16 rad,
                  Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    norm(&x1, &x2);
    norm(&y1, &y2);
    if (rad < 0) {
        rad = 0;
    }
    const Sint16 w = (Sint16)(x2 - x1);
    const Sint16 h = (Sint16)(y2 - y1);
    if (rad * 2 > w) {
        rad = w / 2;
    }
    if (rad * 2 > h) {
        rad = h / 2;
    }
    boxRGBA(r, (Sint16)(x1 + rad), y1, (Sint16)(x2 - rad), y2, red, green, blue, alpha);
    boxRGBA(r, x1, (Sint16)(y1 + rad), x2, (Sint16)(y2 - rad), red, green, blue, alpha);
    filledCircleRGBA(r, (Sint16)(x1 + rad), (Sint16)(y1 + rad), rad, red, green, blue, alpha);
    filledCircleRGBA(r, (Sint16)(x2 - rad), (Sint16)(y1 + rad), rad, red, green, blue, alpha);
    filledCircleRGBA(r, (Sint16)(x1 + rad), (Sint16)(y2 - rad), rad, red, green, blue, alpha);
    filledCircleRGBA(r, (Sint16)(x2 - rad), (Sint16)(y2 - rad), rad, red, green, blue, alpha);
    return 0;
}

int roundedRectangleRGBA(SDL_Renderer *r, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2, Sint16 rad,
                         Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    norm(&x1, &x2);
    norm(&y1, &y2);
    if (rad < 0) {
        rad = 0;
    }
    rectangleRGBA(r, x1, y1, x2, y2, red, green, blue, alpha);
    (void)rad;
    return 0;
}
