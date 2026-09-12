idf_component_register(
    SRCS
        "app_main.cpp"
        "fallback_ui.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash
        esp_timer
        hojy_core
        tab5_platform
        espressif__m5stack_tab5_noglib
)

target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions -frtti)
