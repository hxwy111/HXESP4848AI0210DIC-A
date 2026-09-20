#include "gt911_touch.h"
#include "i2c_bsp.h"

namespace {
constexpr uint8_t kExpander = 0x20;
constexpr uint8_t kGt911A = 0x14;
constexpr uint8_t kGt911B = 0x5D;
constexpr uint8_t kOutput = 0x01;
constexpr uint8_t kConfig = 0x03;
constexpr uint8_t kResetBit = 1u << 1; // TCA9554 P1
constexpr uint8_t kBacklightEnableBit = 1u << 2; // TCA9554 P2 / BL_EN
}

bool GT911Touch::tcaRead(uint8_t reg, uint8_t &value)
{
    return I2CBSP::writeRead(kExpander, &reg, 1, &value, 1) == ESP_OK;
}

bool GT911Touch::tcaWrite(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return I2CBSP::write(kExpander, data, sizeof(data)) == ESP_OK;
}

bool GT911Touch::readRegister(uint8_t address, uint16_t reg, uint8_t *data, size_t length)
{
    const uint8_t command[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg)};
    return I2CBSP::writeRead(address, command, sizeof(command), data, length) == ESP_OK;
}

bool GT911Touch::writeRegister(uint8_t address, uint16_t reg, const uint8_t *data, size_t length)
{
    if (!data || length > 32) return false;
    uint8_t packet[34] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg)};
    memcpy(packet + 2, data, length);
    return I2CBSP::write(address, packet, length + 2) == ESP_OK;
}

bool GT911Touch::configureExpander()
{
    uint8_t direction = 0;
    uint8_t output = 0;
    if (!tcaRead(kConfig, direction)) return false;
    // TP_INT is GPIO45. P1 (TP_RST) and P2 (BL_EN) are both outputs.
    direction = static_cast<uint8_t>(direction & ~(kResetBit | kBacklightEnableBit));
    if (!tcaWrite(kConfig, direction)) return false;

    // Keep the active-high BL_EN low until LCD and LVGL initialization finish.
    if (!tcaRead(kOutput, output)) return false;
    output = static_cast<uint8_t>(output & ~kBacklightEnableBit);
    return tcaWrite(kOutput, output);
}

bool GT911Touch::resetController()
{
    uint8_t output = 0;
    if (!tcaRead(kOutput, output)) return false;
    if (!tcaWrite(kOutput, static_cast<uint8_t>(output & ~kResetBit))) return false;
    delay(10);
    if (!tcaWrite(kOutput, static_cast<uint8_t>(output | kResetBit))) return false;
    delay(200);
    return true;
}

bool GT911Touch::identifyController()
{
    const uint8_t candidates[] = {kGt911A, kGt911B};
    bool found = false;
    for (uint8_t candidate : candidates) {
        uint8_t id[4] = {};
        if (I2CBSP::probe(candidate) == ESP_OK && readRegister(candidate, 0x8140, id, sizeof(id)) &&
            id[0] == '9' && id[1] == '1' && id[2] == '1') {
            if (!found || candidate == kGt911B) address_ = candidate;
            found = true;
        }
    }
    if (!found) return false;

    uint8_t config[5] = {};
    if (readRegister(address_, 0x8047, config, sizeof(config))) {
        const uint16_t w = config[1] | (static_cast<uint16_t>(config[2]) << 8);
        const uint16_t h = config[3] | (static_cast<uint16_t>(config[4]) << 8);
        if (w && h) { width_ = w; height_ = h; }
    }
    Serial.printf("GT911 selected address: 0x%02X, config %ux%u\n", address_, width_, height_);
    return true;
}

bool GT911Touch::begin()
{
    if (!I2CBSP::begin() || I2CBSP::probe(kExpander) != ESP_OK ||
        !configureExpander() || !resetController() || !identifyController()) {
        Serial.println("GT911 initialization failed");
        initialized_ = false;
        return false;
    }
    initialized_ = true;
    cached_ = {};
    Serial.println("GT911 initialization succeeded");
    return true;
}

bool GT911Touch::readPoint(GT911Point &point)
{
    if (!initialized_) return false;

    uint8_t status = 0;
    if (!readRegister(address_, 0x814E, &status, 1)) {
        point = cached_;
        return true;
    }

    if (!(status & 0x80)) {
        cached_.points = 0;
        cached_.pressed = false;
        cached_.pressure = 0;
        point = cached_;
        return true;
    }

    uint8_t count = status & 0x0F;
    cached_.points = count;
    if (count) {
        uint8_t raw[8] = {};
        if (readRegister(address_, 0x814F, raw, sizeof(raw))) {
            cached_.id = raw[0];
            cached_.x = raw[1] | (static_cast<uint16_t>(raw[2]) << 8);
            cached_.y = raw[3] | (static_cast<uint16_t>(raw[4]) << 8);
            const uint16_t area = raw[5] | (static_cast<uint16_t>(raw[6]) << 8);
            cached_.pressure = static_cast<uint8_t>(area > 255 ? 255 : area);
            if (cached_.x >= 480) cached_.x = 479;
            if (cached_.y >= 480) cached_.y = 479;
            cached_.pressed = true;
        }
    } else {
        cached_.pressed = false;
        cached_.pressure = 0;
        cached_.points = 0;
    }

    const uint8_t clear = 0;
    writeRegister(address_, 0x814E, &clear, 1);
    point = cached_;
    return true;
}
