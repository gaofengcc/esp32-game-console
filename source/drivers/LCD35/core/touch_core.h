/**
 * @file touch_core.h
 * @brief 触摸核心层内部接口
 *
 * 触摸核心层内部使用的函数声明
 *
 * @copyright Copyright (c) 2024
 */

#ifndef TOUCH_CORE_H
#define TOUCH_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 核心层API,仅内部使用 */
int touch_core_init(void);
uint8_t touch_core_scan(uint8_t mode);
bool touch_core_is_pressed(void);
void touch_core_get_coords(uint16_t *x, uint16_t *y);
int touch_core_save_calibration(void);
int touch_core_load_calibration(void);
void touch_core_get_calibration(float *xfac, float *yfac, int16_t *xoff, int16_t *yoff);
void touch_core_set_calibration(float xfac, float yfac, int16_t xoff, int16_t yoff, uint8_t touchtype);

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_CORE_H */
