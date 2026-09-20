#include "display_bsp.h"
#include "board_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_rom_sys.h"
#include "i2c_bsp.h"

static const char *TAG = "display_bsp";
static esp_lcd_panel_handle_t s_rgb_panel;
static bool s_initialized;

/* HXESP4848AI0210DIC-A backlight wiring:
 *   BL_PWM -> ESP GPIO38 (active low)
 *   BL_EN  -> TCA9554 P2 / Extend_IO3 (active high)
 * Keep these levels explicit here because both signals must be asserted for
 * the AP3032 backlight driver to turn on after reset. */
static const gpio_num_t s_backlight_pwm_gpio = GPIO_NUM_38;
static const uint8_t s_backlight_en_bit = (1u << 2);

typedef struct {
    uint8_t cmd;
    const uint8_t *data;
    uint8_t len;
    uint16_t delay_ms;
} init_cmd_t;

#define CMD(name, ...) static const uint8_t name[] = { __VA_ARGS__ }

CMD(c_ff10, 0x77, 0x01, 0x00, 0x00, 0x10);
CMD(c_c0, 0x3B, 0x00);
CMD(c_c1, 0x0B, 0x02);
CMD(c_c2, 0x07, 0x02);
CMD(c_cc, 0x10);
CMD(c_b0, 0x00, 0x11, 0x16, 0x0E, 0x11, 0x06, 0x05, 0x09,
    0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18);
CMD(c_b1, 0x00, 0x11, 0x16, 0x0E, 0x11, 0x07, 0x05, 0x09,
    0x09, 0x21, 0x05, 0x13, 0x11, 0x2A, 0x31, 0x18);
CMD(c_ff11, 0x77, 0x01, 0x00, 0x00, 0x11);
CMD(c_b0_1, 0x6D);
CMD(c_b1_1, 0x37);
CMD(c_b2, 0x81);
CMD(c_b3, 0x80);
CMD(c_b5, 0x43);
CMD(c_b7, 0x85);
CMD(c_b8, 0x20);
CMD(c_c1_1, 0x78);
CMD(c_c2_1, 0x78);
CMD(c_d0, 0x88);
CMD(c_e0, 0x00, 0x00, 0x02);
CMD(c_e1, 0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00, 0x00, 0x20, 0x20);
CMD(c_e2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
CMD(c_e3, 0x00, 0x00, 0x11, 0x00);
CMD(c_e4, 0x22, 0x00);
CMD(c_e5, 0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
CMD(c_e6, 0x00, 0x00, 0x11, 0x00);
CMD(c_e7, 0x22, 0x00);
CMD(c_e8, 0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
CMD(c_eb, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00);
CMD(c_ed, 0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF,
    0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF);
CMD(c_ef, 0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F);
CMD(c_ff13, 0x77, 0x01, 0x00, 0x00, 0x13);
CMD(c_ef13, 0x08);
CMD(c_ff00, 0x77, 0x01, 0x00, 0x00, 0x00);
CMD(c_36, 0x00);
CMD(c_3a, 0x50);

static const init_cmd_t s_init[] = {
    {0xFF, c_ff10, 5, 0},
    {0xC0, c_c0, 2, 0},
    {0xC1, c_c1, 2, 0},
    {0xC2, c_c2, 2, 0},
    {0xCC, c_cc, 1, 0},
    {0xB0, c_b0, 16, 0},
    {0xB1, c_b1, 16, 0},
    {0xFF, c_ff11, 5, 0},
    {0xB0, c_b0_1, 1, 0},
    {0xB1, c_b1_1, 1, 0},
    {0xB2, c_b2, 1, 0},
    {0xB3, c_b3, 1, 0},
    {0xB5, c_b5, 1, 0},
    {0xB7, c_b7, 1, 0},
    {0xB8, c_b8, 1, 0},
    {0xC1, c_c1_1, 1, 0},
    {0xC2, c_c2_1, 1, 0},
    {0xD0, c_d0, 1, 0},
    {0xE0, c_e0, 3, 0},
    {0xE1, c_e1, 11, 0},
    {0xE2, c_e2, 13, 0},
    {0xE3, c_e3, 4, 0},
    {0xE4, c_e4, 2, 0},
    {0xE5, c_e5, 16, 0},
    {0xE6, c_e6, 4, 0},
    {0xE7, c_e7, 2, 0},
    {0xE8, c_e8, 16, 0},
    {0xEB, c_eb, 7, 0},
    {0xED, c_ed, 16, 0},
    {0xEF, c_ef, 6, 0},
    {0xFF, c_ff13, 5, 0},
    {0xEF, c_ef13, 1, 0},
    {0xFF, c_ff00, 5, 0},
    {0x11, NULL, 0, 120},
    {0x29, NULL, 0, 0},
    {0x36, c_36, 1, 0},
    {0x3A, c_3a, 1, 0},
};

static void spi3_delay(void)
{
    esp_rom_delay_us(1);
}

static void spi3_write_byte(uint8_t value, int dc)
{
    /* ST7701S 3-wire protocol: one DC bit precedes each 8-bit value. */
    gpio_set_level(BOARD_LCD_SPI_SDA, dc);
    gpio_set_level(BOARD_LCD_SPI_SCLK, 0);
    spi3_delay();
    gpio_set_level(BOARD_LCD_SPI_SCLK, 1);
    spi3_delay();

    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(BOARD_LCD_SPI_SDA, (value >> bit) & 1);
        gpio_set_level(BOARD_LCD_SPI_SCLK, 0);
        spi3_delay();
        gpio_set_level(BOARD_LCD_SPI_SCLK, 1);
        spi3_delay();
    }
}

static void spi3_send(uint8_t cmd, const uint8_t *data, size_t len)
{
    gpio_set_level(BOARD_LCD_SPI_CS, 0);
    spi3_write_byte(cmd, 0); /* DC=0: command */

    for (size_t i = 0; i < len; ++i) {
        spi3_write_byte(data[i], 1); /* DC=1: parameter */
    }

    gpio_set_level(BOARD_LCD_SPI_SDA, 1);
    gpio_set_level(BOARD_LCD_SPI_SCLK, 1);
    gpio_set_level(BOARD_LCD_SPI_CS, 1);
}

static esp_err_t expander_write(uint8_t reg, uint8_t value)
{
    uint8_t buffer[2] = {reg, value};
    return i2c_master_write_to_device(
        I2C_NUM_0,
        BOARD_LCD_EXPANDER_ADDR,
        buffer,
        sizeof(buffer),
        pdMS_TO_TICKS(100)
    );
}

static esp_err_t expander_read(uint8_t reg, uint8_t *value)
{
    return i2c_bsp_write_read(BOARD_LCD_EXPANDER_ADDR, &reg, 1, value, 1);
}

static esp_err_t expander_update_output(uint8_t mask, uint8_t value)
{
    uint8_t output;
    esp_err_t ret = expander_read(0x01, &output);
    if (ret != ESP_OK) {
        return ret;
    }
    output = (uint8_t)((output & (uint8_t)~mask) | (value & mask));
    return expander_write(0x01, output);
}

static esp_err_t expander_configure_board_outputs(void)
{
    uint8_t direction;
    esp_err_t ret = expander_read(0x03, &direction);
    if (ret != ESP_OK) {
        return ret;
    }

    direction &= (uint8_t)~(BOARD_LCD_EXPANDER_RESET_BIT |
                            BOARD_TOUCH_RST_EXPANDER_BIT |
                            s_backlight_en_bit |
                            BOARD_SD_CS_EXPANDER_BIT);
    return expander_write(0x03, direction);
}

static esp_err_t expander_reset(bool high)
{
    uint8_t output = 0xFF;
    esp_err_t ret = expander_read(0x01, &output);

    if (ret != ESP_OK) {
        output = 0xFF;
    }

    if (high) {
        output |= BOARD_LCD_EXPANDER_RESET_BIT;
    } else {
        output &= ~BOARD_LCD_EXPANDER_RESET_BIT;
    }

    return expander_write(0x01, output);
}

static esp_err_t init_control_spi(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BOARD_LCD_SPI_CS) |
                        (1ULL << BOARD_LCD_SPI_SCLK) |
                        (1ULL << BOARD_LCD_SPI_SDA),
        .mode = GPIO_MODE_OUTPUT,
    };

    ESP_RETURN_ON_ERROR(
        gpio_config(&config), TAG, "3-wire SPI GPIO config failed"
    );

    gpio_set_level(BOARD_LCD_SPI_CS, 1);
    gpio_set_level(BOARD_LCD_SPI_SCLK, 1);
    gpio_set_level(BOARD_LCD_SPI_SDA, 1);

    for (size_t i = 0; i < sizeof(s_init) / sizeof(s_init[0]); ++i) {
        spi3_send(s_init[i].cmd, s_init[i].data, s_init[i].len);

        if (s_init[i].delay_ms != 0) {
            vTaskDelay(pdMS_TO_TICKS(s_init[i].delay_ms));
        }
    }

    return ESP_OK;
}

static esp_err_t init_rgb_panel(void)
{
    const esp_lcd_rgb_panel_config_t config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 16000000,
            .h_res = 480,
            .v_res = 480,
            .hsync_pulse_width = 10,
            .hsync_back_porch = 10,
            .hsync_front_porch = 20,
            .vsync_pulse_width = 10,
            .vsync_back_porch = 11,
            .vsync_front_porch = 10,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        /* Keep the RGB scanner on a complete frame while LVGL draws the
         * next frame, avoiding visible tearing/flicker. */
        .num_fbs = 2,
        .bounce_buffer_size_px = 4800,
        .dma_burst_size = 64,
        .hsync_gpio_num = BOARD_LCD_RGB_HSYNC,
        .vsync_gpio_num = BOARD_LCD_RGB_VSYNC,
        .de_gpio_num = BOARD_LCD_RGB_DE,
        .pclk_gpio_num = BOARD_LCD_RGB_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            BOARD_LCD_RGB_DATA0, BOARD_LCD_RGB_DATA1,
            BOARD_LCD_RGB_DATA2, BOARD_LCD_RGB_DATA3,
            BOARD_LCD_RGB_DATA4, BOARD_LCD_RGB_DATA5,
            BOARD_LCD_RGB_DATA6, BOARD_LCD_RGB_DATA7,
            BOARD_LCD_RGB_DATA8, BOARD_LCD_RGB_DATA9,
            BOARD_LCD_RGB_DATA10, BOARD_LCD_RGB_DATA11,
            BOARD_LCD_RGB_DATA12, BOARD_LCD_RGB_DATA13,
            BOARD_LCD_RGB_DATA14, BOARD_LCD_RGB_DATA15,
        },
        .flags = {
            .fb_in_psram = 1,
        },
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_rgb_panel(&config, &s_rgb_panel),
        TAG,
        "RGB panel create failed"
    );
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_init(s_rgb_panel), TAG, "RGB panel init failed"
    );
    /* disp_gpio_num is -1 on this board. Display enable is handled by the
     * ST7701S 0x29 command and the backlight GPIO, so disp_on_off() is not
     * supported by the generic RGB panel in this configuration. */

    return ESP_OK;
}

esp_err_t display_bsp_set_backlight(bool enabled)
{
    esp_err_t ret = expander_update_output(
        s_backlight_en_bit,
        enabled ? s_backlight_en_bit : 0);
    if (ret != ESP_OK) {
        return ret;
    }
    /* BL_PWM is active low: low enables current, high disables it. */
    gpio_set_level(s_backlight_pwm_gpio, enabled ? 0 : 1);
    return ESP_OK;
}

esp_err_t display_bsp_hardware_reset(void)
{
    ESP_RETURN_ON_ERROR(i2c_bsp_init(), TAG, "I2C init failed");
    /* Extend_IO4/P3 is SD DAT3/CS. Keep it high so the card stays in
     * native SD mode while the LCD and SD peripherals share the bus pins. */
    ESP_RETURN_ON_ERROR(
        expander_update_output(BOARD_SD_CS_EXPANDER_BIT,
                               BOARD_SD_CS_EXPANDER_BIT),
        TAG,
        "SD DAT3/CS idle-high setup failed"
    );
    ESP_RETURN_ON_ERROR(
        /* P0/P1/P2/P3 are outputs (LCD reset, TP reset, BL_EN, SD DAT3).
         * Preserve the input configuration of the remaining expander pins. */
        expander_configure_board_outputs(),
        TAG,
        "TCA9554 direction config failed"
    );
    ESP_RETURN_ON_ERROR(expander_reset(false), TAG, "LCD reset low failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(expander_reset(true), TAG, "LCD reset high failed");
    ESP_RETURN_ON_ERROR(
        expander_update_output(BOARD_SD_CS_EXPANDER_BIT,
                               BOARD_SD_CS_EXPANDER_BIT),
        TAG,
        "SD DAT3/CS restore-high failed"
    );
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t display_bsp_stage1_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_bsp_init(), TAG, "I2C init failed");
    /* Configure TCA9554 P2 before writing the initial BL_EN state.  The
     * expander defaults to inputs after reset, so a latch write alone does
     * not drive the backlight enable line. */
    ESP_RETURN_ON_ERROR(
        expander_configure_board_outputs(), TAG,
        "Backlight expander output config failed"
    );
    ESP_RETURN_ON_ERROR(
        gpio_set_direction(s_backlight_pwm_gpio, GPIO_MODE_OUTPUT),
        TAG,
        "Backlight PWM GPIO init failed"
    );

    return display_bsp_set_backlight(false);
}

esp_err_t display_bsp_stage2_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(
        display_bsp_stage1_init(), TAG, "Backlight init failed"
    );
    ESP_RETURN_ON_ERROR(
        display_bsp_hardware_reset(), TAG, "LCD reset failed"
    );
    ESP_RETURN_ON_ERROR(
        init_control_spi(), TAG, "Control SPI init failed"
    );
    ESP_RETURN_ON_ERROR(
        init_rgb_panel(), TAG, "RGB init failed"
    );
    ESP_RETURN_ON_ERROR(
        display_bsp_set_backlight(true), TAG, "Backlight on failed"
    );

    s_initialized = true;
    return ESP_OK;
}

esp_err_t display_bsp_fill_color(uint16_t rgb565)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    static uint16_t line[480];
    for (int i = 0; i < 480; ++i) {
        line[i] = rgb565;
    }

    for (int y = 0; y < 480; ++y) {
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(s_rgb_panel, 0, y, 480, y + 1, line),
            TAG,
            "Fill color failed"
        );
    }

    return ESP_OK;
}

esp_err_t display_bsp_draw_bitmap(
    int x_start,
    int y_start,
    int x_end,
    int y_end,
    const void *color_data
)
{
    if (!s_initialized || color_data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_lcd_panel_draw_bitmap(
        s_rgb_panel,
        x_start,
        y_start,
        x_end,
        y_end,
        color_data
    );
}
