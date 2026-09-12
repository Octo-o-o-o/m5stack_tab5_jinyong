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
