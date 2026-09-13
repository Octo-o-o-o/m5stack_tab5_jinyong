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

/* Overlay: skip the 1919x960 ARGB minimap when show_minimap is false. */

#include "globalmap.hh"

#include "colorpalette.hh"
#include "window.hh"
#include "content/grpdata.hh"
#include "world/savedata.hh"
#include "util/file.hh"
#include "util/random.hh"
#include "core/config.hh"
#include "esp_log.h"
#include "tab5_platform.h"
#include <cstring>
#include <stdexcept>

namespace hojy::scene {

enum {
    GlobalMapWidth = 480,
    GlobalMapHeight = 480,
};

GlobalMap::GlobalMap(Renderer *renderer, int ix, int iy, int width, int height, std::pair<int, int> scale):
    MapWithEvent(renderer, ix, iy, width, height, scale),
    drawingTerrainTex2_(Texture::create(renderer_, auxWidth_, auxHeight_)) {
    drawingTerrainTex2_->enableBlendMode(true);
    if (core::config.showMinimap()) {
        miniMapTex_ = Texture::create(renderer_, 2 * (GlobalMapWidth + GlobalMapHeight - 1) + 1,
                                      GlobalMapWidth + GlobalMapHeight - 1 + 1);
        if (miniMapTex_) {
            miniMapTex_->enableBlendMode(true);
        }
    }
    mapWidth_ = GlobalMapWidth;
    mapHeight_ = GlobalMapHeight;
    ::hojy::content::GrpData::loadData("MMAP", texData_);
    renderer_->enableLinear();
    {
        /* The six cloud sprites used to get their own TextureMgr, i.e. a whole
         * 512x512 atlas page (1.05 MB) for ~60 KB of pixels. Pack them into the
         * map's own manager instead: cloud ids are 0..5 and character sprites
         * start at 2501, so the id spaces do not collide. */
        ::hojy::content::GrpData::DataSet dset;
        if (::hojy::content::GrpData::loadData("CLOUD", dset)) {
            textureMgr_.loadFromRLE(dset);
            cloudCount_ = static_cast<int>(dset.size());
        }
    }
    renderer_->enableLinear(false);
    if (texData_.empty() || texData_[0].size() < sizeof(uint16_t) * 4) {
        ESP_LOGE(TAB5_TAG, "MMAP tex header missing entries=%u",
                 (unsigned)texData_.size());
        throw std::runtime_error("MMAP header");
    }
    {
        const auto *arr = reinterpret_cast<const uint16_t*>(texData_[0].data());
        cellWidth_ = arr[0];
        cellHeight_ = arr[1];
        offsetX_ = arr[2];
        offsetY_ = arr[3];
    }
    int cellDiffX = cellWidth_ / 2;
    int cellDiffY = cellHeight_ / 2;
    auto size = mapWidth_ * mapHeight_;
    /* cellInfo_ is the largest single block this object needs. Claim it before
     * the five 460 KB .002 vectors carve up the remaining PSRAM, otherwise the
     * allocation that decides whether the player can leave a submap at all is
     * the one made against the most fragmented heap. */
    cellInfo_.resize(size);
    ESP_LOGI(TAB5_TAG, "GlobalMap cellInfo %d cells x %u B = %u KB",
             (int)size, (unsigned)sizeof(CellInfo),
             (unsigned)((size_t)size * sizeof(CellInfo) / 1024u));

    /* The five .002 tables are 0.45 MB each. Upstream holds all five at once;
     * earth/surface are only needed for the first pass, so load and drop them
     * before the building tables come in. Two fewer live blocks at the moment
     * this object is being built is the difference between fitting and not. */
    {
        std::vector<std::uint16_t> earth_, surface_;
        util::File::getFileContent(core::config.dataFilePath("EARTH.002"), earth_);
        util::File::getFileContent(core::config.dataFilePath("SURFACE.002"), surface_);
        earth_.resize(size);
        surface_.resize(size);
        int p2 = 0;
        for (int j = 0; j < mapHeight_; ++j) {
            for (int i = 0; i < mapWidth_; ++i, ++p2) {
                auto &ci = cellInfo_[p2];
                auto n = static_cast<std::uint16_t>(earth_[p2] >> 1);
                if (n) {
                    if (n == 419 || n >= 306 && n <= 335) {
                        ci.type = 1;
                    } else if (n >= 179 && n <= 181 || n >= 253 && n <= 335 || n >= 508 && n <= 511) {
                        ci.type = 1;
                        ci.canWalk = true;
                    } else if (n > 0) {
                        ci.canWalk = true;
                    }
                }
                ci.earthId = n;
                ci.surfaceId = surface_[p2] >> 1;
            }
        }
    }

    util::File::getFileContent(core::config.dataFilePath("BUILDING.002"), building_);
    util::File::getFileContent(core::config.dataFilePath("BUILDX.002"), buildx_);
    util::File::getFileContent(core::config.dataFilePath("BUILDY.002"), buildy_);
    building_.resize(size);
    buildx_.resize(size);
    buildy_.resize(size);

    int pos = 0;
    for (int j = 0; j < mapHeight_; ++j) {
        for (int i = 0; i < mapWidth_; ++i, ++pos) {
            auto &ci = cellInfo_[pos];
            auto &n1 = building_[pos];
            n1 >>= 1;
            if (n1 > 0) {
                ci.canWalk = false;
                if (n1 >= 1008 && n1 <= 1164 || n1 >= 1214 && n1 <= 1238) {
                    ci.type = 2;
                }
                if (n1 && n1 < texData_.size() && !texData_[n1].empty()) {
                    const auto *arr = reinterpret_cast<const uint16_t *>(texData_[n1].data());
                    auto deltaY = (arr[0] + 35) / 36 / 2;
                    if (n1 >= 1176 && n1 <= 1182 || n1 == 1352) {
                        deltaY = arr[1] / 18 + 1;
                    }
                    if (deltaY) {
                        /* Upstream indexes (j-deltaY, i-deltaY) unchecked. Near
                         * the top-left corner that is a negative index into
                         * cellInfo_, i.e. a write before the vector -- silent
                         * heap corruption that only shows up later. */
                        const int cj = j - deltaY, cnx = i - deltaY;
                        if (cj >= 0 && cnx >= 0) {
                            auto &ci2 = cellInfo_[cj * mapWidth_ + cnx];
                            ci2.buildingId = n1;
                            ci2.buildingDeltaY = std::int16_t(deltaY * cellHeight_);
                        }
                    } else {
                        ci.buildingId = n1;
                        ci.buildingDeltaY = 0;
                    }
                }
            }
        }
    }
    resetTime();
    updateMainCharTexture();
}

GlobalMap::~GlobalMap() {
    delete drawingTerrainTex2_;
}

void GlobalMap::load() {
    int pos = 0;
    std::map<std::int16_t, std::uint32_t> colorMap;
    const auto *colors = gNormalPalette.colors();
    int pitch = 0;
    std::uint32_t *pixels = miniMapTex_ ? miniMapTex_->lock(pitch) : nullptr;
    int miniMapStartX = 2 * (mapHeight_ - 1) + 1;
    int miniMapStartY = 1;
    for (int j = 0; j < mapHeight_; ++j) {
        for (int i = 0; i < mapWidth_; ++i, ++pos) {
            int mmx = miniMapStartX + (i - j) * 2;
            int mmy = miniMapStartY + (i + j);
            auto &ci = cellInfo_[pos];
            std::uint32_t c;
            if (!ci.canWalk || (buildx_[pos] != 0 && building_[buildy_[pos] * mapWidth_ + buildx_[pos]] != 0)) {
                c = 0x202020U;
            } else {
                auto n = ci.earthId;
                auto ite = colorMap.find(n);
                if (ite == colorMap.end()) {
                    c = Texture::calcRLEAvgColor(texData_[ci.earthId], colors);
                    colorMap[n] = c;
                } else {
                    c = ite->second;
                }
            }
            if (pixels != nullptr) {
                c |= 0xE0000000u;
                int mmoff = mmx + mmy * pitch;
                pixels[mmoff - 1] = c;
                pixels[mmoff] = c;
                pixels[mmoff + 1] = c;
                pixels[mmoff - pitch] = c;
            }
        }
    }
    auto subMapSz = ::hojy::world::state::gSaveData.subMapInfo.size();
    for (size_t i = 0; i < subMapSz; ++i) {
        const auto &smi = ::hojy::world::state::gSaveData.subMapInfo[i];
        auto ex = smi->globalEnterX1;
        auto ey = smi->globalEnterY1;
        subMapEntries_[std::make_pair(ex, ey)] = i;
        const std::uint32_t mark = 0xE040C0C0;
        if (pixels != nullptr) {
            int mmx = miniMapStartX + (ex - ey) * 2;
            int mmy = miniMapStartY + (ex + ey);
            int mmoff = mmx + mmy * pitch;
            pixels[mmoff - 1] = mark;
            pixels[mmoff] = mark;
            pixels[mmoff + 1] = mark;
            pixels[mmoff - pitch] = mark;
        }

        ex = smi->globalEnterX2;
        if (ex >= 0) {
            ey = smi->globalEnterY2;
            subMapEntries_[std::make_pair(ex, ey)] = i;
            if (pixels != nullptr) {
                int mmx = miniMapStartX + (ex - ey) * 2;
                int mmy = miniMapStartY + (ex + ey);
                int mmoff = mmx + mmy * pitch;
                pixels[mmoff - 1] = mark;
                pixels[mmoff] = mark;
                pixels[mmoff + 1] = mark;
                pixels[mmoff - pitch] = mark;
            }
        }
    }
    if (miniMapTex_) {
        miniMapTex_->unlock();
    }

    /* tryMove() and the minimap both test
     *   buildx_[pos] && building_[buildy_[pos] * mapWidth_ + buildx_[pos]]
     * which is a pure function of files that never change after load. Fold it
     * into canWalk once and release the three 0.44 MB lookup tables; they are
     * 1.32 MB of PSRAM held for the whole session otherwise. */
    pos = 0;
    for (int j = 0; j < mapHeight_; ++j) {
        for (int i = 0; i < mapWidth_; ++i, ++pos) {
            const auto bx = buildx_[pos];
            if (bx != 0 && building_[buildy_[pos] * mapWidth_ + bx] != 0) {
                cellInfo_[pos].canWalk = false;
            }
        }
    }
    building_.clear(); building_.shrink_to_fit();
    buildx_.clear();   buildx_.shrink_to_fit();
    buildy_.clear();   buildy_.shrink_to_fit();

    onShip_ = cellInfo_[currY_ * mapWidth_ + currX_].type == 1;
    if (core::config.shipLogicEnabled()) {
        showShip(!onShip_);
    }
}

void GlobalMap::update() {
    MapWithEvent::update();
    for (int i = 0; i < 3; ++i) {
        auto &c = cloud_[i];
        if (!c) {
            if (util::gRandom(2500)) { continue; }
            if (cloudCount_ <= 0) { continue; }
            c = textureMgr_[util::gRandom(cloudCount_)];
            if (!c) { continue; }
            cloudStartX_[i] = cameraX_; cloudStartY_[i] = cameraY_;
            cloudX_[i] = -width_ * 3 / 5;
            cloudY_[i] = int(util::gRandom(int(auxHeight_) + height_ / 10) + height_ / 20);
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!cloud_[i]) { continue; }
        ++cloudX_[i];
        const int cellDiffX = cellWidth_ / 2;
        const int cloudcx = cloudStartX_[i] - cameraX_;
        const int cloudcy = cloudStartY_[i] - cameraY_;
        const int cloudx = (cloudcx - cloudcy) * cellDiffX * scale_.first / scale_.second
            + cloudX_[i] / 2;
        if (cloudx > width_ * 5 / 2) {
            cloud_[i] = nullptr;
        }
    }
}

void GlobalMap::render() {
    Map::render();
    if (drawDirty_) {
        drawDirty_ = false;
        int cellDiffX = cellWidth_ / 2;
        int cellDiffY = cellHeight_ / 2;
        int camX = cameraX_, camY = cameraY_;
        int nx = int(auxWidth_) / 2 + cellWidth_ * 2;
        int ny = int(auxHeight_) / 2 + cellHeight_ * 2;
        int ocx = (nx / cellDiffX + ny / cellDiffY) / 2;
        int ocy = (ny / cellDiffY - nx / cellDiffX) / 2;
        int wcount = nx * 2 / cellWidth_;
        int hcount = (ny * 2 + 4 * cellHeight_) / cellDiffY;
        int aheight = int(auxHeight_);
        int otx = int(auxWidth_) / 2 - (ocx - ocy) * cellDiffX;
        int oty = aheight / 2 + cellDiffY - (ocx + ocy) * cellDiffY;
        ocx = camX - ocx; ocy = camY - ocy;
        int delta = -mapWidth_ + 1;
        int cx = ocx, cy = ocy, tx = otx, ty = oty;
        const auto *colors = gNormalPalette.colors();
        auto *curTex = drawingTerrainTex_;
        int pitch;
        std::uint32_t *pixels = curTex->lock(pitch);
        memset(pixels, 0, pitch * auxHeight_ * sizeof(std::uint32_t));
        for (int j = hcount; j; --j) {
            int x = cx, y = cy;
            int dx = tx;
            int offset = y * mapWidth_ + x;
            for (int i = wcount; i; --i, dx += cellWidth_, offset += delta, ++x, --y) {
                if (x < 0 || x >= GlobalMapWidth || y < 0 || y >= GlobalMapHeight) {
                    Texture::renderRLE(texData_[0], colors, pixels, pitch, aheight, dx, ty);
                    continue;
                }
                auto &ci = cellInfo_[offset];
                Texture::renderRLE(texData_[ci.earthId], colors, pixels, pitch, aheight, dx, ty);
                if (ci.surfaceId) {
                    Texture::renderRLE(texData_[ci.surfaceId], colors, pixels, pitch, aheight, dx, ty);
                }
            }
            if (j % 2) {
                ++cx;
                tx += cellDiffX;
                ty += cellDiffY;
            } else {
                ++cy;
                tx -= cellDiffX;
                ty += cellDiffY;
            }
        }
        cx = ocx; cy = ocy; tx = otx; ty = oty;
        int charX = currX_, charY = currY_;
        for (int j = hcount; j; --j) {
            int x = cx, y = cy;
            int dx = tx;
            int offset = y * mapWidth_ + x;
            for (int i = wcount; i; --i, dx += cellWidth_, offset += delta, ++x, --y) {
                if (x < 0 || x >= GlobalMapWidth || y < 0 || y >= GlobalMapHeight) {
                    continue;
                }
                auto &ci = cellInfo_[offset];
                if (ci.buildingId) {
                    Texture::renderRLE(texData_[ci.buildingId], colors, pixels, pitch, aheight, dx, ty + ci.buildingDeltaY);
                }
                if (x == charX && y == charY) {
                    curTex->unlock();
                    curTex = drawingTerrainTex2_;
                    pixels = curTex->lock(pitch);
                    memset(pixels, 0, pitch * auxHeight_ * sizeof(std::uint32_t));
                }
            }
            if (j % 2) {
                ++cx;
                tx += cellDiffX;
                ty += cellDiffY;
            } else {
                ++cy;
                tx -= cellDiffX;
                ty += cellDiffY;
            }
        }
        curTex->unlock();
        int miniMapStartX = 2 * (mapHeight_ - 1) + 1 + 2 * (cameraX_ - cameraY_);
        int miniMapStartY = 1 + cameraX_ + cameraY_;
        miniMapAuxX_ = miniMapStartX - miniMapAuxW_ / 2;
        miniMapAuxY_ = miniMapStartY - miniMapAuxH_ / 2;
    }
    renderer_->clear(0, 0, 0, 255);
    renderer_->renderTexture(drawingTerrainTex_, x_, y_, width_, height_, 0, 0, auxWidth_, auxHeight_);
    renderChar();
    renderer_->renderTexture(drawingTerrainTex2_, x_, y_, width_, height_, 0, 0, auxWidth_, auxHeight_);
    for (int i = 0; i < 3; ++i) {
        auto &c = cloud_[i];
        if (!c) {
            continue;
        }
        int cellDiffX = cellWidth_ / 2;
        int cellDiffY = cellHeight_ / 2;
        int cloudcx = cloudStartX_[i] - cameraX_, cloudcy = cloudStartY_[i] - cameraY_;
        int cloudx = (cloudcx - cloudcy) * cellDiffX * scale_.first / scale_.second + cloudX_[i] / 2;
        int cloudy = (cloudcx + cloudcy) * cellDiffY * scale_.first / scale_.second + cloudY_[i];
        renderer_->renderTexture(c, cloudx, cloudy, scale_);
    }
    showMiniPanel();
}

void GlobalMap::showShip(bool show) {
    int shipX0 = ::hojy::world::state::gSaveData.baseInfo->shipX;
    int shipY0 = ::hojy::world::state::gSaveData.baseInfo->shipY;
    auto &ci = cellInfo_[shipY0 * mapWidth_ + shipX0];
    if (show) {
        int shipX1 = ::hojy::world::state::gSaveData.baseInfo->shipX1;
        int shipY1 = ::hojy::world::state::gSaveData.baseInfo->shipY1;
        ci.buildingId = 3715 + int(calcDirection(shipX1, shipY1, shipX0, shipY0)) * 4;
        ci.buildingDeltaY = 0;
    } else {
        ci.buildingId = 0;
    }
}

bool GlobalMap::tryMove(int x, int y, bool checkEvent) {
    auto ite = subMapEntries_.find(std::make_pair(std::int16_t(x), std::int16_t(y)));
    if (ite != subMapEntries_.end()) {
        auto *subMapInfo = ::hojy::world::state::gSaveData.subMapInfo[ite->second];
        if (subMapInfo->enterCondition == 1) {
            return true;
        }
        if (subMapInfo->enterCondition == 2) {
            bool allow = false;
            for (auto id: ::hojy::world::state::gSaveData.baseInfo->members) {
                if (id < 0) { continue; }
                /* TODO: get this limit value from Z.DAT? */
                auto *charInfo = ::hojy::world::state::gSaveData.charInfo[id];
                if (charInfo && charInfo->speed >= 70) {
                    allow = true;
                    break;
                }
            }
            if (!allow) {
                return true;
            }
        }
        gWindow->enterSubMap(ite->second, int(direction_));
        auto music = subMapInfo->enterMusic;
        if (music >= 0) {
            gWindow->playMusic(music);
        }
        return true;
    }
    auto offset = y * mapWidth_ + x;
    /* The buildx_/buildy_/building_ term is already folded into canWalk by
     * GlobalMap::load(); those vectors are released there. */
    if (!cellInfo_[offset].canWalk) {
        if (onShip_) {
            currMainCharFrame_ = (currMainCharFrame_ + 1) % 4;
        } else {
            currMainCharFrame_ = currMainCharFrame_ % 6 + 1;
        }
        return true;
    }
    bool lastOnShip = onShip_;
    if (cellInfo_[offset].type == 1) {
        if (core::config.shipLogicEnabled() && !lastOnShip) {
            if (::hojy::world::state::gSaveData.baseInfo->shipX != x ||
                ::hojy::world::state::gSaveData.baseInfo->shipY != y) {
                return true;
            }
        }
        onShip_ = true;
        currMainCharFrame_ = (currMainCharFrame_ + 1) % 4;
        ::hojy::world::state::gSaveData.baseInfo->shipX = x;
        ::hojy::world::state::gSaveData.baseInfo->shipY = y;
        ::hojy::world::state::gSaveData.baseInfo->shipX1 = currX_;
        ::hojy::world::state::gSaveData.baseInfo->shipY1 = currY_;
    } else {
        onShip_ = false;
        currMainCharFrame_ = currMainCharFrame_ % 6 + 1;
    }
    if (core::config.shipLogicEnabled() && lastOnShip != onShip_) { showShip(lastOnShip); }
    currX_ = x;
    currY_ = y;
    cameraX_ = x;
    cameraY_ = y;
    drawDirty_ = true;
    return true;
}

void GlobalMap::updateMainCharTexture() {
    if (onShip_) {
        mainCharTex_ = getOrLoadTexture(3715 + int(direction_) * 4 + currMainCharFrame_);
        return;
    }
    if (resting_) {
        mainCharTex_ = getOrLoadTexture(2529 + int(direction_) * 6 + currMainCharFrame_);
        return;
    }
    mainCharTex_ = getOrLoadTexture(2501 + int(direction_) * 7 + currMainCharFrame_);
}

void GlobalMap::resetTime() {
    if (onShip_) { return; }
    MapWithEvent::resetTime();
}

bool GlobalMap::checkTime() {
    if (onShip_) { return false; }
    return MapWithEvent::checkTime();
}

}
