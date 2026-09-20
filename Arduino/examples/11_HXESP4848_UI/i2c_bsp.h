#pragma once

#include <Arduino.h>
#include "driver/i2c.h"
#include "esp_err.h"

/**
 * @brief 板级公共 I2C 驱动
 *
 * 当前显示工程通过 I2C0 访问 TCA9554 IO扩展器（地址0x20），
 * 用于完成LCD硬件复位。
 *
 * I2C引脚：
 *   - SCL：GPIO40
 *   - SDA：GPIO41
 *
 * 注意：
 *   I2C总线只能在这里初始化一次，其他设备驱动不得再次调用
 *   i2c_param_config() 和 i2c_driver_install()。
 */
namespace I2CBSP {

// 使用ESP32-S3的I2C控制器0
constexpr i2c_port_t PORT = I2C_NUM_0;

// 根据当前模块原理图配置I2C引脚
constexpr gpio_num_t SCL_PIN = GPIO_NUM_40;
constexpr gpio_num_t SDA_PIN = GPIO_NUM_41;

// 初次调试使用200 kHz，提高总线通信稳定性
// 屏幕和触摸稳定后，可以尝试提高到400 kHz
constexpr uint32_t CLOCK_HZ = 200000;

// 单次I2C操作的最大等待时间
constexpr uint32_t TIMEOUT_MS = 100;

/**
 * @brief 初始化公共I2C总线
 *
 * 该函数可以重复调用，但实际只会安装一次I2C驱动。
 *
 * @return true  初始化成功
 * @return false 初始化失败
 */
bool begin();

/**
 * @brief 检查指定I2C地址是否存在设备
 *
 * @param deviceAddress 7位I2C设备地址
 * @return ESP_OK 设备有应答
 * @return 其他值 设备无应答或总线异常
 */
esp_err_t probe(uint8_t deviceAddress);

/**
 * @brief 向指定I2C设备写入数据
 *
 * @param deviceAddress 7位I2C设备地址
 * @param data          待发送数据
 * @param length        数据长度
 * @return ESP-IDF错误码
 */
esp_err_t write(
    uint8_t deviceAddress,
    const uint8_t *data,
    size_t length
);

/**
 * @brief 从指定I2C设备读取数据
 *
 * @param deviceAddress 7位I2C设备地址
 * @param data          接收缓冲区
 * @param length        读取长度
 * @return ESP-IDF错误码
 */
esp_err_t read(
    uint8_t deviceAddress,
    uint8_t *data,
    size_t length
);

/**
 * @brief 先写入命令或寄存器地址，然后读取数据
 *
 * 该操作在一次I2C事务中完成，中间使用Repeated START，
 * 适合执行带寄存器地址的通用I2C读取。
 *
 * @param deviceAddress 7位I2C设备地址
 * @param writeData     待写入的数据
 * @param writeLength   写入长度
 * @param readData      接收缓冲区
 * @param readLength    读取长度
 * @return ESP-IDF错误码
 */
esp_err_t writeRead(
    uint8_t deviceAddress,
    const uint8_t *writeData,
    size_t writeLength,
    uint8_t *readData,
    size_t readLength
);

} // namespace I2CBSP
