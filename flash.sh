#!/usr/bin/env bash
# Flash is refused unless the owner explicitly accepts overwriting the
# Tab5 work firmware (Octoooo monitor). This session does not flash.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
proj="${root}/firmware/bringup"
port="${ESPPORT:-/dev/cu.usbmodem1101}"

if [[ "${TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE:-}" != "1" ]]; then
    cat <<'EOF' >&2
Refusing to flash.

This Tab5 currently runs the work Octoooo monitor. Flashing this project
replaces that system. Default is no flash.

When the owner has said in this conversation:
  "可以刷、我接受暂时覆盖 Tab5 上的工作固件"
then re-run:

  TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE=1 ESPPORT=/dev/cu.usbmodem1101 ./flash.sh

Use only /dev/cu.usbmodem1101 (Espressif 303A:1001 USB JTAG).
Do not use /dev/cu.usbmodem01. Do not flash ESP32-C6. Do not burn eFuse.
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
