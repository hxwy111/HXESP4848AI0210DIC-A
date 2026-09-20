#include "sd_card_bsp.h"

#include <stdio.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_card";
static sdmmc_card_t *s_card;
static bool s_initialized;

/* GPIO1/GPIO2 are shared by the LCD control SPI and SDMMC CMD/CLK. */
#define SD_CARD_STARTUP_DELAY_MS 200

static const int s_freq_ladder_khz[] = {
    SDMMC_FREQ_DEFAULT,  /* 20 MHz */
    SDMMC_FREQ_DEFAULT / 2,
    SDMMC_FREQ_PROBING,  /* 400 kHz */
};
/* 每档重试次数：上电后卡内部还在稳压，首次失败往往下一次就能过。 */
#define SD_MOUNT_ATTEMPTS_PER_FREQ 2
#define SD_MOUNT_RETRY_DELAY_MS 100

static esp_err_t sd_card_try_mount(int freq_khz)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = freq_khz;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = BOARD_SD_CLK;
    slot.cmd = BOARD_SD_CMD;
    slot.d0 = BOARD_SD_D0;
    /* 板上若没有外接10 kΩ上拉，这里的内部上拉（约45 kΩ）是兜底措施。
     * 1-bit模式下D1/D2/D3未接管，仍需硬件上拉——尤其D3上电必须为高，
     * 否则卡会进SPI模式而不是SD模式。 */
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    return esp_vfs_fat_sdmmc_mount(
        SD_CARD_MOUNT_POINT, &host, &slot, &mount_config, &s_card);
}

esp_err_t sd_card_bsp_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* Let the card power rail and the LCD-to-SD GPIO hand-off settle. */
    vTaskDelay(pdMS_TO_TICKS(SD_CARD_STARTUP_DELAY_MS));

    esp_err_t ret = ESP_FAIL;
    for (size_t f = 0;
         f < sizeof(s_freq_ladder_khz) / sizeof(s_freq_ladder_khz[0]); ++f) {
        const int freq_khz = s_freq_ladder_khz[f];

        for (int attempt = 1; attempt <= SD_MOUNT_ATTEMPTS_PER_FREQ; ++attempt) {
            ESP_LOGI(TAG, "SD mount attempt: %d kHz, %d/%d",
                     freq_khz, attempt, SD_MOUNT_ATTEMPTS_PER_FREQ);
            ret = sd_card_try_mount(freq_khz);
            if (ret == ESP_OK) {
                s_initialized = true;
                ESP_LOGI(TAG,
                         "SD卡挂载成功，SDMMC 1-bit @%d kHz "
                         "(CMD=%d CLK=%d D0=%d)",
                         freq_khz, BOARD_SD_CMD, BOARD_SD_CLK, BOARD_SD_D0);
                if (freq_khz != s_freq_ladder_khz[0]) {
                    /* 需要降速才挂得上，说明信号余量不足，应查引脚冲突和上拉。 */
                    ESP_LOGW(TAG, "已降速至%d kHz才挂载成功，请检查GPIO%d/GPIO%d"
                                  "是否与LCD 3-wire SPI共用，以及CMD/D0上拉",
                             freq_khz, BOARD_SD_CMD, BOARD_SD_CLK);
                }
                sdmmc_card_print_info(stdout, s_card);
                return ESP_OK;
            }

            /* 挂载失败时esp_vfs_fat_sdmmc_mount已自行回收host和card，
             * 这里只需清掉悬空指针再重试。 */
            s_card = NULL;
            ESP_LOGW(TAG, "SD卡挂载失败 (%d kHz, 第%d/%d次): %s", freq_khz,
                     attempt, SD_MOUNT_ATTEMPTS_PER_FREQ,
                     esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(SD_MOUNT_RETRY_DELAY_MS));
        }
    }

    ESP_LOGE(TAG, "SD卡挂载失败，已试完全部速率档位: %s", esp_err_to_name(ret));
    return ret;
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
