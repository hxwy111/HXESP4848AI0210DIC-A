#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bsp.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "buzzer.h"

/* ------------------------------------------------------------------
 * 硬件连接（依据开发板原理图 + 官方 06BuzzerDemo / 08LCD_Demo）
 *
 * 蜂鸣器不是直接接在某个 GPIO 上，而是接在 I2C IO 扩展芯片
 * TCA9554 的 P7 引脚（原理图网络名 Extend_IO8）。
 *
 *   - TCA9554 挂在 I2C0 总线上，器件地址 0x20（A2A1A0 = 000）
 *   - I2C0：SCL = GPIO40，SDA = GPIO41（与 QMI8658 / 触摸共用总线）
 *   - 蜂鸣器为有源蜂鸣器：P7 输出高电平即发声，低电平停止
 * ------------------------------------------------------------------ */

/* TCA9554 内部寄存器地址 */
#define TCA9554_REG_INPUT   0x00   // 输入端口（只读）
#define TCA9554_REG_OUTPUT  0x01   // 输出端口
#define TCA9554_REG_POLAR   0x02   // 极性反转
#define TCA9554_REG_CONFIG  0x03   // 配置（1=输入，0=输出）
#define BOARD_LCD_EXPANDER_ADDR 0x20

/* 蜂鸣器接在 TCA9554 的 P7（Extend_IO8） */
#define BUZZER_PIN         7

#define BEEP_ON_MS      1000  // 蜂鸣器响的持续时间（毫秒）
#define BEEP_OFF_MS     1000  // 蜂鸣器停的间隔时间（毫秒）

static const char *TAG = "Buzzer_Test";

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

/* 将 P7 配置为输出，其余引脚保持原样（P0~P6 被 LCD 复位等占用） */
static void buzzer_pin_to_output(void)
{
    uint8_t cfg;
    if (tca9554_read_reg(TCA9554_REG_CONFIG, &cfg) != ESP_OK) {
        ESP_LOGW(TAG, "读配置寄存器失败，默认 P0~P6 输入、P7 输出");
        cfg = (uint8_t)~(1 << BUZZER_PIN);   // 0x7F
    }
    cfg &= ~(1 << BUZZER_PIN);               // 配置位 = 0 表示输出
    tca9554_write_reg(TCA9554_REG_CONFIG, cfg);
}

/* 控制蜂鸣器：level 非 0 响，0 停 */
static void buzzer_set_level(uint8_t level)
{
    uint8_t out = 0;
    tca9554_read_reg(TCA9554_REG_OUTPUT, &out);   // 读回当前输出，避免影响其他引脚
    if (level) {
        out |= (1 << BUZZER_PIN);
    } else {
        out &= ~(1 << BUZZER_PIN);
    }
    tca9554_write_reg(TCA9554_REG_OUTPUT, out);
}

void Buzzer_while(void)
{
    ESP_LOGI(TAG, "Buzzer Test Started");

    /* 探测 TCA9554 是否在线（便于排查接线/供电问题） */
    uint8_t probe = 0;
    esp_err_t err = tca9554_read_reg(TCA9554_REG_CONFIG, &probe);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 (0x%02X) 无响应: %s，请检查 I2C 接线/供电", BOARD_LCD_EXPANDER_ADDR, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "TCA9554 在线，配置寄存器 = 0x%02X", probe);

    /* 蜂鸣器引脚设为输出，初始为低（不响） */
    buzzer_pin_to_output();
    buzzer_set_level(0);
    ESP_LOGI(TAG, "Buzzer ready (TCA9554 P%d / Extend_IO8)", BUZZER_PIN);

    uint32_t beep_count = 0;

    /* 主循环：间隔 1 秒响一次，每次持续 1 秒 */
    while (1) {
        /* 蜂鸣器响 */
        buzzer_set_level(1);
        beep_count++;
        ESP_LOGI(TAG, "Buzzer ON (#%lu)", (unsigned long)beep_count);
        vTaskDelay(pdMS_TO_TICKS(BEEP_ON_MS));

        /* 蜂鸣器停 */
        buzzer_set_level(0);
        ESP_LOGI(TAG, "Buzzer OFF");
        vTaskDelay(pdMS_TO_TICKS(BEEP_OFF_MS));
    }
}
