# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Every .cc in this directory replaces the upstream file with the same name;
# the rest of upstream builds unchanged. The submodule itself is never edited.
# The globs run at configure time: after adding or removing an overlay, run
# `idf.py reconfigure`.
set(HOJY_SRC "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/HeroesOfJinYong/src")
set(HOJY_MODULES app audio battle content core event scene util world)

file(GLOB OVERLAY_SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.cc")
set(OVERLAY_NAMES "")
foreach(src IN LISTS OVERLAY_SOURCES)
    get_filename_component(name "${src}" NAME)
    list(APPEND OVERLAY_NAMES "${name}")
endforeach()

set(UPSTREAM_SOURCES "")
set(UPSTREAM_INCLUDE_DIRS "${HOJY_SRC}")
foreach(module IN LISTS HOJY_MODULES)
    file(GLOB module_sources "${HOJY_SRC}/${module}/*.cc")
    foreach(src IN LISTS module_sources)
        get_filename_component(name "${src}" NAME)
        if(NOT name IN_LIST OVERLAY_NAMES)
            list(APPEND UPSTREAM_SOURCES "${src}")
        endif()
    endforeach()
    list(APPEND UPSTREAM_INCLUDE_DIRS "${HOJY_SRC}/${module}")
endforeach()

idf_component_register(
    SRCS
        ${UPSTREAM_SOURCES}
        ${OVERLAY_SOURCES}
    PRIV_INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}"
        ${UPSTREAM_INCLUDE_DIRS}
    REQUIRES
        hojy_sdl
        hojy_fmt
        hojy_zita
        tab5_platform
        log
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    HOJY_VERSION="tab5-dev"
    ALLOW_ODD_WIDTH
    ftello64=ftello
    fseeko64=fseeko
)
# Upstream is not warning-clean under RISC-V GCC 14 (-Werror=parentheses,
# reorder, ...). Overlays are copies of upstream files and inherit the same
# expressions, so the whole component builds with -w rather than patching
# gameplay code to silence them.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)
