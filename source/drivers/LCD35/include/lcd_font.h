/**
 * @file lcd_font.h
 * @brief LCD ASCII 字体显示接口
 *
 * 基于 8x16 点阵字体, 支持居中显示字符串
 *
 * @copyright Copyright (c) 2024
 */

#ifndef LCD_FONT_H
#define LCD_FONT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 16 号字体字符宽度(像素) */
#define LCD_FONT_CHAR_WIDTH     8U

/** 16 号字体字符高度(像素) */
#define LCD_FONT_CHAR_HEIGHT    16U

/** 字符间距(像素) */
#define LCD_FONT_CHAR_GAP       0U

/**
 * @brief 在指定位置绘制单个 ASCII 字符
 *
 * @param x 左上角 X 坐标
 * @param y 左上角 Y 坐标
 * @param ch 字符(建议大写 ASCII 可打印字符)
 * @param fg_color 前景色 RGB565
 * @param bg_color 背景色 RGB565
 */
void lcd_font_draw_char(uint16_t x, uint16_t y, char ch, uint16_t fg_color, uint16_t bg_color);

/**
 * @brief 在指定位置绘制 ASCII 字符串
 *
 * @param x 起始 X 坐标
 * @param y 起始 Y 坐标
 * @param text 以 '\\0' 结尾的字符串
 * @param fg_color 前景色 RGB565
 * @param bg_color 背景色 RGB565
 */
void lcd_font_draw_string(uint16_t x, uint16_t y, const char *text, uint16_t fg_color, uint16_t bg_color);

/**
 * @brief 在屏幕中央绘制 ASCII 字符串
 *
 * @param text 以 '\\0' 结尾的字符串
 * @param fg_color 前景色 RGB565
 * @param bg_color 背景色 RGB565
 */
void lcd_font_draw_string_centered(const char *text, uint16_t fg_color, uint16_t bg_color);

#ifdef __cplusplus
}
#endif

#endif /* LCD_FONT_H */
