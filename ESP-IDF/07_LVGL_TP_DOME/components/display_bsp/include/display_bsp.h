#ifndef DISPLAY_BSP_H
#define DISPLAY_BSP_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

 /**
 * @brief 初始化第一阶段显示硬件
 *
 * 当前阶段只完成：
 * 1. 配置LCD背光GPIO。
 * 2. 配置LCD复位GPIO。
 * 3. 执行LCD硬件复位。
 * 4. 打开LCD背光。
 *
 * 本函数不初始化QSPI、GC9B72和LVGL。
 *
 * @return
 *  - ESP_OK：初始化成功
 *  - 其他值：GPIO配置或控制失败
 */
esp_err_t display_bsp_stage1_init(void);

/**
 * @brief 控制LCD背光
 *
 * @param enabled
 *  - true：打开背光
 *  - false：关闭背光
 *
 * @return
 *  - ESP_OK：设置成功
 *  - ESP_ERR_INVALID_ARG：背光GPIO配置无效
 *  - 其他值：GPIO操作失败
 */
esp_err_t display_bsp_set_backlight(bool enabled);

 /**
 * @brief 对LCD控制器执行硬件复位
 *
 * 复位时序：
 * 1. RESET拉低10 ms。
 * 2. RESET拉高。
 * 3. 等待120 ms，使LCD控制器完成启动。
 *
 * @return
 *  - ESP_OK：复位完成
 *  - ESP_ERR_INVALID_ARG：RESET GPIO配置无效
 *  - 其他值：GPIO操作失败
 */
esp_err_t display_bsp_hardware_reset(void);

 /**
 * @brief 初始化GC9B72及其QSPI通信接口
 *
 * 初始化完成后用黑色清空屏幕，并打开背光。
 *
 * @return ESP_OK表示初始化成功
 */
esp_err_t display_bsp_stage2_init(void);

/**
 * @brief 使用RGB565颜色填充整个屏幕
 *
 * @param rgb565 RGB565颜色值
 *
 * 常用颜色：
 * 0xF800：红色
 * 0x07E0：绿色
 * 0x001F：蓝色
 * 0xFFFF：白色
 * 0x0000：黑色
 */
esp_err_t display_bsp_fill_color(uint16_t rgb565);

/**
 * @param x_start 区域左边界
 * @param y_start 区域上边界
 * @param x_end   区域右边界，不包含该坐标
 * @param y_end   区域下边界，不包含该坐标
 * @param color_data RGB565像素数据
*/
esp_err_t display_bsp_draw_bitmap(
    int x_start,
    int y_start,
    int x_end,
    int y_end,
    const void *color_data
);

#ifdef __cplusplus
}
#endif

#endif
