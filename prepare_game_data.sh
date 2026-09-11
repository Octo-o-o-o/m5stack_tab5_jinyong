#!/usr/bin/env bash
# Host-side data prep wrapper. Does not commit game files and does not
# copy onto a microSD unless the caller names an output directory.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"

usage() {
    cat <<'EOF'
Usage:
  ./prepare_game_data.sh <original-game-dir> [output-dir] [font-file]

Builds HeroesOfJinYong makedata on the host (if cmake/c++ are available),
then writes the merged runtime tree under output-dir (default: ./local/sd_image/jinyong).

That output is gitignored. Copy it onto a FAT32 microSD as /jinyong/ yourself.

Required later (not done by this script):
  /jinyong/data/
  /jinyong/music/
  /jinyong/save/
  /jinyong/config/
  /jinyong/fonts/
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || $# -lt 1 ]]; then
    usage
    exit 1
fi

src="$1"
out="${2:-${root}/local/sd_image/jinyong}"
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

host_build="${root}/host/build"
mkdir -p "${host_build}" "${out}/data" "${out}/music" "${out}/save" "${out}/config" "${out}/fonts"

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake not found. Install cmake, then re-run." >&2
    echo "Would write prepared data to: ${out}" >&2
    exit 1
fi

cmake -S "${hojy}" -B "${host_build}" -DBUILD_TOOLS=ON -DBUILD_TESTING=OFF
cmake --build "${host_build}" --target makedata mergepic

makedata_bin="${host_build}/bin/makedata"
if [[ ! -x "${makedata_bin}" ]]; then
    echo "makedata binary not found at ${makedata_bin}" >&2
    exit 1
fi

if [[ -n "${font}" ]]; then
    "${makedata_bin}" "${src}" "${out}/data" "${font}"
else
    echo "No font file given. makedata still needs a font path as its third argument." >&2
    echo "Example: ./prepare_game_data.sh /path/to/original ${out} /path/to/chinese.otf" >&2
    exit 1
fi

echo "Prepared (gitignored) tree: ${out}"
echo "Copy that tree onto the microSD as /jinyong/ when you are ready."
echo "Do not git add this output."
