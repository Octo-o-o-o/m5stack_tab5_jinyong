/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

/*
 * BGM is decoded on the hojy_bgm task (see window_audio.cc). A track is ~4 MB,
 * so anything that is about to ask PSRAM for a multi-megabyte contiguous block
 * has to let the loader finish first -- otherwise the door step that triggers
 * both a music change and the GlobalMap build competes with itself.
 */

#include <cstdint>

namespace hojy::scene {

void waitMusicLoaderIdle(std::uint32_t timeoutMs);

}
