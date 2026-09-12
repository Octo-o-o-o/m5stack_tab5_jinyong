#!/usr/bin/env bash
# Flash overwrites whatever firmware is already on the Tab5.
# Require an explicit env var so a stray ./flash.sh cannot do that.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
proj="${root}/firmware/${TAB5_FIRMWARE:-game}"
port="${ESPPORT:-/dev/cu.usbmodem1101}"

if [[ "${TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE:-}" != "1" ]]; then
    cat <<'EOF' >&2
Refusing to flash.

This replaces the firmware currently on the Tab5. Re-run with:

  TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE=1 ESPPORT=/dev/cu.usbmodem1101 ./flash.sh

On Linux the port is often /dev/ttyACM0. Use the Espressif USB-JTAG
device (USB VID:PID 303A:1001). Do not use a different usbmodem that
is not 303A:1001. Do not flash the ESP32-C6. Do not burn eFuse.
EOF
    exit 2
fi

if [[ "${port}" == "/dev/cu.usbmodem01" ]]; then
    echo "Refusing port /dev/cu.usbmodem01" >&2
    exit 2
fi

# shellcheck disable=SC1091
. "${root}/scripts/idf_env.sh"

cd "${proj}"
export ESPPORT="${port}"
idf.py -p "${port}" flash
