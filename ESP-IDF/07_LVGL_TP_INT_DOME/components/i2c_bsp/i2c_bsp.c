#include "i2c_bsp.h"

#include <string.h>

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "board_config.h"

#define I2C_BSP_PORT I2C_NUM_0
#define I2C_BSP_CLOCK_HZ 400000
#define I2C_BSP_TIMEOUT_MS 100

static bool s_initialized;

esp_err_t i2c_bsp_init(void)
{
    if (s_initialized) return ESP_OK;

    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_TOUCH_SDA,
        .scl_io_num = BOARD_TOUCH_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_BSP_CLOCK_HZ,
        .clk_flags = 0,
    };

    esp_err_t ret = i2c_param_config(I2C_BSP_PORT, &config);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    ret = i2c_driver_install(I2C_BSP_PORT, I2C_MODE_MASTER, 0, 0, 0);
    /* display_bsp may have already installed the same I2C0 master. The
     * legacy IDF 5.5 driver reports that duplicate install as ESP_FAIL
     * rather than ESP_ERR_INVALID_STATE, so continue using the existing bus. */
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE && ret != ESP_FAIL) {
        return ret;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t i2c_bsp_probe(uint8_t address)
{
    esp_err_t ret = i2c_bsp_init();
    if (ret != ESP_OK) return ret;

    i2c_cmd_handle_t command = i2c_cmd_link_create();
    if (command == NULL) return ESP_ERR_NO_MEM;

    i2c_master_start(command);
    i2c_master_write_byte(command, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(command);
    ret = i2c_master_cmd_begin(I2C_BSP_PORT, command,
                               pdMS_TO_TICKS(I2C_BSP_TIMEOUT_MS));
    i2c_cmd_link_delete(command);
    return ret;
}

esp_err_t i2c_bsp_write(uint8_t address, const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = i2c_bsp_init();
    if (ret != ESP_OK) return ret;
    return i2c_master_write_to_device(I2C_BSP_PORT, address, data, length,
                                      pdMS_TO_TICKS(I2C_BSP_TIMEOUT_MS));
}

esp_err_t i2c_bsp_read(uint8_t address, uint8_t *data, size_t length)
{
    if (data == NULL || length == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = i2c_bsp_init();
    if (ret != ESP_OK) return ret;
    return i2c_master_read_from_device(I2C_BSP_PORT, address, data, length,
                                       pdMS_TO_TICKS(I2C_BSP_TIMEOUT_MS));
}

esp_err_t i2c_bsp_write_read(uint8_t address, const uint8_t *write_data,
                             size_t write_length, uint8_t *read_data,
                             size_t read_length)
{
    if (write_data == NULL || write_length == 0 ||
        read_data == NULL || read_length == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = i2c_bsp_init();
    if (ret != ESP_OK) return ret;
    return i2c_master_write_read_device(I2C_BSP_PORT, address,
                                        write_data, write_length,
                                        read_data, read_length,
                                        pdMS_TO_TICKS(I2C_BSP_TIMEOUT_MS));
}

uint8_t I2C_writr_buff(uint8_t address, uint8_t reg, uint8_t *data,
                       uint8_t length)
{
    if (data == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t packet[1 + 255];
    packet[0] = reg;
    memcpy(&packet[1], data, length);
    return (uint8_t)i2c_bsp_write(address, packet, (size_t)length + 1);
}

uint8_t I2C_read_buff(uint8_t address, uint8_t reg, uint8_t *data,
                      uint8_t length)
{
    return (uint8_t)i2c_bsp_write_read(address, &reg, 1, data, length);
}
