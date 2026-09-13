/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstddef>

namespace hojy::scene {

/* Running total of ARGB8888 texture bytes handed out by Texture::create /
 * createAsTarget. On this device that is the biggest single class of PSRAM
 * use, and it does not show up in a heap summary as one identifiable item. */
std::size_t textureBytesTotal();

}
