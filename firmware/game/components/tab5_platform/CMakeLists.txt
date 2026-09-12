idf_component_register(
    SRCS
        "tab5_mem.c"
        "tab5_probe.c"
        "tab5_prof.c"
        "tab5_video.c"
        "tab5_input.c"
        "tab5_fs.c"
        "tab5_audio.c"
        "tab5_clock.c"
    INCLUDE_DIRS
        "include"
        "../../../../platform/tab5"
    REQUIRES
        driver
        fatfs
        esp_lcd
        esp_timer
        esp_psram
        esp_mm
        esp_driver_ppa
        nvs_flash
        spi_flash
        esp_hw_support
        espressif__m5stack_tab5_noglib
        espressif__esp_codec_dev
)

# Pixel loops only. Keep the rest of the firmware on the project-wide setting.
target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
