set(HOJY_SRC "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/HeroesOfJinYong/src")

file(GLOB HOJY_APP     "${HOJY_SRC}/app/*.cc")
file(GLOB HOJY_AUDIO   "${HOJY_SRC}/audio/*.cc")
file(GLOB HOJY_BATTLE  "${HOJY_SRC}/battle/*.cc")
file(GLOB HOJY_CONTENT "${HOJY_SRC}/content/*.cc")
file(GLOB HOJY_CORE    "${HOJY_SRC}/core/*.cc")
file(GLOB HOJY_EVENT   "${HOJY_SRC}/event/*.cc")
file(GLOB HOJY_SCENE   "${HOJY_SRC}/scene/*.cc")
file(GLOB HOJY_UTIL    "${HOJY_SRC}/util/*.cc")
file(GLOB HOJY_WORLD   "${HOJY_SRC}/world/*.cc")

set(HOJY_SOURCES
    ${HOJY_APP}
    ${HOJY_AUDIO}
    ${HOJY_BATTLE}
    ${HOJY_CONTENT}
    ${HOJY_CORE}
    ${HOJY_EVENT}
    ${HOJY_SCENE}
    ${HOJY_UTIL}
    ${HOJY_WORLD}
)

list(FILTER HOJY_SOURCES EXCLUDE REGEX "application\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "channelmidi\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "effect\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "channel\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "config\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "conv\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "rate_scheduler\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "resourcemgr\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "warfield_render\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "window_audio\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "/window\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "globalmap\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "title\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "renderer\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "submap\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "savedata\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "grpdata\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "messagebox\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "talkbox\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "nodewithcache\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "input_repeat\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "warfield_load\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "/texture\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "/map\\.cc$")
list(FILTER HOJY_SOURCES EXCLUDE REGEX "ttf\\.cc$")
list(APPEND HOJY_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/application.cc"
    "${CMAKE_CURRENT_LIST_DIR}/channelmidi_stub.cc"
    "${CMAKE_CURRENT_LIST_DIR}/effect.cc"
    "${CMAKE_CURRENT_LIST_DIR}/channel.cc"
    "${CMAKE_CURRENT_LIST_DIR}/config.cc"
    "${CMAKE_CURRENT_LIST_DIR}/conv.cc"
    "${CMAKE_CURRENT_LIST_DIR}/main.cc"
    "${CMAKE_CURRENT_LIST_DIR}/rate_scheduler.cc"
    "${CMAKE_CURRENT_LIST_DIR}/resourcemgr.cc"
    "${CMAKE_CURRENT_LIST_DIR}/warfield_render.cc"
    "${CMAKE_CURRENT_LIST_DIR}/window_audio.cc"
    "${CMAKE_CURRENT_LIST_DIR}/window.cc"
    "${CMAKE_CURRENT_LIST_DIR}/globalmap.cc"
    "${CMAKE_CURRENT_LIST_DIR}/title.cc"
    "${CMAKE_CURRENT_LIST_DIR}/renderer.cc"
    "${CMAKE_CURRENT_LIST_DIR}/submap.cc"
    "${CMAKE_CURRENT_LIST_DIR}/savedata.cc"
    "${CMAKE_CURRENT_LIST_DIR}/grpdata.cc"
    "${CMAKE_CURRENT_LIST_DIR}/messagebox.cc"
    "${CMAKE_CURRENT_LIST_DIR}/talkbox.cc"
    "${CMAKE_CURRENT_LIST_DIR}/nodewithcache.cc"
    "${CMAKE_CURRENT_LIST_DIR}/input_repeat.cc"
    "${CMAKE_CURRENT_LIST_DIR}/warfield_load.cc"
    "${CMAKE_CURRENT_LIST_DIR}/texture.cc"
    "${CMAKE_CURRENT_LIST_DIR}/map.cc"
    "${CMAKE_CURRENT_LIST_DIR}/channelwavstream.cc"
    "${CMAKE_CURRENT_LIST_DIR}/ttf.cc"
)

idf_component_register(
    SRCS
        ${HOJY_SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}"
        "${HOJY_SRC}"
        "${HOJY_SRC}/app"
        "${HOJY_SRC}/audio"
        "${HOJY_SRC}/battle"
        "${HOJY_SRC}/content"
        "${HOJY_SRC}/core"
        "${HOJY_SRC}/event"
        "${HOJY_SRC}/scene"
        "${HOJY_SRC}/util"
        "${HOJY_SRC}/world"
    REQUIRES
        hojy_sdl
        hojy_fmt
        hojy_zita
        tab5_platform
        log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SDL_MAIN_HANDLED
    HOJY_VERSION="tab5-dev"
    HOJY_TAB5=1
    HOJY_TAB5_GAME_ROOT="/sdcard/jinyong"
    ALLOW_ODD_WIDTH
    ftello64=ftello
    fseeko64=fseeko
)
# Upstream HOJY is not -Werror-clean under RISC-V GCC 14. Do not patch gameplay.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -fexceptions
    -frtti
    -w
)
