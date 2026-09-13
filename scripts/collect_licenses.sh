#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Gather the license texts of everything in the release firmware into
# <out-dir> (see THIRD_PARTY_NOTICES.md). Run after building the game
# firmware, which downloads the managed components. Needs IDF_PATH.
#
#   scripts/collect_licenses.sh <out-dir>

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:?usage: scripts/collect_licenses.sh <out-dir>}"
hojy="${root}/third_party/HeroesOfJinYong"
managed="${root}/firmware/game/managed_components"

copy() {
    if [[ ! -f "$1" ]]; then
        echo "License file missing: $1" >&2
        exit 1
    fi
    cp "$1" "${out}/$2"
}

mkdir -p "${out}"
copy "${root}/LICENSE" "tab5_jinyong-LICENSE.txt"
copy "${root}/THIRD_PARTY_NOTICES.md" "THIRD_PARTY_NOTICES.md"
copy "${hojy}/LICENSE" "HeroesOfJinYong-LICENSE.txt"
copy "${hojy}/deps/fmt/LICENSE" "fmt-LICENSE.txt"
copy "${hojy}/deps/zita-resampler/COPYING" "zita-resampler-COPYING.txt"
copy "${IDF_PATH:?IDF_PATH is not set; source scripts/idf_env.sh}/LICENSE" "esp-idf-LICENSE.txt"

shopt -s nullglob
components=("${managed}"/*/)
if [[ ${#components[@]} -eq 0 ]]; then
    echo "No managed components under ${managed}; build the game firmware first." >&2
    exit 1
fi
for dir in "${components[@]}"; do
    name="$(basename "${dir}")"
    files=("${dir}"LICENSE* "${dir}"license*)
    if [[ ${#files[@]} -eq 0 ]]; then
        echo "No license file in ${dir}" >&2
        exit 1
    fi
    copy "${files[0]}" "${name#espressif__}-LICENSE.txt"
done
