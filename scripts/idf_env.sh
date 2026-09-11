#!/usr/bin/env bash
# Source ESP-IDF without baking a personal absolute path into the repo.
# Override with IDF_EXPORT if the toolchain lives elsewhere.

set -euo pipefail

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
