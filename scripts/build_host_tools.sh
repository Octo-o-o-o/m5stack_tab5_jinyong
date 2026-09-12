#!/usr/bin/env bash
# Build makedata/mergepic with the host clang++, without configuring the
# full HOJY desktop target (that needs SDL2).

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
hojy="${root}/third_party/HeroesOfJinYong"
out="${root}/host/build"
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

cxx="${CXX:-c++}"
common=(-std=c++17 -O2 -I "${hojy}/src" -I "${gen}" -Dftello64=ftello -Dfseeko64=fseeko)

"${cxx}" "${common[@]}" \
    "${hojy}/src/tools/makedata.cc" \
    "${hojy}/src/content/atomic_file.cc" \
    -o "${bin}/makedata"

"${cxx}" "${common[@]}" \
    "${hojy}/src/tools/mergepic.cc" \
    "${hojy}/src/util/file.cc" \
    -o "${bin}/mergepic"

echo "Host tools: ${bin}/makedata ${bin}/mergepic"
