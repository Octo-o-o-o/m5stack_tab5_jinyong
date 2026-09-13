/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

int boxRGBA(SDL_Renderer *r, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2,
            Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha);
int rectangleRGBA(SDL_Renderer *r, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2,
                  Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha);
int roundedBoxRGBA(SDL_Renderer *r, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2, Sint16 rad,
                   Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha);
int roundedRectangleRGBA(SDL_Renderer *r, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2, Sint16 rad,
                         Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha);
int circleRGBA(SDL_Renderer *r, Sint16 x, Sint16 y, Sint16 rad,
               Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha);
int filledCircleRGBA(SDL_Renderer *r, Sint16 x, Sint16 y, Sint16 rad,
                     Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha);

#ifdef __cplusplus
}
#endif
