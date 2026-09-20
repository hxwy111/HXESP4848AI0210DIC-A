#include "lvgl_port.h"

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_config.h"
#include "display_bsp.h"
#include "touch_bsp.h"
#include "lvgl.h"

static const char *TAG = "lvgl_port";

/*
 * 每个LVGL绘图缓冲区保存40行。
 *
 * 单个缓冲区大小：
 * 360 × 40 × 2 = 28800字节
 *
 * 使用双缓冲总计约57.6 KB内部DMA内存。
 */
#define LVGL_PORT_BUFFER_LINES       40

/* LVGL时基周期。 */
#define LVGL_PORT_TICK_PERIOD_MS     2

/* LVGL任务配置。 */
#define LVGL_PORT_TASK_STACK_SIZE    (6 * 1024)
#define LVGL_PORT_TASK_PRIORITY      4
#define LVGL_PORT_TASK_DELAY_MS      10

static lv_disp_draw_buf_t s_draw_buffer;
static lv_disp_drv_t s_display_driver;
static lv_indev_drv_t s_touch_driver;

static lv_color_t *s_buffer_1;
static lv_color_t *s_buffer_2;

static SemaphoreHandle_t s_lvgl_mutex;
static esp_timer_handle_t s_tick_timer;
static TaskHandle_t s_lvgl_task_handle;

/*
 * LCD物理分辨率为480×480，LVGL使用完整面板区域。
 */
#define LVGL_PORT_H_RES    BOARD_LCD_H_RES
#define LVGL_PORT_V_RES    BOARD_LCD_V_RES

/*
 * 当前面板没有额外偏移。
 */
#define LVGL_PORT_OFFSET_X 0

#define LVGL_PORT_OFFSET_Y 0

static bool s_initialized;

static void lvgl_port_touch_read(
    lv_indev_drv_t *input_driver,
    lv_indev_data_t *data
)
{
    (void)input_driver;

    static uint16_t last_x;
    static uint16_t last_y;
    static bool was_pressed;
    uint16_t x = last_x;
    uint16_t y = last_y;

    if (touch_bsp_read(&x, &y)) {
        const bool changed = !was_pressed || x != last_x || y != last_y;
        last_x = x;
        last_y = y;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        if (changed) {
            ESP_LOGI(TAG, "触摸按下: (%u,%u)", (unsigned)x, (unsigned)y);
        }
        was_pressed = true;
    } else {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_RELEASED;
        if (was_pressed) {
            ESP_LOGI(TAG, "触摸释放: (%u,%u)", (unsigned)last_x,
                     (unsigned)last_y);
        }
        was_pressed = false;
    }
}


/**
 * @brief LVGL显示刷新回调
 *
 * LVGL中的area结束坐标x2、y2是包含关系，
 * 而display_bsp_draw_bitmap()使用左闭右开坐标，
 * 因此传入时需要加1。
 */
static void lvgl_port_flush_callback(
    lv_disp_drv_t *display_driver,
    const lv_area_t *area,
    lv_color_t *color_map
)
{
    static unsigned flush_count;
    if (flush_count < 3) {
        ESP_LOGI(TAG, "LCD刷新区域=(%d,%d)-(%d,%d)",
                 area->x1, area->y1, area->x2, area->y2);
        flush_count++;
    }

    esp_err_t ret = display_bsp_draw_bitmap(
    area->x1 + LVGL_PORT_OFFSET_X,
    area->y1 + LVGL_PORT_OFFSET_Y,
    area->x2 + 1 + LVGL_PORT_OFFSET_X,
    area->y2 + 1 + LVGL_PORT_OFFSET_Y,
    color_map
);

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "LVGL区域刷新失败：%s，区域=(%d,%d)-(%d,%d)",
            esp_err_to_name(ret),
            area->x1,
            area->y1,
            area->x2,
            area->y2
        );
    }

    /*
     * display_bsp_draw_bitmap()内部已经等待DMA完成，
     * 所以此处可以通知LVGL绘图缓冲区已经可以复用。
     */
    lv_disp_flush_ready(display_driver);
}

/**
 * @brief esp_timer时基回调
 */
static void lvgl_port_tick_callback(void *argument)
{
    (void)argument;

    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

/**
 * @brief LVGL后台处理任务
 */
static void lvgl_port_task(void *argument)
{
    (void)argument;

    ESP_LOGI(TAG, "LVGL任务开始运行");

    while (true) {
        if (xSemaphoreTakeRecursive(
                s_lvgl_mutex,
                portMAX_DELAY
            ) == pdTRUE) {
            /*
             * 处理界面刷新、动画、输入设备及内部定时器。
             */
            lv_timer_handler();

            xSemaphoreGiveRecursive(s_lvgl_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(LVGL_PORT_TASK_DELAY_MS));
    }
}

bool lvgl_port_lock(uint32_t timeout_ms)
{
    if (s_lvgl_mutex == NULL) {
        return false;
    }

    TickType_t timeout_ticks;

    if (timeout_ms == UINT32_MAX) {
        timeout_ticks = portMAX_DELAY;
    } else {
        timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    }

    return xSemaphoreTakeRecursive(
               s_lvgl_mutex,
               timeout_ticks
           ) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    if (s_lvgl_mutex != NULL) {
        xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
}

esp_err_t lvgl_port_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "LVGL端口已经初始化");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "开始初始化LVGL端口");

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_lvgl_mutex == NULL) {
        ESP_LOGE(TAG, "创建LVGL互斥锁失败");
        return ESP_ERR_NO_MEM;
    }

    /*
     * LVGL绘图缓冲区将直接交给SPI DMA，因此必须放在
     * 支持DMA访问的内部内存中。
     */
   const size_t buffer_pixel_count =
    (size_t)LVGL_PORT_H_RES *
    LVGL_PORT_BUFFER_LINES;

    const size_t buffer_size =
        buffer_pixel_count * sizeof(lv_color_t);

    s_buffer_1 = heap_caps_malloc(
        buffer_size,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA
    );

    s_buffer_2 = heap_caps_malloc(
        buffer_size,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA
    );

    if (s_buffer_1 == NULL || s_buffer_2 == NULL) {
        ESP_LOGE(
            TAG,
            "申请LVGL绘图缓冲区失败，每个缓冲区大小=%u",
            (unsigned)buffer_size
        );

        if (s_buffer_1 != NULL) {
            heap_caps_free(s_buffer_1);
            s_buffer_1 = NULL;
        }

        if (s_buffer_2 != NULL) {
            heap_caps_free(s_buffer_2);
            s_buffer_2 = NULL;
        }

        vSemaphoreDelete(s_lvgl_mutex);
        s_lvgl_mutex = NULL;

        return ESP_ERR_NO_MEM;
    }

    lv_init();

    lv_disp_draw_buf_init(
        &s_draw_buffer,
        s_buffer_1,
        s_buffer_2,
        buffer_pixel_count
    );

    lv_disp_drv_init(&s_display_driver);

    s_display_driver.hor_res = LVGL_PORT_H_RES;
    s_display_driver.ver_res = LVGL_PORT_V_RES;
    s_display_driver.flush_cb = lvgl_port_flush_callback;
    s_display_driver.draw_buf = &s_draw_buffer;

    lv_disp_t *display = lv_disp_drv_register(
        &s_display_driver
    );

    if (display == NULL) {
        ESP_LOGE(TAG, "注册LVGL显示驱动失败");
        return ESP_FAIL;
    }

    /*
     * 输入设备必须在显示驱动注册之后创建。LVGL 8 会把新输入设备
     * 关联到当前默认显示；如果此时还没有 display，注册会失败。
     * TP_INT 未接 ESP32 GPIO，因此 touch_bsp_read() 在 LVGL 任务中
     * 直接轮询 GT911 状态寄存器。
     */
    esp_err_t touch_ret = touch_bsp_init();
    if (touch_ret == ESP_OK) {
        lv_indev_drv_init(&s_touch_driver);
        s_touch_driver.type = LV_INDEV_TYPE_POINTER;
        s_touch_driver.read_cb = lvgl_port_touch_read;

        if (lv_indev_drv_register(&s_touch_driver) == NULL) {
            ESP_LOGW(TAG, "GT911 LVGL输入设备注册失败");
        } else {
            ESP_LOGI(TAG, "GT911 LVGL触摸输入已注册（轮询）");
        }
    } else {
        ESP_LOGW(TAG, "GT911输入已禁用：%s", esp_err_to_name(touch_ret));
    }

    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_port_tick_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = true,
    };

    ESP_RETURN_ON_ERROR(
        esp_timer_create(
            &tick_timer_args,
            &s_tick_timer
        ),
        TAG,
        "创建LVGL时基定时器失败"
    );

    ESP_RETURN_ON_ERROR(
        esp_timer_start_periodic(
            s_tick_timer,
            LVGL_PORT_TICK_PERIOD_MS * 1000
        ),
        TAG,
        "启动LVGL时基定时器失败"
    );

    BaseType_t task_result = xTaskCreate(
        lvgl_port_task,
        "lvgl",
        LVGL_PORT_TASK_STACK_SIZE,
        NULL,
        LVGL_PORT_TASK_PRIORITY,
        &s_lvgl_task_handle
    );

    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "创建LVGL任务失败");

        esp_timer_stop(s_tick_timer);
        esp_timer_delete(s_tick_timer);
        s_tick_timer = NULL;

        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "LVGL端口初始化完成，分辨率=%dx%d，双缓冲=%u字节×2",
        BOARD_LCD_H_RES,
        BOARD_LCD_V_RES,
        (unsigned)buffer_size
    );

    return ESP_OK;
}
