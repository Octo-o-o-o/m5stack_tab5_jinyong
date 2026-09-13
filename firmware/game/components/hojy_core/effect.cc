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
 * Overlay of HeroesOfJinYong src/scene/effect.cc — same load logic, with
 * UART reasons so a silent ready_=false is diagnosable on device.
 */

#include "effect.hh"

#include "colorpalette.hh"
#include "content/grpdata.hh"
#include "content/factors.hh"
#include "esp_log.h"
#include "tab5_platform.h"

#include <cstddef>
#include <new>
#include <utility>

namespace hojy::scene {

Effect gEffect;

bool Effect::load(const std::string &filename) {
    try {
        ::hojy::content::GrpData::DataSet dset;
        if (!::hojy::content::GrpData::loadData(filename, dset)) {
            ESP_LOGE(TAB5_TAG, "EFT GrpData::loadData(%s) failed", filename.c_str());
            return false;
        }
        auto effectSz = ::hojy::content::gFactors.effectFrames.size();
        ESP_LOGI(TAB5_TAG, "EFT grp entries=%u factor groups=%u",
                 (unsigned)dset.size(), (unsigned)effectSz);
        std::vector<std::vector<std::string>> loaded(effectSz);
        size_t index = 0;
        for (size_t i = 0; i < effectSz; ++i) {
            auto &data = loaded[i];
            const auto frameCount = ::hojy::content::gFactors.effectFrames[i];
            if (frameCount < 0
                || static_cast<std::size_t>(frameCount) > dset.size() - index) {
                ESP_LOGE(TAB5_TAG, "EFT frame overflow i=%u count=%d index=%u dset=%u",
                         (unsigned)i, (int)frameCount, (unsigned)index, (unsigned)dset.size());
                return false;
            }
            data.assign(dset.begin() + static_cast<std::ptrdiff_t>(index),
                        dset.begin() + static_cast<std::ptrdiff_t>(index + frameCount));
            index += static_cast<std::size_t>(frameCount);
        }
        if (index > dset.size()) {
            ESP_LOGE(TAB5_TAG, "EFT used=%u past dset=%u",
                     (unsigned)index, (unsigned)dset.size());
            return false;
        }
        if (index < dset.size()) {
            /* This LEGEND pack: Z.DAT frame table sums to 713, EFT.IDX has 714. */
            ESP_LOGW(TAB5_TAG, "EFT unused trailing records %u (ignored)",
                     (unsigned)(dset.size() - index));
        }
        effectTexData_ = std::move(loaded);
        ESP_LOGI(TAB5_TAG, "EFT loaded");
        return true;
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "EFT bad_alloc");
        return false;
    }
}

const std::vector<std::string> &Effect::operator[](std::int16_t index) const {
    if (index < 0 || index >= effectTexData_.size()) {
        static const std::vector<std::string> dummy;
        return dummy;
    }
    return effectTexData_[index];
}

}
