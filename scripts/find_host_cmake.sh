# shellcheck shell=bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Print a host cmake: the one on PATH, else the copy ESP-IDF installs, else
# Homebrew's. Exits non-zero when there is none. Run it, do not source it.

if command -v cmake >/dev/null 2>&1; then
    command -v cmake
    exit 0
fi

for cand in \
    "${HOME}/.espressif/tools/cmake/"*/CMake.app/Contents/bin/cmake \
    "${HOME}/.espressif/tools/cmake/"*/bin/cmake \
    /opt/homebrew/bin/cmake \
    /usr/local/bin/cmake
do
    if [[ -x "${cand}" ]]; then
        echo "${cand}"
        exit 0
    fi
done

exit 1
