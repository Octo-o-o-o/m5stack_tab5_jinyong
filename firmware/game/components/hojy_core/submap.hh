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

#pragma once

/*
 * Overlay of HeroesOfJinYong src/scene/submap.hh for ESP32-P4: adds
 * releaseTiles(). Safe because only this repo's submap.cc and window.cc
 * include it (upstream's own submap.cc/window.cc are excluded from the build,
 * and world/submap.hh is a different header resolved from its own directory).
 */

#include "mapwithevent.hh"

#include <set>

namespace hojy::scene {

class SubMap final: public MapWithEvent {
    struct CellInfo {
        std::int16_t earthId = 0, buildingId = 0, decorationId = 0, eventId = 0;
        std::int16_t buildingDeltaY = 0, decorationDeltaY = 0;
        bool blocked = false;
    };
public:
    SubMap(Renderer *renderer, int x, int y, int width, int height, std::pair<int, int> scale);
    ~SubMap() override;

    bool load(std::int16_t subMapId);
    /* Give back the SDX/SMP tile set, the packed sprite atlas and the cell
     * table. The world map cannot coexist with them on 32 MB of PSRAM, so the
     * two sides are made mutually exclusive; the next load() re-reads SMP. */
    void releaseTiles();
    void forceMainCharTexture(std::int16_t id);

    void render() override;
    void handleKeyInput(Key key) override;

protected:
    bool tryMove(int x, int y, bool checkEvent) override;
    void updateMainCharTexture() override;
    void setCellTexture(int x, int y, int layer, std::int16_t tex) override;
    void frameUpdate() override;

private:
    std::int16_t charHeight_ = 0;
    std::vector<CellInfo> cellInfo_;
    Texture *drawingTerrainTex2_ = nullptr;
    std::set<std::int16_t> subMapLoaded_;
    std::vector<std::int16_t> eventLoop_, eventDelay_;
};

}
