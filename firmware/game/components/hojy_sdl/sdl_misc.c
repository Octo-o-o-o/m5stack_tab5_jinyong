/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sdl_internal.h"

#include "tab5_platform.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <stdio.h>

static uint32_t s_init;
static char s_error[128];

void sdl_set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_error, sizeof(s_error), fmt, ap);
    va_end(ap);
}

uint32_t sdl_pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    /* SDL_PIXELFORMAT_ARGB8888 / HOJY palette: 0xAARRGGBB */
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void sdl_unpack(uint32_t p, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    *b = (uint8_t)p;
    *g = (uint8_t)(p >> 8);
    *r = (uint8_t)(p >> 16);
    *a = (uint8_t)(p >> 24);
}

const char *SDL_GetError(void)
{
    return s_error;
}

void SDL_SetError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_error, sizeof(s_error), fmt, ap);
    va_end(ap);
}

void SDL_ClearError(void)
{
    s_error[0] = 0;
}

void SDL_Log(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ESP_LOGI(TAB5_TAG, "%s", buf);
}

int SDL_Init(Uint32 flags)
{
    s_init |= flags;
    return 0;
}

int SDL_InitSubSystem(Uint32 flags)
{
    s_init |= flags;
    return 0;
}

Uint32 SDL_WasInit(Uint32 flags)
{
    if (flags == 0) {
        return s_init;
    }
    return s_init & flags;
}

void SDL_Quit(void)
{
    s_init = 0;
}

int SDL_SetHint(const char *name, const char *value)
{
    (void)name;
    (void)value;
    return 1;
}

void *SDL_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) {
        p = malloc(size);
    }
    return p;
}

void SDL_free(void *ptr)
{
    free(ptr);
}

Uint32 SDL_GetTicks(void)
{
    return (Uint32)(tab5_clock_us() / 1000ULL);
}

Uint64 SDL_GetTicks64(void)
{
    return tab5_clock_us() / 1000ULL;
}

Uint64 SDL_GetPerformanceCounter(void)
{
    return tab5_clock_us();
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return 1000000ULL;
}

void SDL_Delay(Uint32 ms)
{
    tab5_delay_ms(ms);
}

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size)
{
    if (mem == NULL || size < 0) {
        return NULL;
    }
    SDL_RWops *rw = (SDL_RWops *)SDL_malloc(sizeof(SDL_RWops));
    if (rw == NULL) {
        return NULL;
    }
    rw->base = (const Uint8 *)mem;
    rw->here = rw->base;
    rw->stop = rw->base + size;
    rw->freesrc = 0;
    return rw;
}
