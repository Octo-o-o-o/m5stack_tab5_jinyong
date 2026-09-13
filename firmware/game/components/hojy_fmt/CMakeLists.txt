# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set(HOJY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/HeroesOfJinYong")
set(FMT_SRC_INC "${HOJY_ROOT}/deps/fmt/include")
set(FMT_DST_INC "${CMAKE_CURRENT_BINARY_DIR}/fmt_inc")

# The RISC-V toolchain has no __int128, and fmt's fallback uint128 lacks
# operator~. Patch a private copy of the headers, not the submodule.
file(COPY "${FMT_SRC_INC}/" DESTINATION "${FMT_DST_INC}")
file(READ "${FMT_DST_INC}/fmt/format.h" HOJY_FMT_HDR)
set(HOJY_FMT_NEEDLE "  friend constexpr auto operator|(const uint128& lhs, const uint128& rhs)")
set(HOJY_FMT_PATCH [=[  friend constexpr auto operator~(const uint128& v) -> uint128 {
    return uint128(~v.hi_, ~v.lo_);
  }
  friend constexpr auto operator|(const uint128& lhs, const uint128& rhs)]=])
string(REPLACE "${HOJY_FMT_NEEDLE}" "${HOJY_FMT_PATCH}" HOJY_FMT_HDR "${HOJY_FMT_HDR}")
if (HOJY_FMT_HDR MATCHES "operator~\\(const uint128")
    file(WRITE "${FMT_DST_INC}/fmt/format.h" "${HOJY_FMT_HDR}")
else()
    message(FATAL_ERROR "failed to patch fmt uint128 operator~")
endif()

idf_component_register(
    SRCS
        "${HOJY_ROOT}/deps/fmt/src/format.cc"
    INCLUDE_DIRS
        "${FMT_DST_INC}"
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    FMT_OS=0
    FMT_USE_INT128=0
    FMT_USE_CONSTEVAL=0
)
target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions -frtti -w)
