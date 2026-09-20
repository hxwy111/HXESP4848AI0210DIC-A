#include <Arduino.h>
#include <esp_display_panel.hpp>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_bsp.h"
#include "lvgl_v8_port.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

namespace {

// TCA9554 通过公共 I2C0 连接，负责触摸复位和中断电平管理。
constexpr uint8_t kTca9554Address = 0x20;
// 交换网络后的硬件连接：GPIO45 直接连接 GT911 TP_INT。
constexpr uint8_t kTouchIntGpio = 45;
// GT911 的两个可选 7 位地址；当前硬件两个地址都能读到 GT911 ID。
constexpr uint8_t kGt911AddressA = 0x14;
constexpr uint8_t kGt911AddressB = 0x5D;
constexpr uint8_t kGt911MaxPoints = 5;
constexpr size_t kGt911FrameSize = 1 + kGt911MaxPoints * 8;
constexpr uint16_t kDisplayWidth = 480;
constexpr uint16_t kDisplayHeight = 480;
// 实测触摸帧坐标按 480x480 输出；配置区的 1024x600 可能是旧配置。
constexpr uint16_t kTouchCoordinateWidth = 480;
constexpr uint16_t kTouchCoordinateHeight = 480;
constexpr uint8_t kBacklightPwmPin = 38;

// 当前使用的 GT911 地址；两个地址同时应答时优先选择 0x5D。
uint8_t gGt911Address = kGt911AddressB;
uint16_t gGt911Width = 1024;
uint16_t gGt911Height = 600;

Board *gBoard = nullptr;
LCD *gLcd = nullptr;
Backlight *gBacklight = nullptr;
lv_obj_t *gScreen = nullptr;
lv_obj_t *gRawLabel = nullptr;
lv_obj_t *gMappedLabel = nullptr;
TaskHandle_t gTouchTask = nullptr;

// TCA9554 寄存器地址：输出和方向配置。
constexpr uint8_t kTca9554OutputRegister = 0x01;
constexpr uint8_t kTca9554ConfigRegister = 0x03;

// 交换网络后，TCA9554 P1 连接触摸芯片的低有效复位脚 RSTn。
constexpr uint8_t kTpResetBit = (1u << 1);
// TCA9554 P2 连接背光使能 BL_EN。
constexpr uint8_t kBacklightEnableBit = (1u << 2);

lv_color_t rgb(uint32_t value)
{
    // 将 24 位 RGB 颜色转换为 LVGL 颜色类型。
    return lv_color_hex(value);
}

[[noreturn]] void stopWithError(const char *message)
{
    // 初始化失败后停止程序，避免在硬件未准备好时继续访问外设。
    Serial.println(message);
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
    // P1(TP_RST) 和 P2(BL_EN) 都是由主控输出的控制信号。
    uint8_t direction = 0;
    if (!tca9554ReadRegister(kTca9554ConfigRegister, direction)) {
        Serial.println("TCA9554: read CONFIG register failed");
        return false;
    }

    // 采用“读-修改-写”，其他端口保持原来的方向。
    direction = static_cast<uint8_t>(direction & ~kTpResetBit);
    direction = static_cast<uint8_t>(direction & ~kBacklightEnableBit);

    if (!tca9554WriteRegister(kTca9554ConfigRegister, direction)) {
        Serial.println("TCA9554: write CONFIG register failed");
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
    // BL_EN 使用 TCA9554 P2 控制，只修改 P2，保留其他输出锁存值。
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
    if (actualEnabled != enabled) {
        return false;
    }
    return true;
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
    // 依次尝试两个 GT911 地址，并通过 0x8140 的 "911" 字符串确认芯片。
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
        // 0x8048/49 为 X 分辨率，0x804A/4B 为 Y 分辨率，均为低字节在前。
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
    // 创建用于验证坐标映射的圆屏测试界面，不修改 GT911 内部配置。
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

/*
 * 将一个坐标从源坐标系转换到目标坐标系。
 *
 * 例如：
 *   触摸坐标范围为 0~479，显示坐标范围也为 0~479 时，坐标不变；
 *   如果源坐标范围为 0~1023，目标坐标范围为 0~479，则执行等比例缩放。
 */
uint16_t mapTouchCoordinate(uint16_t value, uint16_t sourceSize,
                            uint16_t displaySize)
{
    // sourceSize 表示源坐标的总长度，而不是最大坐标值。
    // 例如最大坐标为 479 时，sourceSize 应为 480。
    if (sourceSize <= 1) {
        // 源范围无效时返回 0，同时避免后面的除零错误。
        return 0;
    }
    if (value >= sourceSize) {
        // 将异常坐标限制到源坐标允许的最大值。
        value = sourceSize - 1;
    }

    // 线性映射公式：
    // 目标坐标 = 源坐标 × 目标最大坐标 ÷ 源最大坐标。
    // 转成 uint32_t 后再相乘，避免 uint16_t 相乘时溢出。
    return static_cast<uint16_t>(
               (static_cast<uint32_t>(value) * (displaySize - 1)) /
               (sourceSize - 1)
           );
}

/*
 * 更新 LVGL 上的触摸状态和坐标文字。
 *
 * count 为当前触点数量；本测试界面只显示第一个触点的 ID 和坐标。
 * 函数不直接控制 GT911，只负责对已经读取到的触摸数据进行显示。
 */
void updateTouchUi(uint8_t count, uint16_t rawX, uint16_t rawY)
{
    // 以下变量在多次调用之间保持数值，用于过滤重复帧和降低刷新频率。
    static bool haveTouch = false;
    static uint16_t lastX = 0;
    static uint16_t lastY = 0;
    static uint32_t lastTextUpdate = 0;

    if (count == 0) {
        // GT911 报告没有触点：只有上一帧确实有触摸时才显示 RELEASE。
        if (!haveTouch) {
            return;
        }
        haveTouch = false;
        // LVGL 由独立任务运行，修改控件前必须先取得互斥锁。
        // 修改完成后立即释放锁。
        return;
    }

    // 触点数量、ID 和坐标都没有变化时，说明是重复帧，不需要刷新界面。
    const bool changed = !haveTouch ||
                         rawX != lastX || rawY != lastY;
    if (!changed) {
        return;
    }

    const uint32_t now = millis();
    // 坐标每次变化都可以更新内部记录，但文字最多每 50 ms 刷新一次。
    // 这样可以减少标签控件重绘，改善快速滑动时的流畅度。
    const bool updateText = !haveTouch ||
                            (now - lastTextUpdate) >= 50;

    // 保存本次数据，下一次调用时用于判断是否为重复帧。
    haveTouch = true;
    lastX = rawX;
    lastY = rawY;

    if (!gRawLabel || !gMappedLabel || !lvgl_port_lock(10)) {
        return;
    }

    // 将触摸芯片原始坐标转换为 LCD 的 480x480 坐标。
    const uint16_t displayX = mapTouchCoordinate(rawX, kTouchCoordinateWidth, kDisplayWidth);
    const uint16_t displayY = mapTouchCoordinate(rawY, kTouchCoordinateHeight, kDisplayHeight);
    if (updateText) {
        // 只在允许刷新的时机更新 3 个文字控件。
        lv_label_set_text_fmt(gRawLabel, "RAW: %u, %u", rawX, rawY);
        lv_label_set_text_fmt(gMappedLabel, "DISPLAY: %u, %u", displayX, displayY);
        lastTextUpdate = now;
    }

    // 释放 LVGL 互斥锁，允许 LVGL 任务继续刷新显示。
    lvgl_port_unlock();
}

// 清除 GT911 的“数据已准备好”标志，避免重复读取同一触摸帧。
bool clearGt911Status()
{
    // 向状态寄存器写 0，通知 GT911 主机已经读取完本帧数据。
    const uint8_t value = 0;
    return write16BitRegister(gGt911Address, 0x814E, &value, 1);
}

/*
 * 轮询 GT911 触摸数据：
 *   1. 从 0x814E 开始一次读取状态和触点数据；
 *   2. 判断数据是否有效；
 *   3. 解析第一个触点；
 *   4. 更新 LVGL；
 *   5. 向 0x814E 写 0 清除状态。
 */
void pollGt911()
{
    // 轮询模式：一次读取状态和触点数据，再更新界面并清除状态。
    uint8_t frame[kGt911FrameSize] = {};

    // 一次读取状态和全部触点数据，减少 I2C 事务和 LVGL 等待时间。
    if (!gGt911Address ||
        !read16BitRegister(gGt911Address, 0x814E, frame, sizeof(frame))) {
        return;
    }

    const uint8_t status = frame[0];
    if (!(status & 0x80)) {
        // 最高位为 0 表示 GT911 尚未准备好新的触摸数据。
        return;
    }

    // 状态寄存器低 4 位表示触点数量。
    uint8_t count = status & 0x0F;
    if (count > kGt911MaxPoints) {
        count = kGt911MaxPoints;
    }

    if (count == 0) {
        updateTouchUi(0, 0, 0);
    } else {
        // frame[0] 是状态字节，因此第一个触点从 frame[1] 开始。
        // 单个触点记录长度为 8 字节：ID、X、Y、预留/面积等字段。
        const uint8_t *point = frame + 1;
        // X/Y 坐标均为“小端格式”：低字节在前，高字节在后。
        const uint16_t x = point[1] | (static_cast<uint16_t>(point[2]) << 8);
        const uint16_t y = point[3] | (static_cast<uint16_t>(point[4]) << 8);
        updateTouchUi(count, x, y);
    }

    // 读取完成后清除 GT911 的数据就绪标志。
    clearGt911Status();
}

// GPIO45 中断服务函数只发送任务通知，不在中断上下文中执行 I2C 或 LVGL 操作。
void IRAM_ATTR onTouchInterrupt()
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    if (gTouchTask != nullptr) {
        vTaskNotifyGiveFromISR(gTouchTask, &higherPriorityTaskWoken);
    }
    if (higherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

// 在普通任务上下文中响应 GPIO45 中断并读取 GT911 数据。
void touchInterruptTask(void *)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        pollGt911();
    }
}

} // namespace

void setup()
{
    // 初始化顺序：I2C → LCD/IO Expander → TCA9554 控制脚 → GT911 → LVGL → 背光。
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
    pinMode(kBacklightPwmPin, OUTPUT);
    digitalWrite(kBacklightPwmPin, HIGH);

    gBoard = new Board();
    if ((gBoard == nullptr) || !gBoard->init()) {
        stopWithError("Board initialization failed");
    }
    if (!gBoard->begin()) {
        stopWithError("LCD board begin failed");
    }

    // Board::begin() 会把 TCA9554 复位为默认状态，因此必须在其之后重新配置
    // P1=TP_RST、P2=BL_EN，随后再复位并识别 GT911。
    if (!configureTca9554ForTouchAndBacklight()) {
        stopWithError("TCA9554 touch/backlight pin configuration failed");
    }
    if (!setBacklightEnable(false)) {
        stopWithError("Backlight disable failed");
    }
    if (!resetTouch()) {
        stopWithError("Touch reset failed");
    }
    if (!identifyGt911()) {
        stopWithError("GT911 not detected");
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

    // GPIO45 是 GT911 的开漏低有效 TP_INT，使用内部上拉并监听下降沿。
    pinMode(kTouchIntGpio, INPUT_PULLUP);
    if (xTaskCreate(
            touchInterruptTask,
            "gt911_interrupt",
            4096,
            nullptr,
            5,
            &gTouchTask
        ) != pdPASS) {
        stopWithError("Touch interrupt task creation failed");
    }
    attachInterrupt(
        digitalPinToInterrupt(kTouchIntGpio),
        onTouchInterrupt,
        FALLING
    );
    // 若 GT911 在绑定中断前已经拉低 TP_INT，补发一次任务通知，避免丢帧。
    if (digitalRead(kTouchIntGpio) == LOW) {
        xTaskNotifyGive(gTouchTask);
    }

    if (!setBacklightEnable(true)) {
        stopWithError("Backlight enable failed");
    }
    if (!gBacklight->on()) {
        stopWithError("Backlight enable failed");
    }
    // AP3032 is enabled by a low level on BL_PWM. Keep this explicit because
    // BL_EN is on the expander while BL_PWM is a native ESP32-S3 GPIO.
    digitalWrite(kBacklightPwmPin, LOW);
}

void loop()
{
    // 触摸数据由中断任务处理，主循环无需再轮询 GT911。
    delay(1000);
}
