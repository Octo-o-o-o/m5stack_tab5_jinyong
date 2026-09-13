#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Serial monitor only. Does not flash or erase.
#
#   ./monitor.sh [PORT]
#
# PORT falls back to $ESPPORT, then to the one connected Espressif USB device.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
proj="${root}/firmware/${TAB5_FIRMWARE:-game}"
port="${1:-${ESPPORT:-}}"

# shellcheck source=scripts/idf_env.sh
. "${root}/scripts/idf_env.sh"

if [[ -z "${port}" ]]; then
    port="$("${root}/scripts/select_port.sh")"
fi

cd "${proj}"
idf.py -p "${port}" monitor
