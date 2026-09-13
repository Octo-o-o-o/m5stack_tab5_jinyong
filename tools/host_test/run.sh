#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Headless host test of the Tab5 port's own game logic.
#
# Builds upstream HeroesOfJinYong on this machine, substitutes every
# firmware/game/components/hojy_core overlay, stubs the ESP-IDF headers, and
# drives the result against a real data tree (SD card or local image). No
# device, no window, no sound.
#
#   tools/host_test/run.sh "/Volumes/NO NAME/jinyong"
#   tools/host_test/run.sh local/sd_image/jinyong
#
# What it proves: new game -> walk the real path to the door -> the world map
# is actually built and entered. It also reports how much memory each stage
# asks for, including the largest single block -- the number that decides
# whether it fits into one contiguous PSRAM hole on the device.
#
# What it does NOT cover: the Tab5 platform layer (PPA present, DPI flip, I2C
# keyboard, ES8388) and real PSRAM fragmentation. Those still need the device.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
GAME_ROOT="${1:-$REPO/local/sd_image/jinyong}"
WORK="${HOST_TEST_WORK:-${TMPDIR:-/tmp}/tab5_jinyong_hosttest}"
SDL_PREFIX="${SDL2_PREFIX:-$(brew --prefix sdl2-compat 2>/dev/null || brew --prefix sdl2 2>/dev/null || echo /usr/local)}"
CMAKE="$(bash "$REPO/scripts/find_host_cmake.sh" || true)"

[ -d "$GAME_ROOT/data" ] || { echo "no data/ under $GAME_ROOT" >&2; exit 2; }
[ -n "$CMAKE" ] || { echo "no cmake found" >&2; exit 2; }

SRCCOPY="$WORK/hojy-host"
BUILD="$WORK/hostbuild"
RUN="$WORK/run"
mkdir -p "$WORK" "$RUN/save"

# ---------------------------------------------------------------- upstream copy
# The submodule is never modified. Two upstream portability defects have to be
# patched for a libc++ host build; both are unrelated to this port.
if [ ! -d "$SRCCOPY" ]; then
    echo "== copying upstream to $SRCCOPY =="
    rsync -a --exclude '.git' "$REPO/third_party/HeroesOfJinYong/" "$SRCCOPY/"
    # 1. vector<bool> yields a proxy reference on libc++, not bool.
    perl -0pi -e 's/for \(const auto value: enemy\) \{/for (const bool value: enemy) {/g' \
        "$SRCCOPY/src/battle/engine.cc"
    # 2. upstream rejects this data pack: Z.DAT frame table sums to 713 while
    #    EFT.IDX holds 714. The device overlay already tolerates it.
    perl -0pi -e 's/if \(index != dset\.size\(\)\) \{\n            return false;\n        \}/if (index > dset.size()) {\n            return false;\n        }/' \
        "$SRCCOPY/src/scene/effect.cc"
    # GetVersionFromGitTag needs a repo with a tag.
    git -C "$SRCCOPY" init -q .
    git -C "$SRCCOPY" -c user.email=t@t -c user.name=t commit --allow-empty -qm init
    git -C "$SRCCOPY" tag v1.0.0
fi

# ------------------------------------------------------------------ deps build
if [ ! -f "$BUILD/deps/fmt/libfmt.a" ]; then
    echo "== configuring deps =="
    printf 'find_package(SDL2 REQUIRED)\n' > "$WORK/inject.cmake"
    "$CMAKE" -S "$SRCCOPY" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_PREFIX_PATH="$SDL_PREFIX" \
        -DCMAKE_PROJECT_HeroesOfJinYong_INCLUDE="$WORK/inject.cmake" \
        -DCMAKE_CXX_FLAGS="-Dftello64=ftello -Dfseeko64=fseeko" > "$WORK/cfg.log" 2>&1
    "$CMAKE" --build "$BUILD" -j 8 --target fmt SDL2_gfx zita-resampler ADLMIDI_static \
        > "$WORK/deps.log" 2>&1
fi

# -------------------------------------------------- port logic + test compile
SRC="$SRCCOPY/src"
OV="$REPO/firmware/game/components/hojy_core"
# Same rule as hojy_core/CMakeLists.txt: an overlay replaces the upstream file
# with the same name. main.cc is the firmware entry; porttest.cc has its own.
OVERLAYS=()
for f in "$OV"/*.cc; do
    [ "$(basename "$f")" = "main.cc" ] || OVERLAYS+=("$(basename "$f")")
done

srcs=()
for d in app audio battle content core event scene util world; do
    for f in "$SRC/$d"/*.cc; do
        b=$(basename "$f"); skip=0
        for o in "${OVERLAYS[@]}"; do [ "$b" = "$o" ] && skip=1; done
        [ $skip -eq 0 ] && srcs+=("$f")
    done
done
for o in "${OVERLAYS[@]}"; do srcs+=("$OV/$o"); done

echo "== compiling ${#srcs[@]} sources (${OVERLAYS[*]} overlaid) =="
c++ -std=c++17 -O1 -g -fexceptions -frtti -w \
    -Dftello64=ftello -Dfseeko64=fseeko -DALLOW_ODD_WIDTH -DHOJY_VERSION='"host-test"' \
    -I"$HERE/shim" -I"$OV" -I"$SRC" -I"$SRC/app" -I"$SRC/audio" -I"$SRC/battle" \
    -I"$SRC/content" -I"$SRC/core" -I"$SRC/event" -I"$SRC/scene" -I"$SRC/util" -I"$SRC/world" \
    -I"$SRCCOPY/deps/SDL2_gfx" -I"$SRCCOPY/deps/fmt/include" \
    -I"$SRCCOPY/deps/libADLMIDI/include" -I"$SRCCOPY/deps/zita-resampler/source" \
    -I"$SDL_PREFIX/include/SDL2" \
    -o "$WORK/porttest" "$HERE/porttest.cc" "${srcs[@]}" \
    "$BUILD/deps/SDL2_gfx/libSDL2_gfx.a" "$BUILD/deps/fmt/libfmt.a" \
    "$BUILD/deps/libzita-resampler.a" "$BUILD/libADLMIDI.a" \
    -L"$SDL_PREFIX/lib" -lSDL2

# ------------------------------------------------------------------- run setup
cat > "$RUN/config.toml" <<EOF
[main]
pre_path = "$GAME_ROOT/"
data_path = ["data"]
music_path = "data"
sound_path = "data"
save_path = "save"
fonts = "data/font/chinese.otf"
ship_logic_enabled = true

[window]
width = 640
height = 480
show_fps = false
limit_fps = 30

[ui]
simplified_chinese = false
no_name_input = true
show_potential = true
show_map_mini_panel = false
show_minimap = false
scale = 2.0
animation_speed = 1.0
fade_speed = 1.0
window_border = 8

[audio]
opl_emulator = "dosbox"
sample_rate = 22050
sample_format = "I16"
music_volume = 5
sound_volume = 0
EOF

echo "== generating the walk path from the real map data =="
PATH_OUT="$RUN/walkpath.txt" python3 "$HERE/probe_reach.py" "$GAME_ROOT" | tail -4

# Keep the slot-1 files the previous run left on the card: porttest reads that
# slot back, writes it out again, and the two must be byte for byte the same.
SAVE_REF="$WORK/save_ref"
rm -rf "$SAVE_REF"; mkdir -p "$SAVE_REF"
cp "$GAME_ROOT"/save/R1.* "$GAME_ROOT"/save/S1.* "$GAME_ROOT"/save/D1.* "$SAVE_REF/" 2>/dev/null || true

echo "== running =="
cd "$RUN"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOST_TEST_SAVE_REF="$SAVE_REF" "$WORK/porttest"
