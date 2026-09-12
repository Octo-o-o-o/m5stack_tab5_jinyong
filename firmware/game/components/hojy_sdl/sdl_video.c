#include "sdl_internal.h"

#include "tab5_platform.h"

#include "esp_log.h"

#include <stdlib.h>

static void blend_pixel(uint32_t *dst, uint32_t src, int blend,
                        uint8_t mr, uint8_t mg, uint8_t mb, uint8_t ma)
{
    uint8_t sr, sg, sb, sa;
    sdl_unpack(src, &sr, &sg, &sb, &sa);
    sr = (uint8_t)((sr * mr) / 255);
    sg = (uint8_t)((sg * mg) / 255);
    sb = (uint8_t)((sb * mb) / 255);
    sa = (uint8_t)((sa * ma) / 255);
    if (sa == 0) {
        return;
    }
    if (!blend || sa == 255) {
        *dst = sdl_pack(sr, sg, sb, sa);
        return;
    }
    uint8_t dr, dg, db, da;
    sdl_unpack(*dst, &dr, &dg, &db, &da);
    const uint8_t inv = (uint8_t)(255 - sa);
    dr = (uint8_t)((sr * sa + dr * inv) / 255);
    dg = (uint8_t)((sg * sa + dg * inv) / 255);
    db = (uint8_t)((sb * sa + db * inv) / 255);
    da = (uint8_t)(sa + (da * inv) / 255);
    *dst = sdl_pack(dr, dg, db, da);
}

/*
 * Same arithmetic as blend_pixel() with an identity colour mod, written as one
 * expression so the hot loops do not pay for the four mod multiply/divides,
 * the unpack/pack helper calls or the per-pixel bounds tests.
 * Callers must have checked that src alpha is in (0, 255).
 */
static inline uint32_t blend_over(uint32_t d, uint32_t s)
{
    const uint32_t sa = s >> 24;
    const uint32_t inv = 255u - sa;
    const uint32_t r = (((s >> 16) & 0xFFu) * sa + ((d >> 16) & 0xFFu) * inv) / 255u;
    const uint32_t g = (((s >> 8) & 0xFFu) * sa + ((d >> 8) & 0xFFu) * inv) / 255u;
    const uint32_t b = ((s & 0xFFu) * sa + (d & 0xFFu) * inv) / 255u;
    const uint32_t a = sa + ((d >> 24) * inv) / 255u;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* src alpha 0 never writes: upstream relies on that for RLE gaps. */
static inline void put_plain(uint32_t *dst, uint32_t s, int blend)
{
    const uint32_t sa = s >> 24;
    if (sa == 0u) {
        return;
    }
    if (sa == 255u || !blend) {
        *dst = s;
        return;
    }
    *dst = blend_over(*dst, s);
}

void sdl_plot(uint32_t *dst, int dw, int dh, int x, int y,
              uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if ((unsigned)x >= (unsigned)dw || (unsigned)y >= (unsigned)dh) {
        return;
    }
    uint32_t *p = dst + y * dw + x;
    if (a == 0) {
        return;
    }
    if (a == 255) {
        *p = sdl_pack(r, g, b, a);
        return;
    }
    blend_pixel(p, sdl_pack(r, g, b, a), 1, 255, 255, 255, 255);
}

void sdl_fill_argb(uint32_t *dst, int dw, int dh, int x, int y, int w, int h,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > dw) {
        w = dw - x;
    }
    if (y + h > dh) {
        h = dh - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    const uint32_t packed = sdl_pack(r, g, b, a);
    if (a == 0) {
        /* Matches sdl_plot(): a fully transparent fill writes nothing. */
        return;
    }
    if (a == 255) {
        for (int row = 0; row < h; ++row) {
            uint32_t *line = dst + (y + row) * dw + x;
            for (int col = 0; col < w; ++col) {
                line[col] = packed;
            }
        }
        return;
    }
    /* Mask fade-in/out fills the whole 640x480 every frame; the old path went
     * through sdl_plot + sdl_pack + blend_pixel per pixel. */
    for (int row = 0; row < h; ++row) {
        uint32_t *line = dst + (y + row) * dw + x;
        for (int col = 0; col < w; ++col) {
            line[col] = blend_over(line[col], packed);
        }
    }
}

static uint32_t *renderer_pixels(SDL_Renderer *ren, int *w, int *h)
{
    if (ren == NULL) {
        return NULL;
    }
    if (ren->target) {
        *w = ren->target->w;
        *h = ren->target->h;
        return ren->target->pixels;
    }
    *w = ren->w;
    *h = ren->h;
    return ren->back;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
    (void)x;
    (void)y;
    (void)flags;
    SDL_Window *win = (SDL_Window *)calloc(1, sizeof(SDL_Window));
    if (win == NULL) {
        sdl_set_error("CreateWindow oom");
        return NULL;
    }
    win->w = w;
    win->h = h;
    if (title) {
        snprintf(win->title, sizeof(win->title), "%s", title);
    }
    ESP_LOGI(TAB5_TAG, "SDL_CreateWindow %dx%d", w, h);
    return win;
}

void SDL_DestroyWindow(SDL_Window *window)
{
    free(window);
}

void SDL_ShowWindow(SDL_Window *window)
{
    (void)window;
}

void SDL_SetWindowTitle(SDL_Window *window, const char *title)
{
    if (window && title) {
        snprintf(window->title, sizeof(window->title), "%s", title);
    }
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
    (void)index;
    (void)flags;
    if (window == NULL) {
        sdl_set_error("CreateRenderer null window");
        return NULL;
    }
    SDL_Renderer *ren = (SDL_Renderer *)calloc(1, sizeof(SDL_Renderer));
    if (ren == NULL) {
        return NULL;
    }
    ren->window = window;
    ren->w = window->w;
    ren->h = window->h;
    ren->r = ren->g = ren->b = 0;
    ren->a = 255;
    ren->blend = SDL_BLENDMODE_BLEND;
    const size_t bytes = (size_t)ren->w * (size_t)ren->h * 4u;
    ren->back = (uint32_t *)SDL_malloc(bytes);
    if (ren->back == NULL) {
        sdl_set_error("backbuffer %u bytes failed", (unsigned)bytes);
        free(ren);
        return NULL;
    }
    memset(ren->back, 0, bytes);
    ESP_LOGI(TAB5_TAG, "SDL_CreateRenderer backbuffer %dx%d (%u KB)",
             ren->w, ren->h, (unsigned)(bytes / 1024));
    return ren;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    if (renderer == NULL) {
        return;
    }
    SDL_free(renderer->back);
    free(renderer);
}

int SDL_SetRenderDrawBlendMode(SDL_Renderer *renderer, SDL_BlendMode mode)
{
    if (renderer) {
        renderer->blend = mode;
    }
    return 0;
}

int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    if (renderer) {
        renderer->r = r;
        renderer->g = g;
        renderer->b = b;
        renderer->a = a;
    }
    return 0;
}

int SDL_RenderClear(SDL_Renderer *renderer)
{
    int w = 0, h = 0;
    uint32_t *px = renderer_pixels(renderer, &w, &h);
    if (px == NULL) {
        return -1;
    }
    sdl_fill_argb(px, w, h, 0, 0, w, h, renderer->r, renderer->g, renderer->b, renderer->a);
    return 0;
}

int SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    if (renderer) {
        renderer->target = texture;
    }
    return 0;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
    (void)renderer;
    (void)format;
    if (w <= 0 || h <= 0) {
        sdl_set_error("CreateTexture bad size %dx%d", w, h);
        return NULL;
    }
    const size_t bytes = (size_t)w * (size_t)h * 4u;
    /* End-screen 512x4096 and the world minimap are large but must keep lock size. */
    ESP_LOGI(TAB5_TAG, "CreateTexture %dx%d (%u KB) access=%d", w, h, (unsigned)(bytes / 1024), access);
    SDL_Texture *tex = (SDL_Texture *)calloc(1, sizeof(SDL_Texture));
    if (tex == NULL) {
        return NULL;
    }
    tex->w = w;
    tex->h = h;
    tex->access = access;
    tex->pitch = w * 4;
    tex->cr = tex->cg = tex->cb = tex->ca = 255;
    tex->blend = SDL_BLENDMODE_NONE;
    tex->pixels = (uint32_t *)SDL_malloc(bytes);
    if (tex->pixels == NULL) {
        sdl_set_error("texture pixels %u bytes failed", (unsigned)bytes);
        ESP_LOGE(TAB5_TAG, "CreateTexture oom %u bytes", (unsigned)bytes);
        free(tex);
        return NULL;
    }
    memset(tex->pixels, 0, bytes);
    return tex;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
    if (texture == NULL) {
        return;
    }
    SDL_free(texture->pixels);
    free(texture);
}

int SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode mode)
{
    if (texture) {
        texture->blend = mode;
    }
    return 0;
}

int SDL_SetTextureColorMod(SDL_Texture *texture, Uint8 r, Uint8 g, Uint8 b)
{
    if (texture) {
        texture->cr = r;
        texture->cg = g;
        texture->cb = b;
    }
    return 0;
}

int SDL_SetTextureAlphaMod(SDL_Texture *texture, Uint8 a)
{
    if (texture) {
        texture->ca = a;
    }
    return 0;
}

int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch)
{
    if (texture == NULL || texture->pixels == NULL || pixels == NULL || pitch == NULL) {
        return -1;
    }
    *pitch = texture->pitch;
    if (rect == NULL) {
        *pixels = texture->pixels;
        return 0;
    }
    *pixels = texture->pixels + rect->y * texture->w + rect->x;
    return 0;
}

void SDL_UnlockTexture(SDL_Texture *texture)
{
    (void)texture;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                   const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
    int dw = 0, dh = 0;
    uint32_t *dst = renderer_pixels(renderer, &dw, &dh);
    if (dst == NULL || texture == NULL || texture->pixels == NULL) {
        return -1;
    }

    SDL_Rect src = {0, 0, texture->w, texture->h};
    if (srcrect) {
        src = *srcrect;
    }
    SDL_Rect d = {0, 0, src.w, src.h};
    if (dstrect) {
        d = *dstrect;
    }
    if (src.w <= 0 || src.h <= 0 || d.w <= 0 || d.h <= 0) {
        return 0;
    }
    if (src.x < 0) {
        src.w += src.x;
        src.x = 0;
    }
    if (src.y < 0) {
        src.h += src.y;
        src.y = 0;
    }
    if (src.x + src.w > texture->w) {
        src.w = texture->w - src.x;
    }
    if (src.y + src.h > texture->h) {
        src.h = texture->h - src.y;
    }
    if (src.w <= 0 || src.h <= 0) {
        return 0;
    }

    const int blend = texture->blend;
    const uint8_t mr = texture->cr, mg = texture->cg, mb = texture->cb, ma = texture->ca;
    const int exact = (d.w == src.w && d.h == src.h);
    const int modded = (mr != 255 || mg != 255 || mb != 255 || ma != 255);

    /* Clip the destination once. Inside the loops every index is in range, so
     * the per-pixel bounds tests and the per-pixel divide both disappear. */
    int dx0 = 0, dx1 = d.w;
    if (d.x < 0) { dx0 = -d.x; }
    if (d.x + d.w > dw) { dx1 = dw - d.x; }
    int dy0 = 0, dy1 = d.h;
    if (d.y < 0) { dy0 = -d.y; }
    if (d.y + d.h > dh) { dy1 = dh - d.y; }
    if (dx0 >= dx1 || dy0 >= dy1) {
        return 0;
    }

    /*
     * Source stepping by error accumulation. This reproduces
     * `src.x + dx * src.w / d.w` for every dx exactly (truncating division),
     * without a division inside the loop.
     */
    int sy = src.y + (exact ? dy0 : (int)((int64_t)dy0 * src.h / d.h));
    int erry = exact ? 0 : (int)((int64_t)dy0 * src.h % d.h);
    const int sx0 = src.x + (exact ? dx0 : (int)((int64_t)dx0 * src.w / d.w));
    const int errx0 = exact ? 0 : (int)((int64_t)dx0 * src.w % d.w);
    const int run = dx1 - dx0;

    for (int dy = dy0; dy < dy1; ++dy) {
        const uint32_t *srow = texture->pixels + (size_t)sy * texture->w;
        /* d.x may be negative; dx0 cancels it, so form the final address in one
         * step instead of materialising a pointer before the buffer. */
        uint32_t *dp = dst + (size_t)(d.y + dy) * dw + (d.x + dx0);
        if (exact) {
            const uint32_t *sp = srow + sx0;
            if (modded) {
                for (int i = 0; i < run; ++i) {
                    blend_pixel(&dp[i], sp[i], blend, mr, mg, mb, ma);
                }
            } else {
                for (int i = 0; i < run; ++i) {
                    put_plain(&dp[i], sp[i], blend);
                }
            }
            ++sy;
            continue;
        }
        int sx = sx0, errx = errx0;
        if (modded) {
            for (int i = 0; i < run; ++i) {
                blend_pixel(&dp[i], srow[sx], blend, mr, mg, mb, ma);
                errx += src.w;
                while (errx >= d.w) { errx -= d.w; ++sx; }
            }
        } else {
            for (int i = 0; i < run; ++i) {
                put_plain(&dp[i], srow[sx], blend);
                errx += src.w;
                while (errx >= d.w) { errx -= d.w; ++sx; }
            }
        }
        erry += src.h;
        while (erry >= d.h) { erry -= d.h; ++sy; }
    }
    return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    if (renderer == NULL || renderer->back == NULL) {
        return;
    }
    tab5_video_present_argb(renderer->back, renderer->w, renderer->h);
}
