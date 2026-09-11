#!/usr/bin/env bash
# Prepare this checkout for M1 bring-up builds. Does not flash, does not
# install a global toolchain, and does not touch other workspaces.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
cd "${root}"

if [[ ! -f "${root}/third_party/HeroesOfJinYong/src/main.cc" ]]; then
    echo "HeroesOfJinYong sources missing; initializing submodule..."
    git submodule update --init --recursive third_party/HeroesOfJinYong
fi

# shellcheck disable=SC1091
. "${root}/scripts/idf_env.sh"

echo "ESP-IDF: ${IDF_PATH}"
idf.py --version
echo "Submodule SHA: $(git -C "${root}/third_party/HeroesOfJinYong" rev-parse HEAD)"
echo "setup.sh: ready. Next: ./build.sh"
echo "Flash is disabled by default. Do not run ./flash.sh unless the owner explicitly accepts overwriting the work firmware."
