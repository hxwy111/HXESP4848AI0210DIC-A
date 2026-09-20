#include <Arduino.h>
#include "i2c_bsp.h"

namespace {

/* TCA9554 内部寄存器地址 */
#define TCA9554_REG_OUTPUT  0x01   // 输出端口
#define TCA9554_REG_CONFIG  0x03   // 配置（1=输入，0=输出）
#define BOARD_LCD_EXPANDER_ADDR 0x20
constexpr uint8_t kBacklightControlPin = 38;
constexpr uint8_t kBacklightEnableBit = (1u << 2); // TCA9554 P2

/* 写 TCA9554 寄存器：发送格式 [寄存器地址][数据] */
static esp_err_t tca9554_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = { reg, data };
    return i2c_bsp_write(BOARD_LCD_EXPANDER_ADDR, buf, sizeof(buf));
}

/* 读 TCA9554 寄存器：先写寄存器地址，再用重复起始条件读回 1 字节 */
static esp_err_t tca9554_read_reg(uint8_t reg, uint8_t *data)
{
    return i2c_bsp_write_read(BOARD_LCD_EXPANDER_ADDR, &reg, 1, data, 1);
}

[[noreturn]] static void stopWithError(const char *message)
{
    Serial.println(message);
    while (true) {
        delay(1000);
    }
}

/* 将 P2 配置为输出。 */
static bool back_en_pin_to_output(void)
{
    uint8_t cfg = 0;
    if (tca9554_read_reg(TCA9554_REG_CONFIG, &cfg) != ESP_OK) {
        Serial.println("TCA9554 config read failed");
        return false;
    }
    cfg = static_cast<uint8_t>(cfg & ~kBacklightEnableBit);
    if (tca9554_write_reg(TCA9554_REG_CONFIG, cfg) != ESP_OK) {
        Serial.println("TCA9554 config write failed");
        return false;
    }
    return true;
}

static bool back_en_pin_set_level(bool high)
{
    uint8_t out = 0;
    if (tca9554_read_reg(TCA9554_REG_OUTPUT, &out) != ESP_OK) {
        Serial.println("TCA9554 output read failed");
        return false;
    }
    if (high) {
        out = static_cast<uint8_t>(out | kBacklightEnableBit);
    } else {
        out = static_cast<uint8_t>(out & ~kBacklightEnableBit);
    }
    if (tca9554_write_reg(TCA9554_REG_OUTPUT, out) != ESP_OK) {
        Serial.println("TCA9554 output write failed");
        return false;
    }
    return true;
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(100);

    pinMode(kBacklightControlPin, OUTPUT);
    digitalWrite(kBacklightControlPin, HIGH);

    if (i2c_bsp_init() != ESP_OK) {
        stopWithError("I2C initialization failed");
    }
    if (i2c_bsp_probe(BOARD_LCD_EXPANDER_ADDR) != ESP_OK) {
        stopWithError("TCA9554 0x20 is not responding");
    }
    // 先写低电平锁存值，再把 P2 切换为输出，避免 BL_EN 瞬间拉高。
    if (!back_en_pin_set_level(false)) {
        stopWithError("TCA9554 BL_EN low failed");
    }
    if (!back_en_pin_to_output()) {
        stopWithError("TCA9554 P2 output configuration failed");
    }

    delay(500);

    Serial.println("Backlight enable");
    digitalWrite(kBacklightControlPin, LOW);
    if (!back_en_pin_set_level(true)) {
        stopWithError("TCA9554 BL_EN high failed");
    }

}

void loop()
{
    delay(1000);
}
