idf_component_register(
    SRCS
        "display_bsp.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        board_config
        esp_common
        driver

    PRIV_REQUIRES
        esp_lcd
        i2c_bsp
        freertos
        heap
        log
)
