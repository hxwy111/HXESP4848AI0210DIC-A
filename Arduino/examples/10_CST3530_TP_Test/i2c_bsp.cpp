#include "i2c_bsp.h"

namespace I2CBSP {

// 记录公共I2C总线是否已经初始化
// 防止重复调用i2c_driver_install()
static bool initialized = false;

bool begin()
{
    // 如果已经初始化，直接返回成功
    if (initialized) {
        return true;
    }

    // 创建并清空I2C配置结构体
    i2c_config_t config = {};

    // ESP32作为I2C主机
    config.mode = I2C_MODE_MASTER;

    // 根据原理图设置公共I2C引脚
    config.sda_io_num = SDA_PIN;
    config.scl_io_num = SCL_PIN;

    // 启用ESP32内部上拉
    // 实际硬件通常还应有外部上拉电阻
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;

    // 设置公共I2C总线频率
    config.master.clk_speed = CLOCK_HZ;
    config.clk_flags = 0;

    // 将上述参数配置到I2C0控制器
    esp_err_t result = i2c_param_config(PORT, &config);

    if (result != ESP_OK) {
        Serial.printf(
            "I2C BSP: parameter configuration failed: %s\n",
            esp_err_to_name(result)
        );
        return false;
    }

    // 安装I2C0驱动
    // 主机模式不需要接收和发送环形缓冲区，所以长度都设置为0
    result = i2c_driver_install(
        PORT,
        I2C_MODE_MASTER,
        0,
        0,
        0
    );

    if (result != ESP_OK) {
        Serial.printf(
            "I2C BSP: driver installation failed: %s\n",
            esp_err_to_name(result)
        );
        return false;
    }

    // 标记I2C已经初始化
    initialized = true;

    return true;
}

esp_err_t write(
    uint8_t deviceAddress,
    const uint8_t *data,
    size_t length
)
{
    // 检查输入参数是否合法
    if ((data == nullptr) || (length == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    // 向指定设备写入数据
    return i2c_master_write_to_device(
        PORT,
        deviceAddress,
        data,
        length,
        pdMS_TO_TICKS(TIMEOUT_MS)
    );
}

esp_err_t read(
    uint8_t deviceAddress,
    uint8_t *data,
    size_t length
)
{
    // 检查接收缓冲区是否合法
    if ((data == nullptr) || (length == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    // 从指定设备读取数据
    return i2c_master_read_from_device(
        PORT,
        deviceAddress,
        data,
        length,
        pdMS_TO_TICKS(TIMEOUT_MS)
    );
}

esp_err_t writeRead(
    uint8_t deviceAddress,
    const uint8_t *writeData,
    size_t writeLength,
    uint8_t *readData,
    size_t readLength
)
{
    // 检查发送和接收参数
    if ((writeData == nullptr) ||
        (writeLength == 0) ||
        (readData == nullptr) ||
        (readLength == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * 先向设备写入寄存器地址，然后使用Repeated START读取数据。
     * 整个过程由ESP-IDF的I2C驱动完成。
     */
    return i2c_master_write_read_device(
        PORT,
        deviceAddress,
        writeData,
        writeLength,
        readData,
        readLength,
        pdMS_TO_TICKS(TIMEOUT_MS)
    );
}

esp_err_t probe(uint8_t deviceAddress)
{
    // 创建一个I2C命令链
    i2c_cmd_handle_t command = i2c_cmd_link_create();

    if (command == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    // 产生START起始信号
    i2c_master_start(command);

    /*
     * 发送7位设备地址和写方向位。
     * 如果设备存在，它会返回ACK。
     */
    i2c_master_write_byte(
        command,
        static_cast<uint8_t>(
            (deviceAddress << 1) | I2C_MASTER_WRITE
        ),
        true
    );

    // 产生STOP停止信号
    i2c_master_stop(command);

    // 在公共I2C0总线上执行命令
    const esp_err_t result = i2c_master_cmd_begin(
        PORT,
        command,
        pdMS_TO_TICKS(TIMEOUT_MS)
    );

    // 释放命令链占用的内存
    i2c_cmd_link_delete(command);

    return result;
}

} // namespace I2CBSP
