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
 * Overlay of HeroesOfJinYong src/scene/nodewithcache.cc for ESP32-P4.
 * createAsTarget can return a Texture with null pixels after SMP/ALLSIN.
 * Do not enableBlendMode or setTarget on that object.
 */

#include "nodewithcache.hh"

#include "texture.hh"
#include "esp_log.h"
#include "tab5_platform.h"

namespace hojy::scene {

NodeWithCache::~NodeWithCache() {
    delete cache_;
}

void NodeWithCache::update() {
    rebuildCache();
}

void NodeWithCache::rebuildCache() {
    if (!cacheDirty_) { return; }
    makeCache();
    cacheDirty_ = false;
}

void NodeWithCache::makeCenter(int w, int h, int x, int y) {
    rebuildCache();
    Node::makeCenter(w, h, x, y);
}

void NodeWithCache::close() {
    delete cache_;
    cache_ = nullptr;
    Node::close();
}

void NodeWithCache::render() {
    if (!cache_ || !cache_->data()) { return; }
    renderer_->renderTexture(cache_, x_, y_, 0, 0, cache_->width(), cache_->height(), true);
}

void NodeWithCache::cacheBegin() {
    if (cache_ && !cache_->data()) {
        delete cache_;
        cache_ = nullptr;
    }
    if (!cache_) {
        cache_ = Texture::createAsTarget(renderer_, width_, height_);
        if (!cache_ || !cache_->data()) {
            ESP_LOGE(TAB5_TAG, "NodeWithCache target failed %dx%d", width_, height_);
            delete cache_;
            cache_ = nullptr;
            return;
        }
        cache_->enableBlendMode(true);
    }
    renderer_->setTargetTexture(cache_);
}

void NodeWithCache::cacheEnd() {
    renderer_->setTargetTexture(nullptr);
}

}
