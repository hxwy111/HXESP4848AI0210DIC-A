#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化LVGL、显示驱动、时基和运行任务
 *
 * 调用本函数前必须先完成display_bsp_stage2_init()。
 */
esp_err_t lvgl_port_init(void);

/**
 * @brief 获取LVGL互斥锁
 *
 * LVGL本身不是线程安全的。除LVGL任务之外的其他任务调用
 * LVGL API之前，需要先获取该锁。
 *
 * @param timeout_ms 等待时间，UINT32_MAX表示永久等待
 *
 * @return true表示成功获取互斥锁
 */
bool lvgl_port_lock(uint32_t timeout_ms);

/**
 * @brief 释放LVGL互斥锁
 */
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif

#endif