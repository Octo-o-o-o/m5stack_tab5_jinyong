/*
 * SPDX-FileCopyrightText: 2021 Soar Qin <soarchin@gmail.com>
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Heroes of Jin Yong.
 * A reimplementation of the DOS game `The legend of Jin Yong Heroes`.
 * Copyright (C) 2021, Soar Qin<soarchin@gmail.com>

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Overlay of HeroesOfJinYong src/scene/ttf.cc for ESP32-P4.
 * Upstream atlas is 1024x1024 (4MB). After SMP/ALLSIN that allocation
 * often fails; makeCache still returns metrics, so TalkBox draws an
 * empty frame. Use a 256x256 atlas, fail closed, and fall back to a
 * per-glyph texture.
 */

#include "ttf.hh"

#include "rectpacker.hh"
#include "renderer.hh"
#include "texture.hh"
#include "util/file.hh"
#include "esp_log.h"
#include "tab5_platform.h"

#ifdef USE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#else
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <external/stb_truetype.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hojy::scene {

namespace {
constexpr int kAtlasSize = 256;
constexpr int kGlyphMax = 64;
}

TTF::TTF(Renderer *renderer): renderer_(renderer), rectpacker_(new RectPacker(kAtlasSize, kAtlasSize)) {
#ifdef USE_FREETYPE
    FT_Init_FreeType(&ftLib_);
#endif
}

TTF::~TTF() {
    deinit();
#ifdef USE_FREETYPE
    FT_Done_FreeType(ftLib_);
#endif
}

void TTF::init(int size, std::uint8_t width) {
    fontSize_ = size;
    monoWidth_ = width;
}

void TTF::deinit() {
    for (auto &tex: textures_) {
        delete tex;
    }
    textures_.clear();
    fontCache_.clear();
    for (auto &p: fonts_) {
#ifdef USE_FREETYPE
        FT_Done_Face(p.face);
#else
        delete static_cast<stbtt_fontinfo *>(p.font);
        p.ttf_buffer.clear();
#endif
    }
    fonts_.clear();
}

bool TTF::add(const std::string &filename, int index) {
    FontInfo fi;
#ifdef USE_FREETYPE
    if (FT_New_Face(ftLib_, filename.c_str(), index, &fi.face)) return false;
    fonts_.emplace_back(fi);
#else
    if (!util::File::getFileContent(filename, fi.ttf_buffer)) {
        return false;
    }
    if (fi.ttf_buffer.empty()) {
        return false;
    }
    auto *info = new stbtt_fontinfo;
    const int offset = stbtt_GetFontOffsetForIndex(&fi.ttf_buffer[0], index);
    if (!stbtt_InitFont(info, &fi.ttf_buffer[0], offset)) {
        ESP_LOGE(TAB5_TAG, "TTF InitFont failed %s", filename.c_str());
        delete info;
        return false;
    }
    fi.font = info;
    fonts_.emplace_back(std::move(fi));
    ESP_LOGI(TAB5_TAG, "TTF stbtt glyphs=%d cmap=%d bytes=%u",
             info->numGlyphs, info->index_map != 0,
             (unsigned)fonts_.back().ttf_buffer.size());
#endif
    return true;
}

void TTF::charDimension(std::uint32_t ch, std::uint8_t &width, std::int8_t &t, std::int8_t &b, int fontSize) {
    if (fontSize < 0) fontSize = fontSize_;
    const FontData *fd;
    std::uint64_t key = (std::uint64_t(fontSize) << 32) | std::uint64_t(ch);
    auto ite = fontCache_.find(key);
    if (ite == fontCache_.end()) {
        fd = makeCache(ch, fontSize);
        if (!fd) {
            width = t = b = 0;
            return;
        }
    } else {
        fd = &ite->second;
        if (fd->advW == 0) {
            width = t = b = 0;
            return;
        }
    }
    if (monoWidth_)
        width = std::max(fd->advW, monoWidth_);
    else
        width = fd->advW;
    t = fd->iy0;
    b = fd->iy0 + fd->h;
}

int TTF::stringWidth(const std::wstring &str, int fontSize) {
    std::uint8_t w;
    std::int8_t t, b;
    int res = 0;
    for (auto &ch: str) {
        if (ch < 32) { continue; }
        charDimension(ch, w, t, b, fontSize);
        res += int(std::uint32_t(w));
    }
    return res;
}

void TTF::setColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    altR_[0] = r; altG_[0] = g; altB_[0] = b;
}

void TTF::setAltColor(int index, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (index > 0 && index <= 16) {
        --index;
        altR_[index] = r;
        altG_[index] = g;
        altB_[index] = b;
    }
}

void TTF::render(std::wstring_view str, int x, int y, bool shadow, int fontSize) {
    if (fontSize < 0) fontSize = fontSize_;
    int colorIndex = 0;
    for (auto ch: str) {
        if (ch > 0 && ch < 17) { colorIndex = ch - 1; continue; }
        const FontData *fd;
        std::uint64_t key = (std::uint64_t(fontSize) << 32) | std::uint64_t(ch);
        auto ite = fontCache_.find(key);
        if (ite == fontCache_.end()) {
            fd = makeCache(ch, fontSize);
            if (!fd) {
                continue;
            }
        } else {
            fd = &ite->second;
            if (fd->advW == 0) continue;
        }
        if (fd->rpidx >= textures_.size()) {
            continue;
        }
        auto *tex = textures_[fd->rpidx];
        if (!tex || !tex->data()) {
            continue;
        }
        if (shadow) {
            tex->setBlendColor(0, 0, 0, 255);
            renderer_->renderTexture(tex, x + fd->ix0 + 2, y + fd->iy0 + 2, fd->rpx, fd->rpy, fd->w, fd->h, true);
            tex->setBlendColor(altR_[colorIndex], altG_[colorIndex], altB_[colorIndex], 255);
            renderer_->renderTexture(tex, x + fd->ix0, y + fd->iy0, fd->rpx, fd->rpy, fd->w, fd->h, true);
        } else {
            tex->setBlendColor(altR_[colorIndex], altG_[colorIndex], altB_[colorIndex], 255);
            renderer_->renderTexture(tex, x + fd->ix0, y + fd->iy0, fd->rpx, fd->rpy, fd->w, fd->h, true);
        }
        x += fd->advW;
    }
}

const TTF::FontData *TTF::makeCache(std::uint32_t ch, int fontSize) {
    if (fontSize < 0) fontSize = fontSize_;
    FontInfo *fi = nullptr;
#ifndef USE_FREETYPE
    stbtt_fontinfo *info = nullptr;
    std::uint32_t index = 0;
#endif
    for (auto &f: fonts_) {
#ifdef USE_FREETYPE
        auto index = FT_Get_Char_Index(f.face, ch);
        if (index == 0) continue;
        FT_Set_Pixel_Sizes(f.face, 0, fontSize);
        auto err = FT_Load_Glyph(f.face, index, FT_LOAD_DEFAULT);
        if (!err) { fi = &f; break; }
#else
        info = static_cast<stbtt_fontinfo*>(f.font);
        index = stbtt_FindGlyphIndex(info, ch);
        if (index != 0) { fi = &f; break; }
#endif
    }
    std::uint64_t key = (std::uint64_t(fontSize) << 32) | std::uint64_t(ch);
    FontData *fd = &fontCache_[key];
    if (fi == nullptr) {
        memset(fd, 0, sizeof(FontData));
        static int missing;
        if (missing < 8) {
            ++missing;
            ESP_LOGW(TAB5_TAG, "TTF missing U+%04X", (unsigned)ch);
        }
        return nullptr;
    }

#ifdef USE_FREETYPE
    unsigned char *srcPtr;
    int bitmapPitch;
    if (FT_Render_Glyph(fi->face->glyph, FT_RENDER_MODE_NORMAL)) return nullptr;
    FT_GlyphSlot slot = fi->face->glyph;
    fd->ix0 = slot->bitmap_left;
    fd->iy0 = fontSize * 7 / 8 - slot->bitmap_top;
    fd->w = slot->bitmap.width;
    fd->h = slot->bitmap.rows;
    fd->advW = slot->advance.x >> 6;
    srcPtr = slot->bitmap.buffer;
    bitmapPitch = slot->bitmap.pitch;
#else
    int advW, leftB;
    float fontScale = stbtt_ScaleForMappingEmToPixels(info, static_cast<float>(fontSize));
    stbtt_GetGlyphHMetrics(info, index, &advW, &leftB);
    int ascent, descent;
    stbtt_GetFontVMetrics(info, &ascent, &descent, nullptr);
    fd->advW = std::uint8_t(std::lround(fontScale * float(advW)));
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(info, index, fontScale, fontScale, &x0, &y0, &x1, &y1);
    int w = x1 - x0;
    int h = y1 - y0;
    fd->ix0 = static_cast<std::int8_t>(std::clamp(x0, -128, 127));
    int iy = int(float(ascent + descent) * fontScale) + y0;
    fd->iy0 = static_cast<std::int8_t>(std::clamp(iy, -128, 127));
    if (w < 0) { w = 0; }
    if (h < 0) { h = 0; }
    if (w > kGlyphMax) { w = kGlyphMax; }
    if (h > kGlyphMax) { h = kGlyphMax; }
    fd->w = static_cast<std::uint8_t>(w);
    fd->h = static_cast<std::uint8_t>(h);
#endif

    int dstPitch = int((fd->w + 1u) & ~1u);
    std::uint8_t dst[kGlyphMax * kGlyphMax];
    memset(dst, 0, sizeof(dst));

#ifdef USE_FREETYPE
    auto *dstPtr = dst;
    for (int k = 0; k < fd->h; ++k) {
        memcpy(dstPtr, srcPtr, fd->w);
        srcPtr += bitmapPitch;
        dstPtr += dstPitch;
    }
#else
    if (fd->w > 0 && fd->h > 0) {
        stbtt_MakeGlyphBitmapSubpixel(info, dst, fd->w, fd->h, dstPitch, fontScale, fontScale, 0, 0, index);
    }
#endif

    auto failFd = [&]() -> const FontData * {
        memset(fd, 0, sizeof(FontData));
        return nullptr;
    };

    auto upload = [&](Texture *tex, std::int16_t px, std::int16_t py) -> bool {
        if (!tex || !tex->data()) {
            return false;
        }
        int pitch = 0;
        uint32_t *pixels = tex->lock(pitch, px, py, dstPitch, fd->h);
        if (!pixels) {
            return false;
        }
        auto *pdst = dst;
        int offset = pitch - dstPitch;
        int hh = fd->h;
        while (hh--) {
            int ww = dstPitch;
            while (ww--) {
                *pixels++ = 0xFFFFFFu | (std::uint32_t(*pdst++) << 24);
            }
            pixels += offset;
        }
        tex->unlock();
        return true;
    };

    Texture *tex = nullptr;
    if (fd->w > 0 && fd->h > 0) {
        auto rpidx = rectpacker_->pack(static_cast<std::uint16_t>(dstPitch), fd->h, fd->rpx, fd->rpy);
        if (rpidx >= 0) {
            fd->rpidx = static_cast<std::uint8_t>(rpidx);
            if (rpidx >= static_cast<int>(textures_.size())) {
                textures_.resize(rpidx + 1, nullptr);
            }
            tex = textures_[rpidx];
            if (tex == nullptr) {
                tex = Texture::create(renderer_, kAtlasSize, kAtlasSize);
                if (!tex || !tex->data()) {
                    ESP_LOGE(TAB5_TAG, "TTF atlas create failed %dx%d", kAtlasSize, kAtlasSize);
                    delete tex;
                    tex = nullptr;
                    textures_[rpidx] = nullptr;
                } else {
                    tex->enableBlendMode(true);
                    textures_[rpidx] = tex;
                    ESP_LOGI(TAB5_TAG, "TTF atlas %dx%d page=%d", kAtlasSize, kAtlasSize, rpidx);
                }
            }
            if (tex && upload(tex, fd->rpx, fd->rpy)) {
                return fd;
            }
        }
        tex = Texture::create(renderer_, static_cast<std::int16_t>(std::max(dstPitch, 1)),
                              static_cast<std::int16_t>(std::max(int(fd->h), 1)));
        if (!tex || !tex->data()) {
            ESP_LOGE(TAB5_TAG, "TTF glyph tex failed U+%04X %dx%d", (unsigned)ch, dstPitch, fd->h);
            delete tex;
            return failFd();
        }
        tex->enableBlendMode(true);
        textures_.push_back(tex);
        fd->rpidx = static_cast<std::uint8_t>(textures_.size() - 1);
        fd->rpx = 0;
        fd->rpy = 0;
        if (!upload(tex, 0, 0)) {
            ESP_LOGE(TAB5_TAG, "TTF glyph lock failed U+%04X", (unsigned)ch);
            return failFd();
        }
        return fd;
    }

    /* Space or empty outline: keep advance, no blit. */
    fd->rpidx = 0;
    fd->rpx = 0;
    fd->rpy = 0;
    if (textures_.empty()) {
        tex = Texture::create(renderer_, 2, 2);
        if (tex && tex->data()) {
            tex->enableBlendMode(true);
            textures_.push_back(tex);
        } else {
            delete tex;
        }
    }
    return fd;
}

}
