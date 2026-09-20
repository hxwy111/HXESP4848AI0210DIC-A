#include "ui_app.h"
#include "lvgl_v8_port.h"
#include <stdint.h>
#include <Arduino.h>

/* 当前显示的启动信息页 */
static lv_obj_t *g_info_screen = nullptr;

/* 当前显示的主界面占位页 */
static lv_obj_t *g_main_screen = nullptr;

/* 颜色测试页面的界面内容容器 */
static lv_obj_t *g_color_content=nullptr;

/* 纯色显示状态下使用的恢复按钮 */
static lv_obj_t *g_restore_button = nullptr;

/* 六个颜色按钮对象 */
static lv_obj_t *g_color_buttons[6] = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr
};

/* 当前选中的颜色，-1表示尚未选择 */
static int8_t g_selected_color = -1;

/* 触摸测试占位页面 */
static lv_obj_t *g_touch_screen = nullptr;

/* 第3页功能状态占位页面 */
static lv_obj_t *g_function_screen = nullptr;

/* 第2页动态控件 */
static lv_obj_t *g_touch_count_label = nullptr;
static lv_obj_t *g_slider_value_label = nullptr;
static lv_obj_t *g_touch_state_label = nullptr;
static lv_obj_t *g_touch_coordinate_label = nullptr;
static lv_obj_t *g_touch_pressure_label = nullptr;
static lv_timer_t *g_touch_status_timer = nullptr;
static uint32_t g_touch_event_count = 0;

/* 页面滑动切换去抖：防止一次手势在动画切页后被下一页再次识别。 */
static uint32_t g_last_page_gesture_ms = 0;

static bool accept_page_gesture()
{
    const uint32_t now = millis();
    if ((now - g_last_page_gesture_ms) < 550) {
        return false;
    }
    g_last_page_gesture_ms = now;
    return true;
}

/**
 * @brief 设置屏幕的通用背景
 *
 * @param screen 需要设置背景的LVGL屏幕对象
 */
static void set_screen_background(lv_obj_t *screen)
{
    /* 使用深蓝黑色作为主背景 */
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x08111F), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    /* 屏幕本身不参与滚动 */
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}


/**
 * @brief 创建信息卡片中的参数文字
 *
 * @param parent 信息卡片父对象
 */
static void create_device_information(lv_obj_t *parent)
{
    /* 左侧参数名称 */
    lv_obj_t *name_label = lv_label_create(parent);
    lv_label_set_text(
        name_label,
        "Resolution\n"
        "LCD IC\n"
        "Touch IC\n"
        "MCU"
    );

    lv_obj_set_style_text_color(
        name_label,
        lv_color_hex(0x8EA2BC),
        0
    );
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_16, 0);

    /* 增加每一行之间的距离 */
    lv_obj_set_style_text_line_space(name_label, 10, 0);

    lv_obj_align(
        name_label,
        LV_ALIGN_LEFT_MID,
        18,
        0
    );

    /* 右侧参数内容 */
    lv_obj_t *value_label = lv_label_create(parent);
    lv_label_set_text(
        value_label,
        "480 x 480\n"
        "ST7701S\n"
        "GT911\n"
        "ESP32-S3"
    );

    lv_obj_set_style_text_color(
        value_label,
        lv_color_white(),
        0
    );
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_16, 0);

    lv_obj_set_style_text_align(
        value_label,
        LV_TEXT_ALIGN_RIGHT,
        0
    );

    lv_obj_set_style_text_line_space(value_label, 10, 0);

    lv_obj_align(
        value_label,
        LV_ALIGN_RIGHT_MID,
        -18,
        0
    );
}


/**
 * @brief 返回启动信息页按钮事件
 *
 * @param event LVGL事件对象
 */



/**
 * @brief 创建主界面占位页
 *
 * 第二阶段将在这个页面中加入红、绿、蓝、黑、白颜色按钮。
 */
static void create_main_placeholder_screen()
{
   
}
/**
 * @brief 根据编号取得测试颜色
 *
 * @param index 颜色编号
 * @return 对应的LVGL颜色
 */
static lv_color_t get_test_color(uint8_t index)
{
    switch (index) {
        case 0:
            return lv_color_hex(0xFF0000);  // 红色

        case 1:
            return lv_color_hex(0x00FF00);  // 绿色

        case 2:
            return lv_color_hex(0x0000FF);  // 蓝色

        case 3:
            return lv_color_hex(0x000000);  // 黑色

        case 4:
            return lv_color_hex(0xFFFFFF);  // 白色

        case 5:
            return lv_color_hex(0x808080);  // 灰色

        default:
            return lv_color_hex(0x08111F);
    }
}


/**
 * @brief 创建底部分页指示器
 *
 * @param parent     分页指示器的父对象
 * @param activePage 当前页面编号，范围为0～2
 */
static void create_page_indicator(
    lv_obj_t *parent,
    uint8_t activePage
)
{
    for (uint8_t i = 0; i < 3; ++i) {
        lv_obj_t *dot = lv_obj_create(parent);

        /* 当前页的圆点稍大 */
        if (i == activePage) {
            lv_obj_set_size(dot, 12, 12);
        } else {
            lv_obj_set_size(dot, 8, 8);
        }

        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);

        if (i == activePage) {
            lv_obj_set_style_bg_color(
                dot,
                lv_color_hex(0x31C5F4),
                0
            );
        } else {
            lv_obj_set_style_bg_color(
                dot,
                lv_color_hex(0x536477),
                0
            );
        }

        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

        /*
         * 三个圆点分别位于中心左侧、中心和中心右侧。
         * 位置避开圆屏底部不可见区域。
         */
        lv_obj_align(
            dot,
            LV_ALIGN_BOTTOM_MID,
            (static_cast<int32_t>(i) - 1) * 22,
            -22
        );
    }
}


/**
 * @brief 更新五个颜色按钮的选中边框
 *
 * 被选中的按钮显示白色粗边框，其他按钮显示普通细边框。
 */
static void update_selected_color_border()
{
    for (uint8_t i = 0; i < 6; ++i) {
        if (g_color_buttons[i] == nullptr) {
            continue;
        }

        if (i == g_selected_color) {
            lv_obj_set_style_border_width(
                g_color_buttons[i],
                4,
                0
            );

            lv_obj_set_style_border_color(
                g_color_buttons[i],
                lv_color_hex(0x31C5F4),
                0
            );
        } else {
            lv_obj_set_style_border_width(
                g_color_buttons[i],
                1,
                0
            );

            lv_obj_set_style_border_color(
                g_color_buttons[i],
                lv_color_hex(0x6B7D91),
                0
            );
        }
    }
}


/**
 * @brief 设置恢复按钮的对比色
 *
 * @param colorIndex 当前纯色背景编号
 */
static void update_restore_button_color(uint8_t colorIndex)
{
    if (g_restore_button == nullptr) {
        return;
    }

    lv_obj_t *label = lv_obj_get_child(
        g_restore_button,
        0
    );

    /*
     * 绿色和白色背景较亮，恢复按钮使用黑色。
     * 红色、蓝色和黑色背景较暗，恢复按钮使用白色。
     */
    bool isLightBackground =
        (colorIndex == 1) ||
        (colorIndex == 4) ||
        (colorIndex == 5);

    if (isLightBackground) {
        lv_obj_set_style_bg_color(
            g_restore_button,
            lv_color_black(),
            0
        );

        if (label != nullptr) {
            lv_obj_set_style_text_color(
                label,
                lv_color_white(),
                0
            );
        }
    } else {
        lv_obj_set_style_bg_color(
            g_restore_button,
            lv_color_white(),
            0
        );

        if (label != nullptr) {
            lv_obj_set_style_text_color(
                label,
                lv_color_black(),
                0
            );
        }
    }
}


/**
 * @brief 恢复颜色测试操作界面
 *
 * @param event LVGL事件对象
 */
static void restore_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    Serial.println("UI: restore color control panel");

    /* 恢复页面默认深色背景 */
    lv_obj_set_style_bg_color(
        g_main_screen,
        lv_color_hex(0x08111F),
        0
    );

    /* 重新显示颜色按钮区域 */
    if (g_color_content != nullptr) {
        lv_obj_clear_flag(
            g_color_content,
            LV_OBJ_FLAG_HIDDEN
        );
    }

    /* 隐藏恢复按钮 */
    if (g_restore_button != nullptr) {
        lv_obj_add_flag(
            g_restore_button,
            LV_OBJ_FLAG_HIDDEN
        );
    }
}


/**
 * @brief 颜色按钮长按
 * @param event LVGL事件对象
 */
static void color_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_LONG_PRESSED) {
        return;
    }

    /*
     * 从按钮的用户数据中取得颜色编号。
     * 编号范围：
     * 0=红、1=绿、2=蓝、3=黑、4=白
     */
    uint8_t colorIndex = static_cast<uint8_t>(
        reinterpret_cast<intptr_t>(
            lv_event_get_user_data(event)
        )
    );

    g_selected_color = colorIndex;

    /* 更新按钮选中边框 */
    update_selected_color_border();

    /* 将整个页面背景切换为选中的纯色 */
    lv_obj_set_style_bg_color(
        g_main_screen,
        get_test_color(colorIndex),
        0
    );

    /*
     * 隐藏标题、颜色按钮和分页指示器，
     * 使屏幕主体进入纯色显示状态。
     */
    if (g_color_content != nullptr) {
        lv_obj_add_flag(
            g_color_content,
            LV_OBJ_FLAG_HIDDEN
        );
    }

    /* 根据背景颜色设置恢复按钮的对比色 */
    update_restore_button_color(colorIndex);

    /* 显示恢复按钮 */
    if (g_restore_button != nullptr) {
        lv_obj_clear_flag(
            g_restore_button,
            LV_OBJ_FLAG_HIDDEN
        );
    }

    Serial.printf(
        "UI: color long pressed, index=%u\n",
        static_cast<unsigned int>(colorIndex)
    );
}


/**
 * @brief 创建单个颜色按钮
 *
 * @param parent     按钮父对象
 * @param colorIndex 颜色编号
 * @param text       按钮文字
 * @param x          按钮X坐标
 * @param y          按钮Y坐标
 */
static void create_color_button(
    lv_obj_t *parent,
    uint8_t colorIndex,
    const char *text,
    lv_coord_t x,
    lv_coord_t y
)
{
    lv_obj_t *button = lv_btn_create(parent);
    g_color_buttons[colorIndex] = button;

    lv_obj_set_size(button, 90, 72);
    lv_obj_set_pos(button, x, y);

    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_bg_color(
        button,
        get_test_color(colorIndex),
        0
    );

    lv_obj_set_style_bg_opa(
        button,
        LV_OPA_COVER,
        0
    );

    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(
        button,
        lv_color_hex(0x6B7D91),
        0
    );

    /* 按下时轻微缩小，提供明显触摸反馈 */
    lv_obj_set_style_transform_zoom(
        button,
        240,
        LV_STATE_PRESSED
    );

    /* 允许手势继续向父页面传递 */
    lv_obj_clear_flag(
        button,
        LV_OBJ_FLAG_GESTURE_BUBBLE
    );

    lv_obj_add_event_cb(
        button,
        color_button_event,
        LV_EVENT_LONG_PRESSED,
        reinterpret_cast<void *>(
            static_cast<intptr_t>(colorIndex)
        )
    );

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);

    lv_obj_set_style_text_font(
        label,
        &lv_font_montserrat_16,
        0
    );

    /*
     * 绿色和白色按钮使用黑字，
     * 其余按钮使用白字。
     */
    if ((colorIndex == 1) || (colorIndex == 4) || (colorIndex == 5)) {
        lv_obj_set_style_text_color(
            label,
            lv_color_black(),
            0
        );
    } else {
        lv_obj_set_style_text_color(
            label,
            lv_color_white(),
            0
        );
    }

    lv_obj_center(label);
}

/**
 * @brief 关闭功能演示提示框
 *
 * @param event LVGL事件对象
 */
static void close_feature_message_event(
    lv_event_t *event
)
{
    if (
        lv_event_get_code(event) !=
        LV_EVENT_VALUE_CHANGED
    ) {
        return;
    }

    lv_obj_t *messageBox =
        lv_event_get_current_target(event);

    const char *activeButton =
        lv_msgbox_get_active_btn_text(messageBox);

    if (
        (activeButton != nullptr) &&
        (strcmp(activeButton, "OK") == 0)
    ) {
        /*
         * 使用异步删除，避免在LVGL事件回调执行期间
         * 直接删除当前正在处理事件的对象。
         */
        lv_obj_del_async(messageBox);
    }
}


/**
 * @brief 功能卡片演示按钮事件
 *
 * 这里只显示提示框，不会初始化任何实际硬件。
 *
 * @param event LVGL事件对象
 */
static void feature_demo_button_event(
    lv_event_t *event
)
{
    if (
        lv_event_get_code(event) !=
        LV_EVENT_SHORT_CLICKED
    ) {
        return;
    }

    const char *featureName =
        static_cast<const char *>(
            lv_event_get_user_data(event)
        );

    if (featureName == nullptr) {
        return;
    }

    char message[96];

    lv_snprintf(
        message,
        sizeof(message),
        "%s is for UI demonstration only.\n"
        "No hardware function is started.",
        featureName
    );

    /* 消息框按钮列表必须以空字符串结束 */
    static const char *buttons[] = {
        "OK",
        ""
    };

    lv_obj_t *messageBox = lv_msgbox_create(
        g_function_screen,
        "UI DEMO",
        message,
        buttons,
        false
    );

/*
 * 将弹窗标题、正文和确认按钮文字统一设置为白色。
 * Wi-Fi、SD Card和BLE共用该弹窗，因此三项都会生效。
 */
lv_obj_set_style_text_color(
    lv_msgbox_get_title(messageBox),
    lv_color_white(),
    0
);
lv_obj_set_style_text_font(
    lv_msgbox_get_title(messageBox),
    &lv_font_montserrat_30,
    0
);
lv_obj_set_width(lv_msgbox_get_title(messageBox), 300);
lv_obj_set_style_text_align(lv_msgbox_get_title(messageBox), LV_TEXT_ALIGN_CENTER, 0);

lv_obj_set_style_text_color(
    lv_msgbox_get_text(messageBox),
    lv_color_white(),
    0
);
lv_obj_set_style_text_font(
    lv_msgbox_get_text(messageBox),
    &lv_font_montserrat_20,
    0
);
lv_obj_set_width(lv_msgbox_get_text(messageBox), 310);
lv_obj_set_style_text_align(lv_msgbox_get_text(messageBox), LV_TEXT_ALIGN_CENTER, 0);

lv_obj_set_style_text_color(
    lv_msgbox_get_btns(messageBox),
    lv_color_white(),
    LV_PART_ITEMS
);
lv_obj_set_style_text_font(
    lv_msgbox_get_btns(messageBox),
    &lv_font_montserrat_20,
    LV_PART_ITEMS
);

    /* 根据圆形屏幕限制消息框尺寸 */
    lv_obj_set_size(messageBox, 340, 205);
    lv_obj_center(messageBox);

    lv_obj_set_style_bg_color(
        messageBox,
        lv_color_hex(0x152538),
        0
    );

    lv_obj_set_style_border_width(
        messageBox,
        1,
        0
    );

    lv_obj_set_style_border_color(
        messageBox,
        lv_color_hex(0x31C5F4),
        0
    );

    lv_obj_set_style_radius(
        messageBox,
        16,
        0
    );

    lv_obj_add_event_cb(
        messageBox,
        close_feature_message_event,
        LV_EVENT_VALUE_CHANGED,
        nullptr
    );

    Serial.printf(
        "UI demo selected: %s\n",
        featureName
    );
}


/**
 * @brief 创建一张功能展示卡片
 *
 * @param parent      卡片父对象
 * @param name        功能名称
 * @param status      显示状态
 * @param y           卡片纵向位置
 * @param accentColor 功能强调色
 */
static void create_feature_card(
    lv_obj_t *parent,
    const char *name,
    const char *status,
    lv_coord_t y,
    lv_color_t accentColor
)
{
    /* 创建功能卡片 */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 360, 80);
    lv_obj_set_pos(card, 60, y);

    lv_obj_set_style_radius(card, 12, 0);

    lv_obj_set_style_bg_color(
        card,
        lv_color_hex(0x122235),
        0
    );

    lv_obj_set_style_bg_opa(
        card,
        LV_OPA_COVER,
        0
    );

    lv_obj_set_style_border_width(card, 1, 0);

    lv_obj_set_style_border_color(
        card,
        lv_color_hex(0x294057),
        0
    );

    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * 卡片空白区域允许把滑动手势传递给页面，
     * 因此可以从卡片空白位置向右滑动返回。
     */
    lv_obj_add_flag(
        card,
        LV_OBJ_FLAG_GESTURE_BUBBLE
    );

    /* 左侧功能颜色标记 */
    lv_obj_t *accent = lv_obj_create(card);
    lv_obj_set_size(accent, 5, 38);

    lv_obj_align(
        accent,
        LV_ALIGN_LEFT_MID,
        9,
        0
    );

    lv_obj_set_style_radius(
        accent,
        LV_RADIUS_CIRCLE,
        0
    );

    lv_obj_set_style_bg_color(
        accent,
        accentColor,
        0
    );

    lv_obj_set_style_bg_opa(
        accent,
        LV_OPA_COVER,
        0
    );

    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

    /* 功能名称 */
    lv_obj_t *nameLabel = lv_label_create(card);
    lv_label_set_text(nameLabel, name);

    lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_20, 0);

    lv_obj_set_style_text_color(
        nameLabel,
        lv_color_white(),
        0
    );

    lv_obj_set_pos(nameLabel, 32, 8);

    /* 功能状态 */
    lv_obj_t *statusLabel = lv_label_create(card);
    lv_label_set_text(statusLabel, status);

    lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_16, 0);

    lv_obj_set_style_text_color(
        statusLabel,
        lv_color_hex(0x8FA4BA),
        0
    );

    lv_obj_set_pos(statusLabel, 32, 47);

    /* UI演示按钮 */
    lv_obj_t *demoButton = lv_btn_create(card);
    lv_obj_set_size(demoButton, 82, 44);

    lv_obj_align(
        demoButton,
        LV_ALIGN_RIGHT_MID,
        -12,
        0
    );

    lv_obj_set_style_radius(
        demoButton,
        12,
        0
    );

    lv_obj_set_style_bg_color(
        demoButton,
        lv_color_hex(0x263B50),
        0
    );

    lv_obj_set_style_bg_color(
        demoButton,
        accentColor,
        LV_STATE_PRESSED
    );

    /*
     * 从按钮开始的触摸不传递给页面，
     * 避免点击按钮时误触发页面切换。
     */
    lv_obj_clear_flag(
        demoButton,
        LV_OBJ_FLAG_GESTURE_BUBBLE
    );

    lv_obj_add_event_cb(
        demoButton,
        feature_demo_button_event,
        LV_EVENT_SHORT_CLICKED,
        const_cast<char *>(name)
    );

    lv_obj_t *buttonLabel =
        lv_label_create(demoButton);

    lv_label_set_text(buttonLabel, "DEMO");

    lv_obj_set_style_text_font(
        buttonLabel,
        &lv_font_montserrat_16,
        0
    );

    lv_obj_set_style_text_color(
        buttonLabel,
        lv_color_white(),
        0
    );

    lv_obj_center(buttonLabel);
}

/* 提前声明触摸测试占位页创建函数 */
static void create_touch_placeholder_screen();
static void create_function_placeholder_screen();

/* 更新有效短点击次数 */
static void update_touch_count_label()
{
    if (g_touch_count_label != nullptr) {
        lv_label_set_text_fmt(
            g_touch_count_label,
            "Event count: %lu",
            static_cast<unsigned long>(g_touch_event_count)
        );
    }
}

/* 只有有效短点击才计数，长按或滑出后松开均不计数 */
static void touch_count_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED) {
        return;
    }

    ++g_touch_event_count;
    update_touch_count_label();
}

/* 清零点击次数 */
static void clear_touch_count_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED) {
        return;
    }

    g_touch_event_count = 0;
    update_touch_count_label();
}

/* 滑动条数值变化时实时更新文字 */
static void touch_slider_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    lv_obj_t *slider = lv_event_get_target(event);
    lv_label_set_text_fmt(
        g_slider_value_label,
        "Slider value: %ld",
        static_cast<long>(lv_slider_get_value(slider))
    );
}

/* 从LVGL端口缓存更新坐标、压力和按压状态 */
static void touch_status_timer_event(lv_timer_t *timer)
{
    (void)timer;

    LVGLPortTouchState state;
    if (!lvgl_port_get_touch_state(&state)) {
        return;
    }

    lv_label_set_text(
        g_touch_state_label,
            state.pressed ? "Touch: DETECTED" : "Touch: NOT DETECTED"
    );
    lv_obj_set_style_text_color(
        g_touch_state_label,
        state.pressed ? lv_color_hex(0x42E695) : lv_color_hex(0x9AAABD),
        0
    );
    lv_label_set_text_fmt(
        g_touch_coordinate_label,
        "X: %03u   Y: %03u",
        state.x,
        state.y
    );
    lv_label_set_text_fmt(
        g_touch_pressure_label,
            "Touch points: %u",
            state.points
    );
}

static void start_touch_status_timer()
{
    if (g_touch_status_timer == nullptr) {
        g_touch_status_timer = lv_timer_create(
            touch_status_timer_event,
            50,
            nullptr
        );
        touch_status_timer_event(nullptr);
    }
}

static void stop_touch_status_timer()
{
    if (g_touch_status_timer != nullptr) {
        lv_timer_del(g_touch_status_timer);
        g_touch_status_timer = nullptr;
    }
}

/* 第3页向右滑动时返回第2页 */
static void function_page_gesture_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
        return;
    }

    lv_indev_t *inputDevice = lv_indev_get_act();
    if ((inputDevice == nullptr) ||
        (lv_indev_get_gesture_dir(inputDevice) != LV_DIR_RIGHT)) {
        return;
    }

    if (!accept_page_gesture()) {
        Serial.println("UI: duplicate page gesture ignored");
        return;
    }

    lv_scr_load_anim(
        g_touch_screen,
        LV_SCR_LOAD_ANIM_MOVE_RIGHT,
        120,
        0,
        true
    );
    g_function_screen = nullptr;
    start_touch_status_timer();
}


    /**
 * @brief 创建主界面第3页：无线与存储功能
 *
 * 本页面只进行UI展示，不初始化Wi-Fi、SD卡和BLE。
 */
static void create_function_placeholder_screen()
{
    g_function_screen = lv_obj_create(nullptr);
    set_screen_background(g_function_screen);

    /* 允许屏幕空白区域接收滑动手势 */
    lv_obj_add_flag(
        g_function_screen,
        LV_OBJ_FLAG_CLICKABLE
    );

    lv_obj_add_event_cb(
        g_function_screen,
        function_page_gesture_event,
        LV_EVENT_GESTURE,
        nullptr
    );

    /* 页面标题 */
    lv_obj_t *title =
        lv_label_create(g_function_screen);

    lv_label_set_text(
        title,
        "FUNCTION TEST"
    );

    lv_obj_set_style_text_font(
        title,
        &lv_font_montserrat_30,
        0
    );

    lv_obj_set_style_text_color(
        title,
        lv_color_white(),
        0
    );

    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    /* Wi-Fi功能展示卡片 */
    create_feature_card(
        g_function_screen,
        "Wi-Fi",
        "UI only",
        90,
        lv_color_hex(0x31C5F4)
    );

    /* SD卡功能展示卡片 */
    create_feature_card(
        g_function_screen,
        "SD Card",
        "Not connected",
        180,
        lv_color_hex(0x42D392)
    );

    /* BLE功能展示卡片 */
    create_feature_card(
        g_function_screen,
        "BLE",
        "UI only",
        270,
        lv_color_hex(0xF2B84B)
    );

    /* 页面返回提示 */
    lv_obj_t *hint =
        lv_label_create(g_function_screen);

    lv_label_set_text(
        hint,
        "Swipe right to return"
    );

    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);

    lv_obj_set_style_text_color(
        hint,
        lv_color_hex(0x71869D),
        0
    );

    lv_obj_align(
        hint,
        LV_ALIGN_TOP_MID,
        0,
        380
    );

    /* 第3个分页圆点高亮 */
    create_page_indicator(
        g_function_screen,
        2
    );
}



/**
 * @brief 触摸测试占位页的滑动事件
 *
 * 向右滑动时返回颜色测试页。
 *
 * @param event LVGL事件对象
 */
static void touch_page_gesture_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
        return;
    }

    lv_indev_t *inputDevice = lv_indev_get_act();

    if (inputDevice == nullptr) {
        return;
    }

    lv_dir_t direction =
        lv_indev_get_gesture_dir(inputDevice);

    if (direction == LV_DIR_RIGHT) {
        if (!accept_page_gesture()) {
            Serial.println("UI: duplicate page gesture ignored");
            return;
        }
        Serial.println("UI: swipe right to color page");
        stop_touch_status_timer();
        /* Keep the transition short to limit full-screen RGB565 redraw cost. */
        lv_scr_load_anim(
            g_main_screen,
            LV_SCR_LOAD_ANIM_MOVE_RIGHT,
            120,
            0,
            true
        );

        g_touch_screen = nullptr;
        g_touch_count_label = nullptr;
        g_slider_value_label = nullptr;
        g_touch_state_label = nullptr;
        g_touch_coordinate_label = nullptr;
        g_touch_pressure_label = nullptr;
        return;
    }

    if ((direction == LV_DIR_LEFT) && (g_function_screen == nullptr)) {
        if (!accept_page_gesture()) {
            Serial.println("UI: duplicate page gesture ignored");
            return;
        }
        Serial.println("UI: swipe left to function page");
        stop_touch_status_timer();
        create_function_placeholder_screen();
        lv_scr_load_anim(
            g_function_screen,
            LV_SCR_LOAD_ANIM_MOVE_LEFT,
            120,
            0,
            false
        );
    }
}


/**
 * @brief 创建触摸功能占位页面
 *
 * 当前只用于验证颜色页向左滑动。
 * 下一阶段再加入点击计数和滑动滚轮。
 */
static void create_touch_placeholder_screen()
{
    g_touch_screen = lv_obj_create(nullptr);
    set_screen_background(g_touch_screen);

    lv_obj_add_flag(g_touch_screen, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(
        g_touch_screen,
        touch_page_gesture_event,
        LV_EVENT_GESTURE,
        nullptr
    );

    g_touch_event_count = 0;

    /* 页面标题 */
    lv_obj_t *title = lv_label_create(g_touch_screen);
    lv_label_set_text(title, "TOUCH TEST");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);

    /* 点击次数和清零按钮 */
    g_touch_count_label = lv_label_create(g_touch_screen);
    lv_obj_set_style_text_font(g_touch_count_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_touch_count_label, lv_color_hex(0xE4EDF7), 0);
    lv_obj_set_width(g_touch_count_label, 220);
    lv_obj_set_style_text_align(g_touch_count_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_touch_count_label, 35, 104);
    update_touch_count_label();

    lv_obj_t *clearButton = lv_btn_create(g_touch_screen);
    lv_obj_set_size(clearButton, 70, 38);
    // 约按 9 px/mm 换算：向上 2 mm、向左 1 mm。
    lv_obj_set_pos(clearButton, 276, 90);
    lv_obj_set_style_radius(clearButton, 12, 0);
    lv_obj_set_style_bg_color(clearButton, lv_color_hex(0x35485E), 0);
    lv_obj_clear_flag(clearButton, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(
        clearButton,
        clear_touch_count_event,
        LV_EVENT_SHORT_CLICKED,
        nullptr
    );
    lv_obj_t *clearLabel = lv_label_create(clearButton);
    lv_label_set_text(clearLabel, "CLEAR");
    lv_obj_center(clearLabel);

    /* 大按钮仅响应有效短点击 */
    lv_obj_t *countButton = lv_btn_create(g_touch_screen);
    lv_obj_set_size(countButton, 180, 52);
    lv_obj_align(countButton, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_style_radius(countButton, 16, 0);
    lv_obj_set_style_bg_color(countButton, lv_color_hex(0x087EA4), 0);
    lv_obj_clear_flag(countButton, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(
        countButton,
        touch_count_button_event,
        LV_EVENT_SHORT_CLICKED,
        nullptr
    );
    lv_obj_t *countButtonLabel = lv_label_create(countButton);
    lv_label_set_text(countButtonLabel, "ADD EVENT");
    lv_obj_set_style_text_font(countButtonLabel, &lv_font_montserrat_16, 0);
    lv_obj_center(countButtonLabel);

    /* 滑动条及实时数值 */
    g_slider_value_label = lv_label_create(g_touch_screen);
    lv_label_set_text(g_slider_value_label, "Slider value: 50");
    lv_obj_set_style_text_font(g_slider_value_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_slider_value_label, lv_color_hex(0xE4EDF7), 0);
    lv_obj_align(g_slider_value_label, LV_ALIGN_TOP_MID, 0, 198);

    lv_obj_t *slider = lv_slider_create(g_touch_screen);
    lv_obj_set_size(slider, 260, 20);
    lv_obj_set_pos(slider, 110, 232);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x27384C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x31C5F4), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);

    /* 拖动滑动条时禁止手势冒泡，避免误翻页 */
    lv_obj_clear_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(
        slider,
        touch_slider_event,
        LV_EVENT_VALUE_CHANGED,
        nullptr
    );

    /* 实时触摸状态 */
    g_touch_state_label = lv_label_create(g_touch_screen);
    lv_label_set_text(g_touch_state_label, "Touch: NOT DETECTED");
    lv_obj_set_style_text_font(g_touch_state_label, &lv_font_montserrat_16, 0);
    lv_obj_align(g_touch_state_label, LV_ALIGN_TOP_MID, 0, 270);

    g_touch_coordinate_label = lv_label_create(g_touch_screen);
    lv_label_set_text(g_touch_coordinate_label, "X: 000   Y: 000");
    lv_obj_set_style_text_font(g_touch_coordinate_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_touch_coordinate_label, lv_color_hex(0xC8D5E3), 0);
    lv_obj_align(g_touch_coordinate_label, LV_ALIGN_TOP_MID, 0, 300);

    g_touch_pressure_label = lv_label_create(g_touch_screen);
    lv_label_set_text(g_touch_pressure_label, "Touch points: 0");
    lv_obj_set_style_text_font(g_touch_pressure_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_touch_pressure_label, lv_color_hex(0xC8D5E3), 0);
    lv_obj_align(g_touch_pressure_label, LV_ALIGN_TOP_MID, 0, 330);

    lv_obj_t *hint = lv_label_create(g_touch_screen);
    lv_label_set_text(hint, "Swipe on empty area");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x71869D), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 362);

    create_page_indicator(g_touch_screen, 1);
    start_touch_status_timer();
}


/**
 * @brief 颜色测试页的滑动事件
 *
 * 向左滑动时进入触摸测试占位页。
 *
 * @param event LVGL事件对象
 */
static void color_page_gesture_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
        return;
    }

    lv_indev_t *inputDevice = lv_indev_get_act();

    if (inputDevice == nullptr) {
        return;
    }

    lv_dir_t direction =
        lv_indev_get_gesture_dir(inputDevice);

    if (direction != LV_DIR_LEFT) {
        return;
    }

    if (!accept_page_gesture()) {
        Serial.println("UI: duplicate page gesture ignored");
        return;
    }

    Serial.println("UI: swipe left to touch page");

    /* 防止同一次手势重复创建页面 */
    if (g_touch_screen != nullptr) {
        return;
    }

    create_touch_placeholder_screen();

    /*
     * 切换到触摸测试占位页。
     * 此处不能删除颜色页，因为向右滑动时还需要返回。
     */
    lv_scr_load_anim(
        g_touch_screen,
        LV_SCR_LOAD_ANIM_MOVE_LEFT,
        120,
        0,
        false
    );
}


/**
 * @brief 创建主界面第1页：屏幕颜色测试
 */
static void create_color_test_screen()
{
    g_main_screen = lv_obj_create(nullptr);
    set_screen_background(g_main_screen);

    /* 确保整个页面能够接收滑动手势 */
    lv_obj_add_flag(
        g_main_screen,
        LV_OBJ_FLAG_CLICKABLE
    );

    lv_obj_add_event_cb(
        g_main_screen,
        color_page_gesture_event,
        LV_EVENT_GESTURE,
        nullptr
    );

    /*
     * 所有正常界面控件都放入同一个容器。
     * 进入纯色模式时，只要隐藏这个容器即可。
     */
    g_color_content = lv_obj_create(g_main_screen);
    lv_obj_set_size(g_color_content, 400, 400);
    lv_obj_set_pos(g_color_content, 40, 40);

    lv_obj_set_style_bg_opa(
        g_color_content,
        LV_OPA_TRANSP,
        0
    );

    lv_obj_set_style_border_width(
        g_color_content,
        0,
        0
    );

    lv_obj_set_style_pad_all(
        g_color_content,
        0,
        0
    );

    lv_obj_clear_flag(
        g_color_content,
        LV_OBJ_FLAG_SCROLLABLE
    );

    lv_obj_add_flag(
        g_color_content,
        LV_OBJ_FLAG_GESTURE_BUBBLE
    );

    /* 页面标题 */
    lv_obj_t *title = lv_label_create(g_color_content);
    lv_label_set_text(title, "COLOR TEST");

    lv_obj_set_style_text_font(
        title,
        &lv_font_montserrat_30,
        0
    );

    lv_obj_set_style_text_color(
        title,
        lv_color_white(),
        0
    );

    lv_obj_align(
        title,
        LV_ALIGN_TOP_MID,
        0,
        40
    );

    /*
     * 第一行三个按钮：
     * 红、绿、蓝
     */
    create_color_button(
        g_color_content,
        0,
        "RED",
        15,
        95
    );

    create_color_button(
        g_color_content,
        1,
        "GREEN",
        155,
        95
    );

    create_color_button(
        g_color_content,
        2,
        "BLUE",
        295,
        95
    );

    /* 第二行三个按钮：黑、白、灰 */
    create_color_button(
        g_color_content,
        3,
        "BLACK",
        15,
        190
    );

    create_color_button(
        g_color_content,
        4,
        "WHITE",
        155,
        190
    );

    create_color_button(
        g_color_content,
        5,
        "GRAY",
        295,
        190
    );

    /* 左滑提示 */
    lv_obj_t *swipe_hint =
        lv_label_create(g_color_content);

    lv_label_set_text(
        swipe_hint,
        "Swipe left to enter the touch test"
    );

    lv_obj_set_style_text_font(
        swipe_hint,
        &lv_font_montserrat_16,
        0
    );

    lv_obj_set_style_text_color(
        swipe_hint,
        lv_color_hex(0x8EA2BC),
        0
    );

    lv_obj_align(
        swipe_hint,
        LV_ALIGN_CENTER,
        0,
        88
    );

    /* 第1个分页圆点高亮 */
    create_page_indicator(g_color_content, 0);

    /*
     * 创建纯色模式恢复按钮。
     * 默认隐藏，选择颜色后才显示。
     */
    g_restore_button = lv_btn_create(g_main_screen);
    lv_obj_set_size(g_restore_button, 110, 46);

    lv_obj_align(
        g_restore_button,
        LV_ALIGN_TOP_MID,
        0,
        32
    );

    lv_obj_set_style_radius(
        g_restore_button,
        23,
        0
    );

    lv_obj_set_style_border_width(
        g_restore_button,
        2,
        0
    );

    lv_obj_set_style_border_color(
        g_restore_button,
        lv_color_hex(0x808080),
        0
    );

    lv_obj_add_event_cb(
        g_restore_button,
        restore_button_event,
        LV_EVENT_CLICKED,
        nullptr
    );

    lv_obj_t *restore_label =
        lv_label_create(g_restore_button);

    lv_label_set_text(
        restore_label,
        "RESTORE"
    );

    lv_obj_set_style_text_font(restore_label, &lv_font_montserrat_16, 0);

    lv_obj_center(restore_label);

    /* 初始状态隐藏恢复按钮 */
    lv_obj_add_flag(
        g_restore_button,
        LV_OBJ_FLAG_HIDDEN
    );
}

/**
 * @brief 进入按钮事件
 *
 * 点击进入按钮后，向左滑动切换到主界面占位页。
 *
 * @param event LVGL事件对象
 */
static void enter_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    Serial.println("UI: ENTER button clicked");

    /* 创建主界面第1页：颜色测试 */
    create_color_test_screen();

    lv_scr_load_anim(
        g_main_screen,
        LV_SCR_LOAD_ANIM_MOVE_LEFT,
        120,
        0,
        true
    );

    g_info_screen = nullptr;
}


/**
 * @brief 创建项目启动信息页
 */
static void create_information_screen()
{
    g_info_screen = lv_obj_create(nullptr);
    set_screen_background(g_info_screen);

    /*
     * 项目名称放在圆屏顶部安全区域。
     * 默认字体能够完整显示英文、数字和符号。
     */
    lv_obj_t *project_name = lv_label_create(g_info_screen);
    lv_label_set_text(
        project_name,
        "HXESP4848AI0210DIC-A"
    );
    lv_obj_set_style_text_font(project_name, &lv_font_montserrat_20, 0);

    lv_obj_set_style_text_color(
        project_name,
        lv_color_white(),
        0
    );

    lv_obj_set_style_text_align(
        project_name,
        LV_TEXT_ALIGN_CENTER,
        0
    );

    lv_obj_align(
        project_name,
        LV_ALIGN_TOP_MID,
        0,
        34
    );

    /* 项目副标题 */
    lv_obj_t *subtitle = lv_label_create(g_info_screen);
    lv_label_set_text(subtitle, "LVGL UI DEMO");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);

    lv_obj_set_style_text_color(
        subtitle,
        lv_color_hex(0x31C5F4),
        0
    );

    lv_obj_align(
        subtitle,
        LV_ALIGN_TOP_MID,
        0,
        58
    );

    /* 中央硬件信息卡片 */
    lv_obj_t *info_card = lv_obj_create(g_info_screen);
    lv_obj_set_size(info_card, 340, 190);

    lv_obj_align(
        info_card,
        LV_ALIGN_TOP_MID,
        0,
        120
    );

    /* 信息卡片使用圆角，适应圆形屏幕 */
    lv_obj_set_style_radius(info_card, 22, 0);

    lv_obj_set_style_bg_color(
        info_card,
        lv_color_hex(0x111F31),
        0
    );

    lv_obj_set_style_bg_opa(
        info_card,
        LV_OPA_COVER,
        0
    );

    lv_obj_set_style_border_width(info_card, 1, 0);

    lv_obj_set_style_border_color(
        info_card,
        lv_color_hex(0x28435F),
        0
    );

    lv_obj_set_style_pad_all(info_card, 0, 0);
    lv_obj_clear_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);

    create_device_information(info_card);

    /* 底部进入按钮 */
    lv_obj_t *enter_button = lv_btn_create(g_info_screen);
    lv_obj_set_size(enter_button, 180, 64);

    lv_obj_align(
        enter_button,
        LV_ALIGN_BOTTOM_MID,
        0,
        -24
    );

    /* 胶囊形圆角按钮 */
    lv_obj_set_style_radius(enter_button, 27, 0);

    lv_obj_set_style_bg_color(
        enter_button,
        lv_color_hex(0x087EA4),
        0
    );

    /* 按下按钮时改变颜色，显示触摸反馈 */
    lv_obj_set_style_bg_color(
        enter_button,
        lv_color_hex(0x055E7A),
        LV_STATE_PRESSED
    );

    lv_obj_set_style_shadow_width(
        enter_button,
        16,
        0
    );

    lv_obj_set_style_shadow_color(
        enter_button,
        lv_color_hex(0x087EA4),
        0
    );

    lv_obj_set_style_shadow_opa(
        enter_button,
        LV_OPA_40,
        0
    );

    lv_obj_add_event_cb(
        enter_button,
        enter_button_event,
        LV_EVENT_CLICKED,
        nullptr
    );

    /* 进入按钮文字 */
    lv_obj_t *enter_label = lv_label_create(enter_button);
    lv_label_set_text(enter_label, "ENTER");
    lv_obj_set_style_text_font(enter_label, &lv_font_montserrat_16, 0);

    lv_obj_set_style_text_color(
        enter_label,
        lv_color_white(),
        0
    );

    lv_obj_center(enter_label);
}


/**
 * @brief 返回按钮事件实现
 *
 * @param event LVGL事件对象
 */
static void back_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    Serial.println("UI: BACK button clicked");

    /* 重新创建启动信息页 */
    create_information_screen();

    lv_scr_load_anim(
        g_info_screen,
        LV_SCR_LOAD_ANIM_MOVE_RIGHT,
        120,
        0,
        true
    );

    g_main_screen = nullptr;
}


/**
 * @brief UI模块入口
 */
void ui_app_create()
{
    /* 创建启动信息页 */
    create_information_screen();

    /* 首次加载启动信息页时不播放滑动动画。 */
    lv_scr_load(g_info_screen);
}
