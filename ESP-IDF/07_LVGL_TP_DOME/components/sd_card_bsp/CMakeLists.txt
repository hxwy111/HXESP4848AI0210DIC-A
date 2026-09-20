idf_component_register(
    SRCS "sd_card_bsp.c"
    PRIV_REQUIRES fatfs sdmmc esp_driver_sdmmc board_config
    INCLUDE_DIRS "."
)
