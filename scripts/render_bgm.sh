#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Pre-render the game's XMI music to WAV. The firmware does not synthesise OPL
# itself; it streams these files, and a track without one plays silence.
#
#   scripts/render_bgm.sh <game-tree>        e.g. local/sd_image/jinyong
#
# Writes data/GAMExx.WAV next to each data/GAMExx.XMI: 22050 Hz stereo, DOSBox
# OPL3, first 45 s. Existing WAVs are overwritten.

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
adl="${root}/third_party/HeroesOfJinYong/deps/libADLMIDI"
out="${root}/local/host"
rate=22050
seconds=45

if [[ $# -ne 1 || "$1" == -* ]]; then
    sed -n '2,8s/^# \{0,1\}//p' "$0" >&2
    exit 2
fi
tree="$1"

if [[ ! -f "${adl}/CMakeLists.txt" ]]; then
    echo "libADLMIDI submodule missing. Run ./setup.sh" >&2
    exit 1
fi

shopt -s nullglob
xmis=("${tree}"/data/GAME*.XMI)
if [[ ${#xmis[@]} -eq 0 ]]; then
    echo "No GAME*.XMI under ${tree}/data" >&2
    exit 1
fi

mkdir -p "${out}/bin"
if [[ ! -f "${out}/adlmidi/libADLMIDI.a" ]]; then
    cmake="$(bash "${root}/scripts/find_host_cmake.sh")" || { echo "cmake not found" >&2; exit 1; }
    # Only the DOSBox emulator is used; skip building the heavy ones.
    "${cmake}" -S "${adl}" -B "${out}/adlmidi" -DCMAKE_BUILD_TYPE=Release \
        -DlibADLMIDI_STATIC=ON -DlibADLMIDI_SHARED=OFF \
        -DUSE_NUKED_EMULATOR=OFF -DUSE_OPAL_EMULATOR=OFF -DUSE_JAVA_EMULATOR=OFF \
        -DUSE_ESFMU_EMULATOR=OFF -DUSE_MAME_EMULATOR=OFF -DUSE_YMFM_EMULATOR=OFF \
        > "${out}/adlmidi-configure.log"
    "${cmake}" --build "${out}/adlmidi" --target ADLMIDI_static --parallel \
        > "${out}/adlmidi-build.log"
fi

mkdir -p "${out}/bin"
"${CC:-cc}" -O2 -c -I "${adl}/include" "${root}/scripts/xmi_to_wav.c" -o "${out}/xmi_to_wav.o"
"${CXX:-c++}" "${out}/xmi_to_wav.o" "${out}/adlmidi/libADLMIDI.a" -lm -o "${out}/bin/xmi_to_wav"

for xmi in "${xmis[@]}"; do
    "${out}/bin/xmi_to_wav" "${xmi}" "${xmi%.XMI}.WAV" "${rate}" "${seconds}"
done
