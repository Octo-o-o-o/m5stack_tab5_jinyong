set(ZITA "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/HeroesOfJinYong/deps/zita-resampler/source")

idf_component_register(
    SRCS
        "${ZITA}/vresampler.cc"
        "${ZITA}/resampler-table.cc"
    INCLUDE_DIRS
        "${ZITA}"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions -w)
