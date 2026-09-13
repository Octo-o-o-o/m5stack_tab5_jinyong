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

#include "audio/channelmidi.hh"

namespace hojy::audio {

ChannelMIDI::ChannelMIDI(Mixer *mixer, const std::string &filename) : Channel(mixer, filename)
{
    ok_ = false;
}

ChannelMIDI::~ChannelMIDI() = default;

void ChannelMIDI::load(const std::string &)
{
    ok_ = false;
}

void ChannelMIDI::reset() {}

void ChannelMIDI::setRepeat(bool r)
{
    Channel::setRepeat(r);
}

size_t ChannelMIDI::readPCMData(const void **, size_t, bool)
{
    return 0;
}

}
