#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>

#include "lvgl_v8_port.h"
#include "gt911_touch.h"
#include "i2c_bsp.h"
#include "ui_app.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static GT911Touch touch;
// Schematic: BL_EN is connected to TCA9554 P2 (Extend_IO2).
static constexpr uint8_t kTca9554Address = 0x20;
static constexpr uint8_t kTca9554OutputRegister = 0x01;
static constexpr uint8_t kTca9554ConfigRegister = 0x03;
static constexpr uint8_t kBacklightEnableExpanderPin = 2;
static constexpr uint8_t kBacklightEnableBit = 1u << kBacklightEnableExpanderPin;
static constexpr uint8_t kBacklightPwmPin = 38;

static bool setBacklightEnable(Board *board, bool enabled)
{
    if (board == nullptr || board->getIO_Expander() == nullptr) {
        return false;
    }
    auto expander = board->getIO_Expander()->getBase();
    if (expander == nullptr) {
        return false;
    }

    uint8_t direction = 0;
    if (I2CBSP::writeRead(kTca9554Address,
                          &kTca9554ConfigRegister,
                          sizeof(kTca9554ConfigRegister),
                          &direction,
                          sizeof(direction)) != ESP_OK) {
        Serial.println("TCA9554: read CONFIG register failed");
        return false;
    }
    direction = static_cast<uint8_t>(direction & ~kBacklightEnableBit);
    const uint8_t configData[] = {kTca9554ConfigRegister, direction};
    if (I2CBSP::write(kTca9554Address, configData, sizeof(configData)) != ESP_OK) {
        Serial.println("TCA9554: write CONFIG register failed");
        return false;
    }

    uint8_t output = 0;
    if (I2CBSP::writeRead(kTca9554Address,
                          &kTca9554OutputRegister,
                          sizeof(kTca9554OutputRegister),
                          &output,
                          sizeof(output)) != ESP_OK) {
        Serial.println("TCA9554: read BL_EN output failed");
        return false;
    }
    output = enabled
             ? static_cast<uint8_t>(output | kBacklightEnableBit)
             : static_cast<uint8_t>(output & ~kBacklightEnableBit);
    const uint8_t outputData[] = {kTca9554OutputRegister, output};
    if (I2CBSP::write(kTca9554Address, outputData, sizeof(outputData)) != ESP_OK) {
        Serial.println("TCA9554: write BL_EN output failed");
        return false;
    }

    uint8_t verify = 0;
    if (I2CBSP::writeRead(kTca9554Address,
                          &kTca9554OutputRegister,
                          sizeof(kTca9554OutputRegister),
                          &verify,
                          sizeof(verify)) != ESP_OK) {
        Serial.println("TCA9554: verify BL_EN output failed");
        return false;
    }
    const bool actualEnabled = (verify & kBacklightEnableBit) != 0;
    Serial.printf("Backlight: BL_EN=P2=%s, BL_PWM=GPIO38=%s (output=0x%02X)\n",
                  actualEnabled ? "HIGH" : "LOW",
                  enabled ? "LOW/ON" : "HIGH/OFF",
                  verify);
    return actualEnabled == enabled;
}

static bool forceBacklightOn(Board *board)
{
    // Match 10_CST3530_TP_Test-int: P2 high enables BL_EN and GPIO38 low
    // enables the AP3032 PWM/current-control path.
    if (!setBacklightEnable(board, true)) {
        return false;
    }
    pinMode(kBacklightPwmPin, OUTPUT);
    digitalWrite(kBacklightPwmPin, LOW);
    if (board->getBacklight() && !board->getBacklight()->on()) {
        Serial.println("Backlight driver on() failed; GPIO38 remains LOW");
    }
    digitalWrite(kBacklightPwmPin, LOW);
    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("HXESP4848 GT911 UI demo starting");

    pinMode(kBacklightPwmPin, OUTPUT);
    digitalWrite(kBacklightPwmPin, HIGH);

    /* The board configuration deliberately skips I2C host installation for
     * the TCA9554. Install the shared I2C0 driver before Board::begin(). */
    if (!I2CBSP::begin()) {
        Serial.println("I2C initialization failed");
        while (true) delay(1000);
    }
    if (I2CBSP::probe(kTca9554Address) != ESP_OK) {
        Serial.println("TCA9554 0x20 is not responding");
        while (true) delay(1000);
    }

    Board *board = new Board();
    if (!board || !board->init() || !board->begin()) {
        Serial.println("Board initialization failed");
        while (true) delay(1000);
    }

    // Board::begin() may reset the expander. Restore P2 as an output and keep
    // BL_EN off until touch and LVGL initialization are complete.
    if (!setBacklightEnable(board, false)) {
        Serial.println("Backlight expander initialization failed");
        while (true) delay(1000);
    }

    GT911Touch *touchDevice = nullptr;
    if (touch.begin()) touchDevice = &touch;
    else Serial.println("GT911 unavailable; UI will run without touch");

    if (!lvgl_port_init(board->getLCD(), touchDevice)) {
        Serial.println("LVGL initialization failed");
        while (true) delay(1000);
    }

    lvgl_port_lock(-1);
    ui_app_create();
    lvgl_port_unlock();

    if (!forceBacklightOn(board)) {
        Serial.println("Backlight expander enable failed");
        while (true) delay(1000);
    }
    Serial.println("Backlight on: TCA9554 P2 BL_EN=HIGH, GPIO38 BL_PWM=LOW");
}

void loop()
{
    delay(10);
}
