#!/usr/bin/env bash
# Configure and build the M1 Tab5 bring-up firmware. Does not flash.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
proj="${root}/firmware/bringup"
log="${proj}/last-build.log"

# shellcheck disable=SC1091
. "${root}/scripts/idf_env.sh"

mkdir -p "${proj}"
{
    echo "=== tab5_jinyong M1 build $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
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
