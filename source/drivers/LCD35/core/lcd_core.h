/**
 * @file lcd_core.h
 * @brief LCD核心层内部接口
 *
 * LCD核心层内部使用的函数声明
 *
 * @copyright Copyright (c) 2024
 */

#ifndef LCD_CORE_H
#define LCD_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 核心层API,仅内部使用 */
int lcd_core_init(void);
void lcd_core_set_direction(uint8_t direction);
uint8_t lcd_core_get_direction(void);
void lcd_core_clear(uint16_t color);
void lcd_core_fill_rect(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t color);
void lcd_core_set_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end);
void lcd_core_draw_point(uint16_t x, uint16_t y, uint16_t color);
uint16_t lcd_core_get_point_color(void);
void lcd_core_set_point_color(uint16_t color);
uint16_t lcd_core_get_back_color(void);
void lcd_core_set_back_color(uint16_t color);
uint16_t lcd_core_get_width(void);
uint16_t lcd_core_get_height(void);

#ifdef __cplusplus
}
#endif

#endif /* LCD_CORE_H */
