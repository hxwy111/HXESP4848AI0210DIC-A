#include <Arduino.h>
#include <esp_display_panel.hpp>

#include "lvgl_v8_port.h"
#include "i2c_bsp.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

namespace {

// Schematic: BL_EN is connected to TCA9554 P2 (Extend_IO2).
constexpr uint8_t kBacklightEnableExpanderPin = 2;
constexpr uint8_t kBacklightPwmPin = 38;
constexpr uint8_t kPageCount = 3;
constexpr uint32_t kPageDurationMs = 3000;

lv_obj_t *gScreen = nullptr;
uint8_t gPage = 0;
Board *gBoard = nullptr;

bool setBacklightEnable(bool enabled)
{
    if (gBoard == nullptr || gBoard->getIO_Expander() == nullptr) {
        return false;
    }

    auto expander = gBoard->getIO_Expander()->getBase();
    if (expander == nullptr) {
        return false;
    }

    expander->pinMode(kBacklightEnableExpanderPin, OUTPUT);
    return expander->digitalWrite(
        kBacklightEnableExpanderPin,
        enabled ? HIGH : LOW
    );
}

[[noreturn]] void stopWithError(const char *message)
{
    Serial.println(message);
    setBacklightEnable(false);
    digitalWrite(kBacklightPwmPin, HIGH);
    while (true) {
        delay(1000);
    }
}

lv_color_t rgb565(uint16_t value)
{
    lv_color_t color;
    color.full = value;
    return color;
}

void setPanelBackground()
{
    lv_obj_set_style_bg_color(gScreen, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(gScreen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(gScreen, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *makeLabel(const char *text, int16_t x, int16_t y, int16_t width, int16_t height,
                    lv_color_t color, const lv_font_t *font = LV_FONT_DEFAULT)
{
    lv_obj_t *label = lv_label_create(gScreen);
    lv_label_set_text(label, text);
    lv_obj_set_size(label, width, height);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

void makeHeader(const char *title, const char *subtitle)
{
    // Keep the title clear of the page counter on the 480x480 panel.
    lv_obj_t *titleLabel = makeLabel(title, 40, 18, 400, 38,
                                     lv_color_white(), &lv_font_montserrat_30);
    lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *subtitleLabel = makeLabel(
        subtitle, 40, 60, 400, 24, lv_color_hex(0x9CA3AF)
    );
    lv_obj_set_style_text_align(subtitleLabel, LV_TEXT_ALIGN_CENTER, 0);

}

void createFontPage()
{
    makeHeader("FONT TEST", "Large and small characters");

    lv_obj_t *large = makeLabel("Aa 0123456789", 40, 108, 400, 42,
                                lv_color_white(), &lv_font_montserrat_30);
    lv_obj_set_style_text_align(large, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *large2 = makeLabel("RGB565", 40, 154, 400, 42,
                                 lv_color_hex(0x7DD3FC), &lv_font_montserrat_30);
    lv_obj_set_style_text_align(large2, LV_TEXT_ALIGN_CENTER, 0);

    makeLabel("Small: ABCDEFGHIJKLMNOPQRSTUVWXYZ", 40, 222, 400, 22,
              lv_color_white());
    makeLabel("Small: abcdefghijklmnopqrstuvwxyz", 40, 250, 400, 22,
              lv_color_white());
    makeLabel("Numbers: 0123456789  -  +  =  /  %", 40, 278, 400, 22,
              lv_color_white());
    makeLabel("Symbols: ! @ # $ % & * ( ) [ ] { }", 40, 306, 400, 22,
              lv_color_white());

    lv_obj_t *line = lv_obj_create(gScreen);
    lv_obj_set_size(line, 400, 2);
    lv_obj_set_pos(line, 40, 350);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x4B5563), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);

    lv_obj_t *displayInfo = makeLabel(
        "480 x 480   ST7701S   RGB display",
        40,
        388,
        400,
        24,
        lv_color_hex(0xD1D5DB)
    );
    lv_obj_set_style_text_align(displayInfo, LV_TEXT_ALIGN_CENTER, 0);
}

void createColorSwatch(int16_t x, int16_t y, const char *name, uint16_t color)
{
    lv_obj_t *swatch = lv_obj_create(gScreen);
    lv_obj_set_size(swatch, 132, 66);
    lv_obj_set_pos(swatch, x, y);
    lv_obj_set_style_bg_color(swatch, rgb565(color), 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(swatch, lv_color_hex(0x6B7280), 0);
    lv_obj_set_style_border_width(swatch, 1, 0);
    lv_obj_set_style_radius(swatch, 4, 0);
    lv_obj_set_style_pad_all(swatch, 0, 0);
    lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(gScreen);
    lv_label_set_text_fmt(label, "%s  %04X", name, color);
    lv_obj_set_size(label, 132, 22);
    lv_obj_set_pos(label, x, y + 70);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

void createCommonColorsPage()
{
    makeHeader("COLOR TEST", "Common RGB565 colors");

    static const struct {
        const char *name;
        uint16_t value;
    } colors[] = {
        {"BLACK", 0x0000}, {"WHITE", 0xFFFF}, {"RED", 0xF800},
        {"GREEN", 0x07E0}, {"BLUE", 0x001F}, {"YELLOW", 0xFFE0},
        {"CYAN", 0x07FF}, {"MAGENTA", 0xF81F}, {"GRAY", 0x8410},
    };

    for (uint8_t i = 0; i < 9; ++i) {
        const int16_t column = i % 3;
        const int16_t row = i / 3;
        createColorSwatch(48 + column * 132, 100 + row * 112,
                          colors[i].name, colors[i].value);
    }
}

void createLevelRow(int16_t y, const char *name, const uint16_t *values, uint8_t count)
{
    makeLabel(name, 38, y + 20, 58, 24, lv_color_white());
    const int16_t width = 58;
    const int16_t gap = 8;
    for (uint8_t i = 0; i < count; ++i) {
        const int16_t x = 102 + i * (width + gap);
        lv_obj_t *block = lv_obj_create(gScreen);
        lv_obj_set_size(block, width, 54);
        lv_obj_set_pos(block, x, y);
        lv_obj_set_style_bg_color(block, rgb565(values[i]), 0);
        lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(block, 1, 0);
        lv_obj_set_style_border_color(block, lv_color_hex(0x4B5563), 0);
        lv_obj_set_style_radius(block, 3, 0);
        lv_obj_set_style_pad_all(block, 0, 0);

        lv_obj_t *value = lv_label_create(gScreen);
        lv_label_set_text_fmt(value, "%04X", values[i]);
        lv_obj_set_size(value, width, 20);
        lv_obj_set_pos(value, x, y + 56);
        lv_obj_set_style_text_color(value, lv_color_hex(0xD1D5DB), 0);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void createRgb565Page()
{
    makeHeader("RGB LEVELS", "16-bit RGB565 source");

    static const uint16_t red[] = {0x0000, 0x4000, 0x8000, 0xC000, 0xF800};
    static const uint16_t green[] = {0x0000, 0x0200, 0x0400, 0x0600, 0x07E0};
    static const uint16_t blue[] = {0x0000, 0x0008, 0x0010, 0x0018, 0x001F};
    static const uint16_t gray[] = {0x0000, 0x4208, 0x8410, 0xC618, 0xFFFF};

    createLevelRow(96, "RED", red, 5);
    createLevelRow(174, "GREEN", green, 5);
    createLevelRow(252, "BLUE", blue, 5);
    createLevelRow(330, "GRAY", gray, 5);
}

void showPage()
{
    if (!lvgl_port_lock(-1)) {
        stopWithError("LVGL mutex lock failed");
    }

    lv_obj_clean(gScreen);
    setPanelBackground();

    switch (gPage) {
        case 0:
            Serial.println("UI TEST 1/3: large and small characters");
            createFontPage();
            break;
        case 1:
            Serial.println("UI TEST 2/3: common colors");
            createCommonColorsPage();
            break;
        default:
            Serial.println("UI TEST 3/3: RGB565 levels");
            createRgb565Page();
            break;
    }

    lvgl_port_unlock();
}

void pageTimerCallback(lv_timer_t *)
{
    gPage = static_cast<uint8_t>((gPage + 1) % kPageCount);
    showPage();
}

}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("HXESP4848 display UI test starting");

    pinMode(kBacklightPwmPin, OUTPUT);
    digitalWrite(kBacklightPwmPin, HIGH);

    if (!I2CBSP::begin()) {
        stopWithError("I2C initialization failed");
    }

    Board *board = new Board();
    if ((board == nullptr) || !board->init()) {
        stopWithError("Board initialization failed");
    }
    gBoard = board;
    if (!board->begin()) {
        stopWithError("LCD board begin failed");
    }

    LCD *lcd = board->getLCD();
    Backlight *backlight = board->getBacklight();
    if ((lcd == nullptr) || (backlight == nullptr)) {
        stopWithError("LCD or backlight unavailable");
    }
    if (!lvgl_port_init(lcd)) {
        stopWithError("LVGL initialization failed");
    }

    gScreen = lv_scr_act();
    showPage();
    if (lvgl_port_lock(-1)) {
        lv_timer_create(pageTimerCallback, kPageDurationMs, nullptr);
        lvgl_port_unlock();
    } else {
        stopWithError("LVGL mutex lock failed");
    }

    if (!setBacklightEnable(true)) {
        stopWithError("Backlight boost enable failed");
    }
    if (!backlight->on()) {
        stopWithError("Backlight enable failed");
    }
    // BL_PWM is active low on the AP3032 path; BL_EN=P2 alone is not enough.
    digitalWrite(kBacklightPwmPin, LOW);
    Serial.println("Display UI initialized");
}

void loop()
{
    delay(10);
}
