#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "display_bsp.h"
#include "i2c_bsp.h"
#include "lvgl.h"
#include "lv_demos.h"
#include "lvgl_port.h"
#include "qmi8658c.h"
#include "sd_card_bsp.h"
#include "buzzer.h"

static const char *TAG = "main";

#define IMU_LOG_PATH SD_CARD_MOUNT_POINT "/qmi8658.csv"
#define IMU_SAMPLE_COUNT 20
/* 每行约80字节，加上表头留足余量。缓冲区必须放在堆上：
 * 两个4 KB的栈上数组会直接撑穿8 KB的任务栈并踩坏相邻的TCB。 */
#define IMU_CSV_BUFFER_SIZE 2048

static void imu_sd_capture_task(void *argument)
{
    (void)argument;

    char *csv = NULL;
    char *readback = NULL;

    ESP_LOGI(TAG, "Initializing QMI8658");
    if (qmi8658_init() == 0) {
        /* Keep a sensor wiring fault from taking down the display task. */
        printf("QMI8658 initialization failed; check 3.3V, SDA=GPIO41, "
               "SCL=GPIO40 and address 0x6A/0x6B\n");
        const esp_err_t probe6a = i2c_bsp_probe(0x6A);
        const esp_err_t probe6b = i2c_bsp_probe(0x6B);
        printf("I2C probe 0x6A: %s, 0x6B: %s\n",
               probe6a == ESP_OK ? "ACK" : "no response",
               probe6b == ESP_OK ? "ACK" : "no response");
        fflush(stdout);
        goto cleanup;
    }

    /* 等LVGL把首屏刷完再挂载SD卡：首帧渲染时RGB面板和八线PSRAM正满带宽跑，
     * 与SDMMC的DMA抢总线会把本就不足的时序余量推过临界点。 */
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_err_t ret = sd_card_bsp_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed; IMU data will not be saved: %s",
                 esp_err_to_name(ret));
        goto cleanup;
    }

    csv = malloc(IMU_CSV_BUFFER_SIZE);
    /* 回读缓冲区多留一字节，用于补上字符串结束符后再打印。 */
    readback = malloc(IMU_CSV_BUFFER_SIZE + 1);
    if (csv == NULL || readback == NULL) {
        ESP_LOGE(TAG, "Unable to allocate CSV buffers");
        goto cleanup;
    }

    int header_len = snprintf(csv, IMU_CSV_BUFFER_SIZE,
                              "time_ms,ax,ay,az,gx,gy,gz\n");
    if (header_len < 0 || header_len >= IMU_CSV_BUFFER_SIZE) {
        ESP_LOGE(TAG, "CSV buffer is too small for the header");
        goto cleanup;
    }

    size_t used = (size_t)header_len;
    for (int i = 0; i < IMU_SAMPLE_COUNT && used < IMU_CSV_BUFFER_SIZE; ++i) {
        float acc[3] = {0};
        float gyro[3] = {0};
        qmi8658_read_xyz(acc, gyro);

        const int written = snprintf(
            csv + used, IMU_CSV_BUFFER_SIZE - used,
            "%lld,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
            (long long)(esp_timer_get_time() / 1000),
            acc[0], acc[1], acc[2], gyro[0], gyro[1], gyro[2]);
        if (written <= 0 || (size_t)written >= IMU_CSV_BUFFER_SIZE - used) {
            ESP_LOGW(TAG, "CSV buffer is full after %d samples", i);
            break;
        }
        used += (size_t)written;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ret = sd_card_bsp_write_file(IMU_LOG_PATH, csv, used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write QMI8658 data to SD card: %s",
                 esp_err_to_name(ret));
        goto cleanup;
    }
    ESP_LOGI(TAG, "QMI8658 data written to %s (%u bytes)", IMU_LOG_PATH,
             (unsigned)used);

    size_t read_len = 0;
    ret = sd_card_bsp_read_file(IMU_LOG_PATH, readback, IMU_CSV_BUFFER_SIZE,
                                &read_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read QMI8658 data back from SD card: %s",
                 esp_err_to_name(ret));
        goto cleanup;
    }

    /* 读取接口只填充有效字节数，打印前必须自己补上结束符。 */
    readback[read_len] = '\0';
    printf("\n===== QMI8658 data read back from SD (%u bytes) =====\n%s"
           "===== Readback complete =====\n",
           (unsigned)read_len, readback);

cleanup:
    free(csv);
    free(readback);
    vTaskDelete(NULL);
}

static void display_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    lv_obj_t *button = lv_event_get_target(event);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label != NULL) {
        lv_label_set_text(label, "Clicked OK");
    }
    ESP_LOGI(TAG, "Display button clicked");
}

static void create_test_ui(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x202A44), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "ST7701S + LVGL");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 70);

    lv_obj_t *button = lv_btn_create(screen);
    lv_obj_set_size(button, 170, 60);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x00A8E8), LV_PART_MAIN);
    lv_obj_set_style_radius(button, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(button, display_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Display OK");
    lv_obj_set_style_text_color(button_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(button_label);

    lv_obj_t *status = lv_label_create(screen);
    lv_label_set_text(status, "480 x 480  RGB565  SPI init + RGB");
    lv_obj_set_style_text_color(status, lv_color_hex(0xBFC9D9), LV_PART_MAIN);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -70);
}

void app_main(void)
{
    if (xTaskCreate(imu_sd_capture_task, "imu_sd_capture", 8192,
                    NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create QMI8658/SD capture task");
    }

    ESP_LOGI(TAG, "Initializing ST7701S RGB565 display");
    ESP_ERROR_CHECK(display_bsp_stage2_init());

    ESP_LOGI(TAG, "Initializing LVGL display port");
    ESP_ERROR_CHECK(lvgl_port_init());

    if (lvgl_port_lock(UINT32_MAX)) {
        //create_test_ui();
        lv_demo_widgets();
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
    }

    ESP_LOGI(TAG, "LVGL test UI created");

    Buzzer_while();
    
}
