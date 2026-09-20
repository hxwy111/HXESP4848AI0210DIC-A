/*
 * LVGL configuration used only by the 09_LVGL_Demo sketch.
 *
 * The LCD is an RGB565 parallel RGB panel. Its pixel buffer must keep the
 * normal little-endian RGB565 byte order, so LV_COLOR_16_SWAP must be 0.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_FONT_MONTSERRAT_30 1

#endif /* LV_CONF_H */
