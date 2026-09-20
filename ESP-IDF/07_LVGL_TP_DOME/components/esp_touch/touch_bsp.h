#ifndef TOUCH_BSP_H
#define TOUCH_BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t touch_bsp_init(void);
bool touch_bsp_read(uint16_t *x, uint16_t *y);

/* Legacy compatibility wrappers. */
void touch_Init(void);
uint8_t getTouch(uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif

#endif
