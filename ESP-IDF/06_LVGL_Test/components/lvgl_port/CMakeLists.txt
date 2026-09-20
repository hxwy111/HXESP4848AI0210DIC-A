idf_component_register(
    SRCS
        "lvgl_port.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_common

    PRIV_REQUIRES
        board_config
        display_bsp
        esp_timer
        freertos
        heap
        log
        lvgl
)