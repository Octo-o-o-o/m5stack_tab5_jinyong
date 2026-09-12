#!/usr/bin/env bash
# Host-side data prep. Does not commit game files and does not flash.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"

usage() {
    cat <<'EOF'
Usage:
  ./prepare_game_data.sh --skeleton
  ./prepare_game_data.sh <original-game-dir> [output-dir] [font-file]

makedata writes config.toml + data/ into the output directory (default:
./local/sd_image/jinyong). Copy that tree onto a FAT32 microSD as /jinyong/.

--skeleton prepares a Tab5-sized CJK subset font and empty save/config dirs
when you do not yet have the original game directory.

Do not git add the output.
EOF
}

out_default="${root}/local/sd_image/jinyong"
font_out="${root}/local/fonts_ready/chinese.otf"
strings_toml="${root}/third_party/HeroesOfJinYong/src/strings.toml"

ensure_fonttools() {
    local venv="${root}/local/.venv"
    if "${venv}/bin/python" -c "import fontTools" >/dev/null 2>&1; then
        echo "${venv}/bin/python"
        return 0
    fi
    python3 -m venv "${venv}"
    "${venv}/bin/pip" install --disable-pip-version-check -q fonttools
    echo "${venv}/bin/python"
}

default_system_font() {
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
    return 1
}

subset_font() {
    local src="$1"
    shift
    local py
    py="$(ensure_fonttools)"
    "${py}" "${root}/scripts/subset_cjk_font.py" "${src}" "${strings_toml}" "${font_out}" "$@"
}

write_skeleton() {
    local out="$1"
    mkdir -p "${out}/data/font" "${out}/music" "${out}/save" "${out}/config" "${out}/fonts"
    if [[ -f "${font_out}" ]]; then
        cp "${font_out}" "${out}/data/font/chinese.otf"
        cp "${font_out}" "${out}/fonts/chinese.otf"
    fi
    cat > "${out}/config/bringup.txt" <<'EOF'
tab5_jinyong prepared tree
EOF
    if [[ ! -f "${out}/config.toml" ]]; then
        cat > "${out}/config.toml" <<'EOF'
[main]
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
no_name_input = false
# default_name = "徐小俠"
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
sound_volume = 5
EOF
    fi
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 1
fi

if [[ "${1:-}" == "--skeleton" ]]; then
    src_font="$(default_system_font)"
    echo "Subsetting system font: ${src_font}"
    subset_font "${src_font}"
    write_skeleton "${out_default}"
    echo "Skeleton (gitignored): ${out_default}"
    echo "Still missing original game data. makedata was not run."
    exit 0
fi

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

src="$1"
out="${2:-${out_default}}"
font="${3:-}"

if [[ ! -d "${src}" ]]; then
    echo "Original game directory not found: ${src}" >&2
    exit 1
fi

hojy="${root}/third_party/HeroesOfJinYong"
if [[ ! -f "${hojy}/CMakeLists.txt" ]]; then
    echo "HeroesOfJinYong submodule missing. Run ./setup.sh" >&2
    exit 1
fi

if [[ -z "${font}" ]]; then
    if [[ ! -f "${font_out}" ]]; then
        subset_font "$(default_system_font)"
    fi
    font="${font_out}"
elif [[ "${font}" == *.ttc || "${font}" == *.TTC ]]; then
    echo "Source font is a .ttc; subsetting before makedata so the device does not load 50MB+"
    subset_font "${font}"
    font="${font_out}"
fi

mkdir -p "${out}"
"${root}/scripts/build_host_tools.sh"
makedata_bin="${root}/host/build/bin/makedata"
if [[ ! -x "${makedata_bin}" ]]; then
    echo "makedata binary not found at ${makedata_bin}" >&2
    exit 1
fi

# makedata writes <target>/config.toml and <target>/data/
"${makedata_bin}" "${src}" "${out}" "${font}"

python3 "${root}/scripts/patch_tab5_config.py" "${out}/config.toml"

# Dialogue is XOR Big5; names in RANGER/WAR.STA are plain Big5.
talk="${out}/data/TALK.GRP"
ranger="${out}/data/RANGER.GRP"
war="${out}/data/WAR.STA"
extras=()
if [[ -f "${talk}" ]]; then extras+=("${talk}"); fi
if [[ -f "${ranger}" ]]; then extras+=("${ranger}"); fi
if [[ -f "${war}" ]]; then extras+=("${war}"); fi
if [[ ${#extras[@]} -gt 0 ]]; then
    echo "Expanding CJK subset from $(printf '%s ' "${extras[@]##*/}")"
    subset_font "$(default_system_font)" "${extras[@]}"
    mkdir -p "${out}/data/font" "${out}/fonts"
    cp "${font_out}" "${out}/data/font/chinese.otf"
    cp "${font_out}" "${out}/fonts/chinese.otf"
fi

write_skeleton "${out}"

echo "Prepared (gitignored) tree: ${out}"
echo "Copy that tree onto the microSD as /jinyong/ when you are ready."
echo "Do not git add this output."
