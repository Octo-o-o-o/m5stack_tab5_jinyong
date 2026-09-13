#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fetch the sources this checkout uses and check the ESP-IDF install.
# Does not install a toolchain and does not flash.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
hojy="${root}/third_party/HeroesOfJinYong"

# Only the nested dependencies this port uses: fmt for the firmware and
# libADLMIDI for pre-rendering BGM. Upstream's SDL2 and soxr are not needed.
git -C "${root}" submodule update --init third_party/HeroesOfJinYong
git -C "${hojy}" submodule update --init deps/fmt deps/libADLMIDI
echo "HeroesOfJinYong: $(git -C "${hojy}" rev-parse --short HEAD)"

# Preparing the SD card and flashing a prebuilt release do not need ESP-IDF;
# only ./build.sh, ./flash.sh and ./monitor.sh do.
if (
    # shellcheck source=scripts/idf_env.sh
    . "${root}/scripts/idf_env.sh" && echo "ESP-IDF: ${IDF_PATH}" && idf.py --version
); then
    echo "Ready. Next: ./build.sh"
else
    echo "ESP-IDF v5.5.5 not found. Fine for prepare_game_data.sh and prebuilt firmware;" >&2
    echo "install it (or set IDF_EXPORT) before ./build.sh." >&2
fi
