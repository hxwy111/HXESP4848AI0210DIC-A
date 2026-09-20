#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_CARD_MOUNT_POINT "/sd_card"

esp_err_t sd_card_bsp_init(void);
esp_err_t sd_card_bsp_write_file(const char *path, const void *data, size_t len);
esp_err_t sd_card_bsp_read_file(const char *path, void *data, size_t capacity,
                                size_t *out_len);

#ifdef __cplusplus
}
#endif
