#include <Arduino.h>
#include <esp_display_panel.hpp>

#include "i2c_bsp.h"
#include "lvgl_v8_port.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

namespace {

constexpr uint8_t kTca9554Address = 0x20;
// GT911 的两个可选 7 位地址。实测两个地址均能读到“911”身份。
constexpr uint8_t kGt911AddressA = 0x14;
constexpr uint8_t kGt911AddressB = 0x5D;
constexpr uint8_t kGt911MaxPoints = 5;
constexpr size_t kGt911FrameSize = 1 + kGt911MaxPoints * 8;
constexpr uint16_t kDisplayWidth = 480;
constexpr uint16_t kDisplayHeight = 480;
// 实测触摸帧坐标按屏幕尺寸输出；GT911 配置区的 1024x600 可能是旧配置。
constexpr uint16_t kTouchCoordinateWidth = 480;
constexpr uint16_t kTouchCoordinateHeight = 480;
// Schematic: BL_EN is connected to TCA9554 P2 (Extend_IO2).
constexpr uint8_t kBacklightEnableExpanderPin = 2;
constexpr uint8_t kBacklightPwmPin = 38;

// 当前实际使用的 GT911 地址。若地址选择时序确认后只有一个地址应答，改为对应值即可。
uint8_t gGt911Address = kGt911AddressB;
uint16_t gGt911Width = 1024;
uint16_t gGt911Height = 600;

Board *gBoard = nullptr;
LCD *gLcd = nullptr;
Backlight *gBacklight = nullptr;
lv_obj_t *gScreen = nullptr;
lv_obj_t *gRawLabel = nullptr;
lv_obj_t *gMappedLabel = nullptr;

// TCA9554 寄存器地址：输出、方向配置。
constexpr uint8_t kTca9554OutputRegister = 0x01;
constexpr uint8_t kTca9554ConfigRegister = 0x03;

// TCA9554 P1 连接触摸芯片的低有效复位脚 RSTn。
constexpr uint8_t kTpResetBit = (1u << 1);
// TCA9554 P2 连接背光使能 BL_EN，高电平有效；本示例轮询触摸数据，不使用 TP_INT。
constexpr uint8_t kBacklightEnableBit = (1u << 2);

lv_color_t rgb(uint32_t value)
{
    return lv_color_hex(value);
}

[[noreturn]] void stopWithError(const char *message)
{
    Serial.println(message);
    if (gBoard != nullptr && gBoard->getIO_Expander() != nullptr) {
        auto expander = gBoard->getIO_Expander()->getBase();
        if (expander != nullptr) {
            expander->pinMode(kBacklightEnableExpanderPin, OUTPUT);
            expander->digitalWrite(kBacklightEnableExpanderPin, LOW);
        }
    }
    digitalWrite(kBacklightPwmPin, HIGH);
    while (true) {
        delay(1000);
    }
}

bool tca9554ReadRegister(uint8_t reg, uint8_t &value)
{
    // 先发送寄存器地址，再使用重复起始条件读取一个字节。
    return I2CBSP::writeRead(
               kTca9554Address, &reg, sizeof(reg), &value, sizeof(value)
           ) == ESP_OK;
}

bool tca9554WriteRegister(uint8_t reg, uint8_t value)
{
    // 一次 I2C 事务写入“寄存器地址 + 寄存器值”。
    const uint8_t data[] = {reg, value};
    return I2CBSP::write(kTca9554Address, data, sizeof(data)) == ESP_OK;
}

bool configureTca9554ForTouchAndBacklight()
{
    // 配置触摸复位和背光使能所需的 TCA9554 端口方向。
    uint8_t direction = 0;
    if (!tca9554ReadRegister(kTca9554ConfigRegister, direction)) {
        Serial.println("TCA9554: read CONFIG register failed");
        return false;
    }

    // 采用“读-修改-写”：P1=TP_RST、P2=BL_EN，二者均设置为输出。
    direction = static_cast<uint8_t>(direction & ~(kTpResetBit | kBacklightEnableBit));

    if (!tca9554WriteRegister(kTca9554ConfigRegister, direction)) {
        Serial.println("TCA9554: write CONFIG register failed");
        return false;
    }

    // Keep BL_EN low while the panel and touch controller are initialized.
    uint8_t output = 0;
    if (!tca9554ReadRegister(kTca9554OutputRegister, output)) {
        Serial.println("TCA9554: read OUTPUT register failed");
        return false;
    }
    output = static_cast<uint8_t>(output & ~kBacklightEnableBit);
    if (!tca9554WriteRegister(kTca9554OutputRegister, output)) {
        Serial.println("TCA9554: write OUTPUT register failed");
        return false;
    }

    uint8_t verify = 0;
    if (!tca9554ReadRegister(kTca9554ConfigRegister, verify)) {
        Serial.println("TCA9554: verify CONFIG register failed");
        return false;
    }

    return (verify & kTpResetBit) == 0 &&
           (verify & kBacklightEnableBit) == 0;
}

bool setBacklightEnable(bool enabled)
{
    uint8_t output = 0;
    if (!tca9554ReadRegister(kTca9554OutputRegister, output)) {
        Serial.println("TCA9554: read BL_EN output failed");
        return false;
    }

    if (enabled) {
        output = static_cast<uint8_t>(output | kBacklightEnableBit);
    } else {
        output = static_cast<uint8_t>(output & ~kBacklightEnableBit);
    }
    if (!tca9554WriteRegister(kTca9554OutputRegister, output)) {
        Serial.println("TCA9554: write BL_EN output failed");
        return false;
    }

    uint8_t verify = 0;
    if (!tca9554ReadRegister(kTca9554OutputRegister, verify)) {
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

bool resetTouch()
{
    // 通过 TCA9554 P1 给触摸芯片产生一次低有效复位脉冲。
    uint8_t output = 0;
    if (!tca9554ReadRegister(kTca9554OutputRegister, output)) {
        Serial.println("TCA9554: read OUTPUT register failed");
        return false;
    }

    // 触摸芯片 RSTn 为低电平有效；只修改 P1，保留其他输出端口状态。
    output = static_cast<uint8_t>(output & ~kTpResetBit);
    if (!tca9554WriteRegister(kTca9554OutputRegister, output)) {
        Serial.println("TCA9554: assert TP_RST failed");
        return false;
    }
    delay(10);

    output = static_cast<uint8_t>(output | kTpResetBit);
    if (!tca9554WriteRegister(kTca9554OutputRegister, output)) {
        Serial.println("TCA9554: release TP_RST failed");
        return false;
    }
    delay(200);
    return true;
}

bool read16BitRegister(uint8_t deviceAddress, uint16_t reg,
                       uint8_t *data, size_t length)
{
    // GT911 使用高字节在前的 16 位寄存器地址。
    const uint8_t command[] = {
        static_cast<uint8_t>(reg >> 8),
        static_cast<uint8_t>(reg & 0xFF)
    };

    // 写入寄存器地址后使用重复起始条件连续读取，避免 STOP 导致地址指针丢失。
    return I2CBSP::writeRead(
               deviceAddress,
               command,
               sizeof(command),
               data,
               length
           ) == ESP_OK;
}

bool write16BitRegister(uint8_t deviceAddress, uint16_t reg,
                        const uint8_t *data, size_t length)
{
    // GT911 写寄存器时发送“16 位寄存器地址 + 数据”。
    if ((data == nullptr) || (length == 0)) {
        return false;
    }
    uint8_t command[2 + 32] = {};
    if (length > sizeof(command) - 2) {
        return false;
    }
    command[0] = static_cast<uint8_t>(reg >> 8);
    command[1] = static_cast<uint8_t>(reg & 0xFF);
    memcpy(command + 2, data, length);
    return I2CBSP::write(deviceAddress, command, length + 2) == ESP_OK;
}

bool identifyGt911()
{
    uint8_t id[4] = {};
    bool found = false;
    const uint8_t candidates[] = {kGt911AddressA, kGt911AddressB};

    for (uint8_t address : candidates) {
        memset(id, 0, sizeof(id));
        if (I2CBSP::probe(address) == ESP_OK &&
            read16BitRegister(address, 0x8140, id, sizeof(id))) {
            if (id[0] == '9' && id[1] == '1' && id[2] == '1') {
                // 两个地址都应答时优先使用 GT911 常见的默认地址 0x5D。
                if (!found || address == kGt911AddressB) {
                    gGt911Address = address;
                }
                found = true;
            }
        }
    }

    if (!found) {
        Serial.println("GT911 not detected");
        return false;
    }

    uint8_t config[5] = {};
    if (read16BitRegister(gGt911Address, 0x8047, config, sizeof(config))) {
        const uint16_t width = config[1] | (static_cast<uint16_t>(config[2]) << 8);
        const uint16_t height = config[3] | (static_cast<uint16_t>(config[4]) << 8);
        if (width != 0 && height != 0) {
            gGt911Width = width;
            gGt911Height = height;
        }
    }

    Serial.printf("GT911 selected address: 0x%02X\n", gGt911Address);
    return true;
}

void createTouchUi()
{
    gScreen = lv_scr_act();
    lv_obj_set_style_bg_color(gScreen, rgb(0x111827), 0);
    lv_obj_set_style_bg_opa(gScreen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(gScreen, LV_OBJ_FLAG_SCROLLABLE);

    // 圆屏内圈参考线，便于观察触摸点是否能覆盖整个有效区域。
    lv_obj_t *guide = lv_obj_create(gScreen);
    lv_obj_set_size(guide, 360, 360);
    lv_obj_set_pos(guide, 60, 60);
    lv_obj_set_style_bg_opa(guide, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(guide, rgb(0x374151), 0);
    lv_obj_set_style_border_width(guide, 1, 0);
    lv_obj_set_style_radius(guide, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(guide, 0, 0);
    lv_obj_clear_flag(guide, LV_OBJ_FLAG_SCROLLABLE);

    // 中心十字线作为圆屏坐标参考。
    lv_obj_t *horizontal = lv_obj_create(gScreen);
    lv_obj_set_size(horizontal, 300, 1);
    lv_obj_set_pos(horizontal, 90, 239);
    lv_obj_set_style_bg_color(horizontal, rgb(0x374151), 0);
    lv_obj_set_style_bg_opa(horizontal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(horizontal, 0, 0);
    lv_obj_set_style_pad_all(horizontal, 0, 0);

    lv_obj_t *vertical = lv_obj_create(gScreen);
    lv_obj_set_size(vertical, 1, 300);
    lv_obj_set_pos(vertical, 239, 90);
    lv_obj_set_style_bg_color(vertical, rgb(0x374151), 0);
    lv_obj_set_style_bg_opa(vertical, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(vertical, 0, 0);
    lv_obj_set_style_pad_all(vertical, 0, 0);

    // 信息面板放在圆屏上方中间，避免矩形布局在圆屏边缘被裁剪。
    lv_obj_t *infoPanel = lv_obj_create(gScreen);
    lv_obj_set_size(infoPanel, 360, 78);
    lv_obj_set_pos(infoPanel, 60, 66);
    lv_obj_set_style_bg_color(infoPanel, rgb(0x111827), 0);
    lv_obj_set_style_bg_opa(infoPanel, LV_OPA_90, 0);
    lv_obj_set_style_border_color(infoPanel, rgb(0x4B5563), 0);
    lv_obj_set_style_border_width(infoPanel, 1, 0);
    lv_obj_set_style_radius(infoPanel, 24, 0);
    lv_obj_set_style_pad_all(infoPanel, 6, 0);
    lv_obj_clear_flag(infoPanel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(infoPanel);
    lv_label_set_text(title, "GT911 TOUCH TEST");
    lv_obj_set_width(title, 348);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title, 0, 0);

    gRawLabel = lv_label_create(infoPanel);
    lv_label_set_text(gRawLabel, "RAW: ---, ---");
    lv_obj_set_width(gRawLabel, 174);
    lv_obj_set_style_text_color(gRawLabel, rgb(0xFDE68A), 0);
    lv_obj_set_style_text_align(gRawLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(gRawLabel, 0, 37);

    gMappedLabel = lv_label_create(infoPanel);
    lv_label_set_text(gMappedLabel, "DISPLAY: ---, ---");
    lv_obj_set_width(gMappedLabel, 174);
    lv_obj_set_style_text_color(gMappedLabel, rgb(0x93C5FD), 0);
    lv_obj_set_style_text_align(gMappedLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(gMappedLabel, 174, 37);

    lv_obj_t *configLabel = lv_label_create(gScreen);
    lv_label_set_text_fmt(configLabel, "CFG %ux%u | MAP %ux%u | LCD %ux%u",
                          gGt911Width, gGt911Height,
                          kTouchCoordinateWidth, kTouchCoordinateHeight,
                          kDisplayWidth, kDisplayHeight);
    lv_obj_set_width(configLabel, 360);
    lv_obj_set_style_text_color(configLabel, rgb(0xD1D5DB), 0);
    lv_obj_set_style_text_align(configLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(configLabel, 60, 405);

}

uint16_t mapTouchCoordinate(uint16_t value, uint16_t sourceSize,
                            uint16_t displaySize)
{
    if (sourceSize <= 1) {
        return 0;
    }
    if (value >= sourceSize) {
        value = sourceSize - 1;
    }
    return static_cast<uint16_t>(
               (static_cast<uint32_t>(value) * (displaySize - 1)) /
               (sourceSize - 1)
           );
}

void updateTouchUi(uint8_t count, uint16_t rawX, uint16_t rawY)
{
    static bool haveTouch = false;
    static uint16_t lastX = 0;
    static uint16_t lastY = 0;
    static uint32_t lastTextUpdate = 0;

    if (count == 0) {
        // Release frames are consumed and cleared below, but are not shown in the UI.
        haveTouch = false;
        return;
    }

    const bool changed = !haveTouch || rawX != lastX || rawY != lastY;
    if (!changed) {
        return;
    }

    const uint32_t now = millis();
    const bool updateText = !haveTouch ||
                            (now - lastTextUpdate) >= 50;
    haveTouch = true;
    lastX = rawX;
    lastY = rawY;

    if (!gRawLabel || !gMappedLabel || !lvgl_port_lock(10)) {
        return;
    }

    const uint16_t displayX = mapTouchCoordinate(rawX, kTouchCoordinateWidth, kDisplayWidth);
    const uint16_t displayY = mapTouchCoordinate(rawY, kTouchCoordinateHeight, kDisplayHeight);
    if (updateText) {
        lv_label_set_text_fmt(gRawLabel, "RAW: %u, %u", rawX, rawY);
        lv_label_set_text_fmt(gMappedLabel, "DISPLAY: %u, %u", displayX, displayY);
        lastTextUpdate = now;
    }
    lvgl_port_unlock();
}

bool clearGt911Status()
{
    const uint8_t value = 0;
    return write16BitRegister(gGt911Address, 0x814E, &value, 1);
}

void pollGt911()
{
    uint8_t frame[kGt911FrameSize] = {};

    // 一次读取状态和全部触点数据，减少 I2C 事务和 LVGL 等待时间。
    if (!gGt911Address ||
        !read16BitRegister(gGt911Address, 0x814E, frame, sizeof(frame))) {
        return;
    }

    const uint8_t status = frame[0];
    if (!(status & 0x80)) {
        return;
    }

    uint8_t count = status & 0x0F;
    if (count > kGt911MaxPoints) {
        count = kGt911MaxPoints;
    }

    if (count == 0) {
        updateTouchUi(0, 0, 0);
    } else {
        const uint8_t *point = frame + 1;
        const uint16_t x = point[1] | (static_cast<uint16_t>(point[2]) << 8);
        const uint16_t y = point[3] | (static_cast<uint16_t>(point[4]) << 8);
        updateTouchUi(count, x, y);
    }

    // 读取完成后清除 GT911 的数据就绪标志。
    clearGt911Status();
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("HXESP4848 GT911-compatible touch test starting");

    // 公共 I2C0：SDA=GPIO41，SCL=GPIO40。本测试统一使用 I2CBSP，不与 Wire 混用。
    if (!I2CBSP::begin()) {
        stopWithError("I2C initialization failed");
    }

    if (I2CBSP::probe(kTca9554Address) != ESP_OK) {
        stopWithError("TCA9554 0x20 is not responding");
    }
    if (!configureTca9554ForTouchAndBacklight()) {
        stopWithError("TCA9554 touch/backlight pin configuration failed");
    }
    if (!resetTouch()) {
        stopWithError("Touch reset failed");
    }

    if (!identifyGt911()) {
        stopWithError("GT911 not detected");
    }

    pinMode(kBacklightPwmPin, OUTPUT);
    digitalWrite(kBacklightPwmPin, HIGH);

    gBoard = new Board();
    if ((gBoard == nullptr) || !gBoard->init()) {
        stopWithError("Board initialization failed");
    }
    if (!gBoard->begin()) {
        stopWithError("LCD board begin failed");
    }

    gLcd = gBoard->getLCD();
    gBacklight = gBoard->getBacklight();
    if ((gLcd == nullptr) || (gBacklight == nullptr)) {
        stopWithError("LCD or backlight unavailable");
    }
    if (!lvgl_port_init(gLcd)) {
        stopWithError("LVGL initialization failed");
    }
    if (!lvgl_port_lock(-1)) {
        stopWithError("LVGL mutex lock failed");
    }
    createTouchUi();
    lvgl_port_unlock();

    // Board::begin() has initialized the expander. Reassert P2 as BL_EN and
    // enable both parts of the AP3032 backlight control path.
    if (!configureTca9554ForTouchAndBacklight() ||
        !setBacklightEnable(true)) {
        stopWithError("Backlight boost enable failed");
    }
    if (!gBacklight->on()) {
        stopWithError("Backlight enable failed");
    }
    digitalWrite(kBacklightPwmPin, LOW);
}

void loop()
{
    pollGt911();
    delay(10);
}
