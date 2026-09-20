#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <cstring>

#include "i2c_bsp.h"

using namespace esp_panel::board;

namespace {

// Schematic: BL_EN is connected to TCA9554 P2 (Extend_IO2).
constexpr uint8_t kBacklightEnableExpanderPin = 2;
constexpr uint8_t kBacklightFeedbackPin = 38;
constexpr uint16_t kLcdWidth = 480;
constexpr uint16_t kLcdHeight = 480;
constexpr uint16_t kWhiteBlockLines = 10;

bool setBacklightEnable(Board *board, bool enabled)
{
    if (board == nullptr || board->getIO_Expander() == nullptr) {
        return false;
    }

    auto expander = board->getIO_Expander()->getBase();
    if (expander == nullptr) {
        return false;
    }

    expander->pinMode(kBacklightEnableExpanderPin, OUTPUT);
    return expander->digitalWrite(
        kBacklightEnableExpanderPin,
        enabled ? HIGH : LOW
    );
}

[[noreturn]] void stopWithError(const char *message, Board *board = nullptr)
{
    Serial.println(message);
    setBacklightEnable(board, false);
    digitalWrite(kBacklightFeedbackPin, HIGH);

    while (true) {
        delay(1000);
    }
}

} // namespace

void setup()
{
    // BL_PWM is active low. HIGH is off; LOW is the maximum software brightness.
    digitalWrite(kBacklightFeedbackPin, HIGH);
    pinMode(kBacklightFeedbackPin, OUTPUT);

    Serial.begin(115200);
    delay(500);
    Serial.println("HXESP4848 LCD test starting");

    // TCA9554 uses I2C0 and controls the LCD reset line on P0.
    if (!I2CBSP::begin()) {
        stopWithError("I2C initialization failed");
    }

    Board *board = new Board();
    if ((board == nullptr) || !board->init()) {
        stopWithError("Board initialization failed");
    }

    // This resets ST7701S, sends the vendor command table and starts the RGB bus.
    if (!board->begin()) {
        stopWithError("LCD initialization failed", board);
    }

    auto lcd = board->getLCD();
    if (lcd == nullptr) {
        stopWithError("LCD object is unavailable", board);
    }

    static uint16_t whiteBlock[kLcdWidth * kWhiteBlockLines];
    memset(whiteBlock, 0xFF, sizeof(whiteBlock));

    for (uint16_t y = 0; y < kLcdHeight; y += kWhiteBlockLines) {
        if (!lcd->drawBitmap(
                0,
                y,
                kLcdWidth,
                kWhiteBlockLines,
                reinterpret_cast<const uint8_t *>(whiteBlock)
            )) {
            stopWithError("LCD white screen draw failed", board);
        }
    }

    delay(100);
    digitalWrite(kBacklightFeedbackPin, LOW);
    if (!setBacklightEnable(board, true)) {
        stopWithError("Backlight enable failed", board);
    }

    Serial.println("LCD initialized: ST7701S, RGB565, 480 x 480");
    Serial.println("Pure white screen displayed");
    Serial.println("Backlight brightness: 100%");
}

void loop()
{
    delay(1000);
}
