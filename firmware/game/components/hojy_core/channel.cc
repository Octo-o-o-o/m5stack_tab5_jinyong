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

#include "channel.hh"

#include <util/file.hh>
#include <cstring>
#include <map>
#include <mutex>

namespace hojy::audio {

namespace {

constexpr std::size_t kCacheLimit = 256 * 1024;

static std::map<std::string, std::vector<std::uint8_t>> dataCache_;
/* BGM is loaded on the hojy_bgm task while the game thread still loads sound
 * effects, so this map has two writers. */
static std::mutex dataCacheMutex_;

bool loadFileBytes(const std::string &filename, std::vector<std::uint8_t> &out)
{
    {
        std::scoped_lock lk(dataCacheMutex_);
        auto it = dataCache_.find(filename);
        if (it != dataCache_.end() && !it->second.empty()) {
            out = it->second;
            return true;
        }
    }
    std::vector<std::uint8_t> loaded;
    if (!util::File::getFileContent(filename, loaded) || loaded.empty()) {
        return false;
    }
    if (loaded.size() <= kCacheLimit) {
        std::scoped_lock lk(dataCacheMutex_);
        dataCache_[filename] = loaded;
    }
    out = std::move(loaded);
    return true;
}

}

Channel::Channel(Mixer *mixer, const std::string &filename)
    : sampleRateOut_(mixer->sampleRate())
    , typeOut_(mixer->dataType())
{
    ok_ = loadFileBytes(filename, data_);
}

Channel::Channel(Mixer *mixer, const void *data, size_t size)
    : sampleRateOut_(mixer->sampleRate())
    , typeOut_(mixer->dataType())
    , ok_(size > 0 && data != nullptr)
{
    if (!ok_) {
        return;
    }
    data_.resize(size);
    memcpy(data_.data(), data, size);
}

void Channel::load(const std::string &filename)
{
    resampler_.reset();
    data_.clear();
    ok_ = loadFileBytes(filename, data_);
}

size_t Channel::readData(void *data, size_t size)
{
    if (resampler_) {
        return resampler_->read(data, size);
    }
    const void *pcmdata;
    auto res = readPCMData(&pcmdata, size, true);
    if (res) {
        memcpy(data, pcmdata, res);
    }
    return res;
}

void Channel::start()
{
    if (sampleRateIn_ != sampleRateOut_) {
#if defined(USE_SOXR)
        resampler_ = std::make_unique<Resampler>(2, sampleRateIn_, sampleRateOut_, typeIn_, typeOut_);
        resampler_->setInputCallback([this](const void **data, size_t size) -> size_t {
            return readPCMData(data, size, false);
        });
#else
        resampler_ = std::make_unique<Resampler>(2, sampleRateIn_, sampleRateOut_, Mixer::F32, Mixer::F32);
        resampler_->setInputCallback([this](const void **data, size_t size) -> size_t {
            return readPCMData(data, size, true);
        });
#endif
    }
    /* WAV already decoded into buffer_. Do not keep the file bytes. */
    data_.clear();
    data_.shrink_to_fit();
}

}
