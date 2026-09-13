#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Collect release assets from firmware that has already been built (run
# ./build.sh for game and TAB5_FIRMWARE=bringup ./build.sh first).
#
#   scripts/package_release.sh <vX.Y.Z> [out-dir]
#
# For each firmware: one image to flash at 0x0, and the ELF + map for decoding
# backtraces. Plus the third-party license texts and SHA256SUMS.

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
version="${1:-}"
out="${2:-${root}/dist}"

if [[ ! "${version}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    sed -n '5,11s/^# \{0,1\}//p' "$0" >&2
    exit 2
fi

# shellcheck source=scripts/idf_env.sh
. "${root}/scripts/idf_env.sh"

rm -rf "${out}"
mkdir -p "${out}"
out="$(cd "${out}" && pwd)"

for target in game bringup; do
    build="${root}/firmware/${target}/build"
    if [[ ! -f "${build}/flash_args" ]]; then
        echo "No build in ${build}. Run TAB5_FIRMWARE=${target} ./build.sh first." >&2
        exit 1
    fi
    name="$(python -c 'import json, sys; print(json.load(open(sys.argv[1]))["project_name"])' \
        "${build}/project_description.json")"
    (
        cd "${build}"
        python -m esptool --chip esp32p4 merge_bin -o "${out}/${name}-${version}.bin" @flash_args
        python -m zipfile -c "${out}/${name}-${version}-elf.zip" "${name}.elf" "${name}.map"
    )
done

"${root}/scripts/collect_licenses.sh" "${out}/licenses"
(cd "${out}" && python -m zipfile -c "tab5_jinyong-${version}-licenses.zip" licenses && rm -rf licenses)

# sha256sum format, without depending on coreutils or perl being installed.
python - "${out}" <<'EOF'
import hashlib, pathlib, sys
out = pathlib.Path(sys.argv[1])
sums = [f"{hashlib.sha256(f.read_bytes()).hexdigest()}  {f.name}\n"
        for f in sorted(out.iterdir()) if f.suffix in (".bin", ".zip")]
(out / "SHA256SUMS").write_text("".join(sums))
EOF
ls -l "${out}"
