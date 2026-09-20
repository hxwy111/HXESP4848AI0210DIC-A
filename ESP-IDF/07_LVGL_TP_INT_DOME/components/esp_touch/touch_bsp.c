#include "touch_bsp.h"

#include <string.h>

#include "board_config.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "i2c_bsp.h"

#define GT911_REG_PRODUCT_ID       0x8140
#define GT911_REG_CONFIG           0x8047
#define GT911_REG_STATUS           0x814E
#define GT911_REG_POINT1           0x814F
#define GT911_POINT_SIZE           8
#define GT911_STATUS_READY         0x80
#define GT911_STATUS_TOUCH_MASK    0x0F
#define GT911_MAX_TOUCHES          5

#define TCA9554_REG_INPUT          0x00
#define TCA9554_REG_OUTPUT         0x01
#define TCA9554_REG_CONFIG         0x03

#define GT911_RESET_LOW_MS         10
#define GT911_STARTUP_DELAY_MS     200

static const char *TAG = "gt911";
static uint8_t s_gt911_address;
static bool s_initialized;
static uint16_t s_touch_width = BOARD_LCD_H_RES;
static uint16_t s_touch_height = BOARD_LCD_V_RES;
static volatile bool s_touch_pressed;
static volatile uint16_t s_touch_x;
static volatile uint16_t s_touch_y;
static volatile uint32_t s_irq_count;
static TaskHandle_t s_touch_task;

static void IRAM_ATTR touch_gpio_isr(void *arg);
static void touch_irq_task(void *argument);

static esp_err_t expander_read_register(uint8_t reg, uint8_t *value)
{
    return i2c_bsp_write_read(BOARD_LCD_EXPANDER_ADDR, &reg, 1, value, 1);
}

static esp_err_t expander_write_register(uint8_t reg, uint8_t value)
{
    const uint8_t data[2] = {reg, value};
    return i2c_bsp_write(BOARD_LCD_EXPANDER_ADDR, data, sizeof(data));
}

static esp_err_t expander_update_output(uint8_t mask, uint8_t value)
{
    uint8_t output;
    esp_err_t ret = expander_read_register(TCA9554_REG_OUTPUT, &output);
    if (ret != ESP_OK) {
        return ret;
    }

    output = (uint8_t)((output & (uint8_t)~mask) | (value & mask));
    return expander_write_register(TCA9554_REG_OUTPUT, output);
}

static esp_err_t expander_set_direction(uint8_t mask, bool input)
{
    uint8_t direction;
    esp_err_t ret = expander_read_register(TCA9554_REG_CONFIG, &direction);
    if (ret != ESP_OK) {
        return ret;
    }

    if (input) {
        direction |= mask;
    } else {
        direction &= (uint8_t)~mask;
    }

    return expander_write_register(TCA9554_REG_CONFIG, direction);
}

static esp_err_t gt911_read_register(uint8_t address, uint16_t reg,
                                     uint8_t *data, size_t length)
{
    const uint8_t command[2] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF),
    };

    return i2c_bsp_write_read(address, command, sizeof(command), data, length);
}

static esp_err_t gt911_write_register(uint8_t address, uint16_t reg,
                                      const uint8_t *data, size_t length)
{
    if (data == NULL || length > 32) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[2 + 32];
    buffer[0] = (uint8_t)(reg >> 8);
    buffer[1] = (uint8_t)(reg & 0xFF);
    memcpy(&buffer[2], data, length);

    return i2c_bsp_write(address, buffer, length + 2);
}

static esp_err_t gt911_read_product_id(uint8_t address, char id[5])
{
    uint8_t raw[4] = {0};
    esp_err_t ret = gt911_read_register(address, GT911_REG_PRODUCT_ID,
                                        raw, sizeof(raw));
    if (ret != ESP_OK) {
        return ret;
    }

    memcpy(id, raw, sizeof(raw));
    id[4] = '\0';

    return (raw[0] == '9' && raw[1] == '1' && raw[2] == '1')
               ? ESP_OK
               : ESP_ERR_NOT_FOUND;
}

static esp_err_t gt911_reset_controller(bool int_high)
{
    /* TP_RST is TCA9554 P1.  TP_INT is now connected directly to GPIO45,
     * so it must never be driven through the expander during reset. */
    (void)int_high;
    esp_err_t ret = expander_set_direction(BOARD_TOUCH_RST_EXPANDER_BIT, false);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = expander_update_output(BOARD_TOUCH_RST_EXPANDER_BIT, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(GT911_RESET_LOW_MS));

    ret = expander_update_output(BOARD_TOUCH_RST_EXPANDER_BIT,
                                 BOARD_TOUCH_RST_EXPANDER_BIT);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(GT911_STARTUP_DELAY_MS));
    return ESP_OK;
}

static esp_err_t gt911_select_address(void)
{
    char id[5];

    /* The address strap is no longer driven by the expander. Probe the
     * address selected by the panel hardware, then try the alternate one. */
    esp_err_t ret = gt911_reset_controller(true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gt911_read_product_id(BOARD_TOUCH_I2C_ADDR_PRIMARY, id);
    if (ret == ESP_OK) {
        s_gt911_address = BOARD_TOUCH_I2C_ADDR_PRIMARY;
        ESP_LOGI(TAG, "GT911 found at 0x%02X, product=%s",
                 s_gt911_address, id);
    } else {
        ret = gt911_read_product_id(BOARD_TOUCH_I2C_ADDR_SECONDARY, id);
        if (ret == ESP_OK) {
            s_gt911_address = BOARD_TOUCH_I2C_ADDR_SECONDARY;
            ESP_LOGI(TAG, "GT911 found at 0x%02X, product=%s",
                     s_gt911_address, id);
        }
    }

    if (ret == ESP_OK) {
        uint8_t config[5] = {0};
        if (gt911_read_register(s_gt911_address, GT911_REG_CONFIG,
                                config, sizeof(config)) == ESP_OK) {
            uint16_t width = (uint16_t)(config[1] | ((uint16_t)config[2] << 8));
            uint16_t height = (uint16_t)(config[3] | ((uint16_t)config[4] << 8));
            if (width != 0 && height != 0) {
                s_touch_width = width;
                s_touch_height = height;
            }
            ESP_LOGI(TAG, "GT911 config: %ux%u", s_touch_width, s_touch_height);
        }
    }
    return ret;
}

esp_err_t touch_bsp_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = i2c_bsp_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* P0/P1/P2 are outputs (LCD reset, TP reset, and backlight enable).
     * TP_INT is the native GPIO45 input. */
    uint8_t expander_config = 0;
    ret = expander_read_register(TCA9554_REG_CONFIG, &expander_config);
    if (ret == ESP_OK) {
        expander_config &= (uint8_t)~BOARD_LCD_BACKLIGHT_EN_EXPANDER_BIT;
        ret = expander_write_register(TCA9554_REG_CONFIG, expander_config);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 direction setup failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = gt911_select_address();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GT911 initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const gpio_config_t gpio_cfg = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ret = gpio_config(&gpio_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TP_INT GPIO45 config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR service install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_initialized = true;
    if (xTaskCreate(touch_irq_task, "touch_irq", 4096, NULL, 5,
                    &s_touch_task) != pdPASS) {
        s_initialized = false;
        ESP_LOGE(TAG, "Unable to create touch IRQ task");
        return ESP_ERR_NO_MEM;
    }
    ret = gpio_isr_handler_add(BOARD_TOUCH_INT, touch_gpio_isr, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TP_INT GPIO45 ISR registration failed: %s",
                 esp_err_to_name(ret));
        vTaskDelete(s_touch_task);
        s_touch_task = NULL;
        s_initialized = false;
        return ret;
    }
    if (gpio_get_level(BOARD_TOUCH_INT) == (BOARD_TOUCH_INT_ACTIVE_LOW ? 0 : 1)) {
        xTaskNotifyGive(s_touch_task);
    }
    ESP_LOGI(TAG, "TP_INT GPIO45 falling-edge interrupt enabled");
    return ESP_OK;
}

static void IRAM_ATTR touch_gpio_isr(void *arg)
{
    (void)arg;
    BaseType_t higher_priority_task_woken = pdFALSE;
    s_irq_count++;
    if (s_touch_task != NULL) {
        vTaskNotifyGiveFromISR(s_touch_task, &higher_priority_task_woken);
    }
    if (higher_priority_task_woken) {
        portYIELD_FROM_ISR();
    }
}

static void touch_irq_task(void *argument)
{
    (void)argument;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t status = 0;
        if (gt911_read_register(s_gt911_address, GT911_REG_STATUS,
                                &status, 1) != ESP_OK) {
            ESP_LOGW(TAG, "TP_INT #%lu: GT911 status read failed",
                     (unsigned long)s_irq_count);
            continue;
        }
        ESP_LOGI(TAG, "TP_INT falling IRQ #%lu, status=0x%02X",
                 (unsigned long)s_irq_count, status);
        const uint8_t touch_count = status & GT911_STATUS_TOUCH_MASK;
        if ((status & GT911_STATUS_READY) != 0 && touch_count > 0 &&
            touch_count <= GT911_MAX_TOUCHES) {
            uint8_t point[GT911_POINT_SIZE];
            if (gt911_read_register(s_gt911_address, GT911_REG_POINT1,
                                    point, sizeof(point)) == ESP_OK) {
                const uint16_t raw_x = (uint16_t)(point[1] |
                                                  ((uint16_t)point[2] << 8));
                const uint16_t raw_y = (uint16_t)(point[3] |
                                                  ((uint16_t)point[4] << 8));
                uint16_t x = raw_x < BOARD_LCD_H_RES
                                 ? raw_x
                                 : (uint16_t)(((uint32_t)raw_x * BOARD_LCD_H_RES) /
                                              (s_touch_width ? s_touch_width : BOARD_LCD_H_RES));
                uint16_t y = raw_y < BOARD_LCD_V_RES
                                 ? raw_y
                                 : (uint16_t)(((uint32_t)raw_y * BOARD_LCD_V_RES) /
                                              (s_touch_height ? s_touch_height : BOARD_LCD_V_RES));
                s_touch_x = x >= BOARD_LCD_H_RES ? BOARD_LCD_H_RES - 1 : x;
                s_touch_y = y >= BOARD_LCD_V_RES ? BOARD_LCD_V_RES - 1 : y;
                s_touch_pressed = true;
            }
        } else if ((status & GT911_STATUS_READY) != 0 && touch_count == 0) {
            s_touch_pressed = false;
        }
        const uint8_t clear_status = 0;
        (void)gt911_write_register(s_gt911_address, GT911_REG_STATUS,
                                   &clear_status, 1);
    }
}

bool touch_bsp_read(uint16_t *x, uint16_t *y)
{
    if (!s_initialized || x == NULL || y == NULL) {
        return false;
    }
    *x = s_touch_x;
    *y = s_touch_y;
    return s_touch_pressed;
}

void touch_Init(void)
{
    (void)touch_bsp_init();
}

uint8_t getTouch(uint16_t *x, uint16_t *y)
{
    return touch_bsp_read(x, y) ? 1 : 0;
}
