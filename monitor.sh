#!/usr/bin/env bash
# Serial monitor only. Does not flash or erase.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
proj="${root}/firmware/${TAB5_FIRMWARE:-game}"
port="${ESPPORT:-/dev/cu.usbmodem1101}"

if [[ "${port}" == "/dev/cu.usbmodem01" ]]; then
    echo "Refusing port /dev/cu.usbmodem01" >&2
    exit 2
fi

# shellcheck disable=SC1091
. "${root}/scripts/idf_env.sh"

cd "${proj}"
export ESPPORT="${port}"
idf.py -p "${port}" monitor
