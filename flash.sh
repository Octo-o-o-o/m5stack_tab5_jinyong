#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Flash the Tab5 firmware (game by default, TAB5_FIRMWARE=bringup for the
# self-test). This replaces whatever firmware is on the device, so it asks.
#
#   ./flash.sh [-y|--yes] [PORT]
#
# PORT falls back to $ESPPORT, then to the one connected Espressif USB device;
# with several connected it refuses rather than guess.

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
target="${TAB5_FIRMWARE:-game}"
proj="${root}/firmware/${target}"
port="${ESPPORT:-}"
assume_yes=0

for arg in "$@"; do
    case "${arg}" in
        -y|--yes) assume_yes=1 ;;
        -h|--help) sed -n '2,8s/^# \{0,1\}//p' "$0"; exit 0 ;;
        -*) echo "Unknown option: ${arg}" >&2; exit 2 ;;
        *) port="${arg}" ;;
    esac
done

if [[ ! -d "${proj}" ]]; then
    echo "Unknown firmware target: ${target} (${proj})" >&2
    exit 1
fi

if (( ! assume_yes )) && [[ ! -t 0 ]]; then
    echo "Refusing to flash non-interactively without --yes." >&2
    exit 2
fi

# shellcheck source=scripts/idf_env.sh
. "${root}/scripts/idf_env.sh"

if [[ -z "${port}" ]]; then
    port="$("${root}/scripts/select_port.sh")"
fi

if (( ! assume_yes )); then
    read -r -p "Replace the firmware on the Tab5 at ${port} with '${target}'? [y/N] " reply
    if [[ "${reply}" != [yY]* ]]; then
        echo "Aborted." >&2
        exit 1
    fi
fi

cd "${proj}"
idf.py -p "${port}" flash
