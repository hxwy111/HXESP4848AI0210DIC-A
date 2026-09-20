#pragma once

#include <Arduino.h>

/*
 * GT911 touch point and driver used by the HXESP4848 UI demo.
 */
struct GT911Point {
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t pressure = 0; // GT911 contact-area byte, used as a pressure proxy
    uint8_t id = 0;
    uint8_t points = 0;
    bool pressed = false;
};

class GT911Touch {
public:
    bool begin();
    bool readPoint(GT911Point &point);

private:
    bool tcaRead(uint8_t reg, uint8_t &value);
    bool tcaWrite(uint8_t reg, uint8_t value);
    bool readRegister(uint8_t address, uint16_t reg, uint8_t *data, size_t length);
    bool writeRegister(uint8_t address, uint16_t reg, const uint8_t *data, size_t length);
    bool configureExpander();
    bool resetController();
    bool identifyController();

    uint8_t address_ = 0x5D;
    uint16_t width_ = 480;
    uint16_t height_ = 480;
    GT911Point cached_{};
    bool initialized_ = false;
};
