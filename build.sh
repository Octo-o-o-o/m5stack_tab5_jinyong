#!/usr/bin/env bash
# Configure and build the Tab5 game firmware (default) or bring-up.
# Does not flash.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
target="${TAB5_FIRMWARE:-game}"
proj="${root}/firmware/${target}"
log="${proj}/last-build.log"

if [[ ! -d "${proj}" ]]; then
    echo "Unknown firmware target: ${target} (${proj})" >&2
    exit 1
fi

# shellcheck disable=SC1091
. "${root}/scripts/idf_env.sh"

mkdir -p "${proj}"
{
    echo "=== tab5_jinyong ${target} build $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
    echo "IDF_PATH=${IDF_PATH}"
    idf.py --version
    cd "${proj}"
    if [[ ! -f sdkconfig ]]; then
        echo "No sdkconfig yet; setting target esp32p4"
        idf.py set-target esp32p4
    fi
    idf.py build
} 2>&1 | tee "${log}"

echo "Build log: ${log}"
