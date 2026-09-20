#include "sd_card_bsp.h"

#include <stdio.h>
#include <sys/stat.h>

#include "board_config.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_card";
static sdmmc_card_t *s_card;
static bool s_initialized;

esp_err_t sd_card_bsp_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = BOARD_SD_CLK;
    slot.cmd = BOARD_SD_CMD;
    slot.d0 = BOARD_SD_D0;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(
        SD_CARD_MOUNT_POINT, &host, &slot, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD卡挂载失败: %s", esp_err_to_name(ret));
        s_card = NULL;

        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "SD卡挂载成功，使用SDMMC 1-bit (CMD=%d CLK=%d D0=%d)",
             BOARD_SD_CMD, BOARD_SD_CLK, BOARD_SD_D0);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t sd_card_bsp_write_file(const char *path, const void *data, size_t len)
{
    if (!s_initialized || s_card == NULL || path == NULL ||
        (data == NULL && len != 0)) {
        return ESP_ERR_INVALID_STATE;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    const size_t written = fwrite(data, 1, len, file);
    const int close_ret = fclose(file);
    if (written != len || close_ret != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t sd_card_bsp_read_file(const char *path, void *data, size_t capacity,
                                size_t *out_len)
{
    if (!s_initialized || s_card == NULL || path == NULL || data == NULL ||
        capacity == 0 || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    const size_t read_len = fread(data, 1, capacity - 1, file);
    const int close_ret = fclose(file);
    ((char *)data)[read_len] = '\0';
    *out_len = read_len;
    return close_ret == 0 ? ESP_OK : ESP_FAIL;
}
