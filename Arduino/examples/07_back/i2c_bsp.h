#ifndef I2C_BSP_H
#define I2C_BSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t i2c_bsp_init(void);
esp_err_t i2c_bsp_probe(uint8_t address);
esp_err_t i2c_bsp_write(uint8_t address, const uint8_t *data, size_t length);
esp_err_t i2c_bsp_read(uint8_t address, uint8_t *data, size_t length);
esp_err_t i2c_bsp_write_read(uint8_t address, const uint8_t *write_data,
                             size_t write_length, uint8_t *read_data,
                             size_t read_length);

/* Compatibility wrappers used by the QMI8658 reference driver. */
uint8_t I2C_writr_buff(uint8_t address, uint8_t reg, uint8_t *data,
                       uint8_t length);
uint8_t I2C_read_buff(uint8_t address, uint8_t reg, uint8_t *data,
                      uint8_t length);

#ifdef __cplusplus
}
#endif

#endif
