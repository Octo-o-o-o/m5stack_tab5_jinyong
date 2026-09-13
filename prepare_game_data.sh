#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Turn an original DOS game directory into the tree the Tab5 reads from its
# microSD card. Host-side only: never commits game files, never flashes.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
hojy="${root}/third_party/HeroesOfJinYong"
strings_toml="${hojy}/src/strings.toml"
out_default="${root}/local/sd_image/jinyong"

usage() {
    cat <<'EOF'
Usage:
  ./prepare_game_data.sh [--no-bgm] <original-game-dir> [output-dir] [font-file]
  ./prepare_game_data.sh --skeleton [output-dir] [font-file]

Converts the original game into output-dir (default ./local/sd_image/jinyong)
with upstream makedata, applies the Tab5 config, subsets a CJK font to the
characters the game uses, and pre-renders the music. Copy the whole output
directory onto a FAT32 microSD as /jinyong/.

  font-file   TTF/OTF/TTC to subset. Defaults to the macOS Heiti/Songti
              system font; required on other systems.
  --no-bgm    Skip rendering GAMExx.WAV; the game then runs without music.
  --skeleton  Only config.toml, the subset font and save/, for when the
              original game is not at hand yet.
EOF
}

skeleton=0
bgm=1
args=()
for arg in "$@"; do
    case "${arg}" in
        -h|--help) usage; exit 0 ;;
        --skeleton) skeleton=1 ;;
        --no-bgm) bgm=0 ;;
        -*) usage >&2; exit 2 ;;
        *) args+=("${arg}") ;;
    esac
done

if (( skeleton )); then
    src=""
    out="${args[0]:-${out_default}}"
    font="${args[1]:-}"
else
    if [[ ${#args[@]} -lt 1 ]]; then
        usage >&2
        exit 2
    fi
    src="${args[0]}"
    out="${args[1]:-${out_default}}"
    font="${args[2]:-}"
    if [[ ! -d "${src}" ]]; then
        echo "Original game directory not found: ${src}" >&2
        exit 1
    fi
fi

if [[ ! -f "${strings_toml}" ]]; then
    echo "HeroesOfJinYong submodule missing. Run ./setup.sh" >&2
    exit 1
fi

source_font() {
    if [[ -n "${font}" ]]; then
        if [[ ! -f "${font}" ]]; then
            echo "Font not found: ${font}" >&2
            return 1
        fi
        echo "${font}"
        return 0
    fi
    for f in \
        "/System/Library/Fonts/STHeiti Medium.ttc" \
        "/System/Library/Fonts/STHeiti Light.ttc" \
        "/System/Library/Fonts/Supplemental/Songti.ttc"
    do
        if [[ -f "${f}" ]]; then
            echo "${f}"
            return 0
        fi
    done
    echo "No default CJK font on this system; pass a TTF/OTF/TTC as font-file." >&2
    return 1
}

fonttools_python() {
    local venv="${root}/local/.venv"
    if ! "${venv}/bin/python" -c "import fontTools" >/dev/null 2>&1; then
        python3 -m venv "${venv}"
        "${venv}/bin/pip" install --disable-pip-version-check -q fonttools
    fi
    echo "${venv}/bin/python"
}

# subset_font <dest> [extra text sources...]
subset_font() {
    local dest="$1"
    shift
    "${py}" "${root}/scripts/subset_cjk_font.py" "${src_font}" "${strings_toml}" "${dest}" "$@"
}

src_font="$(source_font)"
py="$(fonttools_python)"
mkdir -p "${out}/data/font" "${out}/save"

if (( skeleton )); then
    subset_font "${out}/data/font/chinese.otf"
    if [[ ! -f "${out}/config.toml" ]]; then
        cp "${hojy}/src/config.toml" "${out}/config.toml"
        python3 "${root}/scripts/patch_tab5_config.py" "${out}/config.toml"
    fi
    echo "Skeleton ready (no game data, makedata was not run): ${out}"
    exit 0
fi

"${root}/scripts/build_host_tools.sh"

# makedata copies whatever font it is given into data/font/ and points
# config.toml at it, so hand it a small subset named chinese.otf rather than a
# 50 MB system collection.
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
subset_font "${tmp}/chinese.otf"
"${root}/local/host/bin/makedata" "${src}" "${out}" "${tmp}/chinese.otf"
python3 "${root}/scripts/patch_tab5_config.py" "${out}/config.toml"

# With the dialogue and name tables in place, subset again to every character
# they use. Dialogue is bit-inverted Big5; names are plain Big5.
extras=()
for f in TALK.GRP RANGER.GRP WAR.STA; do
    if [[ -f "${out}/data/${f}" ]]; then
        extras+=("${out}/data/${f}")
    fi
done
subset_font "${out}/data/font/chinese.otf" ${extras[@]+"${extras[@]}"}

if (( bgm )); then
    "${root}/scripts/render_bgm.sh" "${out}"
fi

echo "Prepared: ${out}"
echo "Copy the whole directory onto a FAT32 microSD as /jinyong/."
