# shellcheck shell=bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Put ESP-IDF on PATH for the calling script. A shell that already exported
# IDF is used as is; otherwise source IDF_EXPORT, which defaults to where the
# ESP-IDF installer puts v5.5.5.

if [[ -z "${IDF_EXPORT:-}" && -n "${IDF_PATH:-}" ]] && command -v idf.py >/dev/null 2>&1; then
    return 0
fi

idf_export="${IDF_EXPORT:-${HOME}/.espressif/esp-idf-v5.5.5/export.sh}"

if [[ ! -f "${idf_export}" ]]; then
    echo "ESP-IDF export.sh not found: ${idf_export}" >&2
    echo "Install ESP-IDF v5.5.5 or set IDF_EXPORT to its export.sh." >&2
    exit 1
fi

# shellcheck disable=SC1090
. "${idf_export}"

if [[ -z "${IDF_PATH:-}" ]]; then
    echo "IDF_PATH is empty after sourcing export.sh" >&2
    exit 1
fi
