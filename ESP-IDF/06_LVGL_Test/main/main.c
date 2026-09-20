#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "display_bsp.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "demos/lv_demos.h"

static const char *TAG = "main";

/**
 * @brief 创建第一阶段LVGL测试界面
 */
static void create_test_ui(void)
{
    lv_obj_t *screen = lv_scr_act();

    /* 设置屏幕背景色。 */
    lv_obj_set_style_bg_color(
        screen,
        lv_color_hex(0x202A44),
        LV_PART_MAIN
    );

    lv_obj_set_style_bg_opa(
        screen,
        LV_OPA_COVER,
        LV_PART_MAIN
    );

    /* 创建标题。 */
    lv_obj_t *title = lv_label_create(screen);

    /*
     * 默认Montserrat字体不包含中文字符，
     * 第一阶段先使用英文，避免显示方框。
     */
    lv_label_set_text(
        title,
        "ST7701S + LVGL"
    );

    lv_obj_set_style_text_color(
        title,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_set_style_text_font(
        title,
        &lv_font_montserrat_16,
        LV_PART_MAIN
    );

    lv_obj_align(
        title,
        LV_ALIGN_TOP_MID,
        0,
        70
    );

    /* 创建按钮。 */
    lv_obj_t *button = lv_btn_create(screen);

    lv_obj_set_size(button, 170, 60);

    lv_obj_align(
        button,
        LV_ALIGN_CENTER,
        0,
        10
    );

    lv_obj_set_style_bg_color(
        button,
        lv_color_hex(0x00A8E8),
        LV_PART_MAIN
    );

    lv_obj_set_style_radius(
        button,
        18,
        LV_PART_MAIN
    );

    /* 创建按钮文字。 */
    lv_obj_t *button_label = lv_label_create(button);

    lv_label_set_text(
        button_label,
        "Display OK"
    );

    lv_obj_set_style_text_color(
        button_label,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN
    );

    lv_obj_center(button_label);

    /* 创建底部说明。 */
    lv_obj_t *status = lv_label_create(screen);

    lv_label_set_text(
        status,
        "480 x 480  RGB565  SPI init + RGB"
    );

    lv_obj_set_style_text_color(
        status,
        lv_color_hex(0xBFC9D9),
        LV_PART_MAIN
    );

    lv_obj_align(
        status,
        LV_ALIGN_BOTTOM_MID,
        0,
        -70
    );
}

void app_main(void)
{
    ESP_LOGI(TAG, "初始化ST7701S RGB565显示屏");

    ESP_ERROR_CHECK(
        display_bsp_stage2_init()
    );

    ESP_LOGI(TAG, "初始化LVGL显示端口");

    ESP_ERROR_CHECK(
        lvgl_port_init()
    );

    /*
     * LVGL任务已经启动。
     * app_main调用LVGL API前必须获取互斥锁。
     */
    if (lvgl_port_lock(UINT32_MAX)) {
       // create_test_ui();
        lv_demo_widgets();
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "获取LVGL互斥锁失败");
    }

    ESP_LOGI(TAG, "LVGL测试界面创建完成");

    /*
     * 后续界面刷新由LVGL任务负责。
     * app_main可以结束，也可以继续执行其他业务。
     */
}
