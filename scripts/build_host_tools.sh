#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build upstream's makedata with the host compiler, without configuring the
# full desktop target (that needs SDL2). Output: local/host/bin/makedata.

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
hojy="${root}/third_party/HeroesOfJinYong"
out="${root}/local/host"
gen="${out}/generated"
bin="${out}/bin"

mkdir -p "${gen}" "${bin}"

python3 - <<PY
from pathlib import Path
src = Path("${hojy}/src")
gen = Path("${gen}")
config = src.joinpath("config.toml").read_text(encoding="utf-8")
strings = src.joinpath("strings.toml").read_text(encoding="utf-8")
if ")HOJY_CONFIG" in config or ")HOJY_STRINGS" in strings:
    raise SystemExit("toml contains raw-string delimiter")
header = """#pragma once

#include <string_view>

namespace hojy::tools::assets {

inline constexpr std::string_view ConfigToml = R\"HOJY_CONFIG(%s)HOJY_CONFIG\";
inline constexpr std::string_view StringsToml = R\"HOJY_STRINGS(%s)HOJY_STRINGS\";

}// namespace hojy::tools::assets
""" % (config, strings)
gen.joinpath("makedata_assets.hh").write_text(header, encoding="utf-8")
print("wrote", gen / "makedata_assets.hh")
PY

"${CXX:-c++}" -std=c++17 -O2 -I "${hojy}/src" -I "${gen}" -Dftello64=ftello -Dfseeko64=fseeko \
    "${hojy}/src/tools/makedata.cc" \
    "${hojy}/src/content/atomic_file.cc" \
    -o "${bin}/makedata"

echo "Host tool: ${bin}/makedata"
