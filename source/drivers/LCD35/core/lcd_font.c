/**
 * @file lcd_font.c
 * @brief LCD ASCII 字体显示实现
 *
 * 使用 8x16 点阵字体(asc2_1608), 与 QDtech/STM32 例程兼容
 *
 * @copyright Copyright (c) 2024
 */

#include "../include/lcd_font.h"
#include "../include/lcd_driver.h"
#include "../hal/lcd_hal.h"
#include "lcd_core.h"
#include <string.h>

#define LCD_FONT_ASCII_COUNT    95U

/** 单字符 RGB888 缓冲(8x16) */
#define LCD_FONT_CHAR_BUF_SIZE  (LCD_FONT_CHAR_WIDTH * LCD_FONT_CHAR_HEIGHT * 3U)

static const uint8_t s_font_8x16[LCD_FONT_ASCII_COUNT][16] = {
#include "lcd_font_data.inc"
};

/**
 * @brief 获取字符在字库中的索引
 *
 * @param ch 输入字符
 * @return 字库索引, 非法字符返回空格索引
 */
static uint8_t lcd_font_get_index(char ch)
{
    if (ch < ' ' || ch > '~') {
        return 0U;
    }
    return (uint8_t)(ch - ' ');
}

/**
 * @brief 将 RGB565 转为 RGB888 字节
 */
static void lcd_font_rgb565_to_rgb888(uint16_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)((color >> 8) & 0xF8);
    *g = (uint8_t)((color >> 3) & 0xFC);
    *b = (uint8_t)((color << 3) & 0xF8);
}

/**
 * @brief 在指定位置绘制单个 ASCII 字符
 */
void lcd_font_draw_char(uint16_t x, uint16_t y, char ch, uint16_t fg_color, uint16_t bg_color)
{
    uint8_t index = lcd_font_get_index(ch);
    const uint8_t *glyph = s_font_8x16[index];
    uint8_t char_buf[LCD_FONT_CHAR_BUF_SIZE];
    uint8_t fg_r;
    uint8_t fg_g;
    uint8_t fg_b;
    uint8_t bg_r;
    uint8_t bg_g;
    uint8_t bg_b;
    uint16_t row;
    uint16_t col;
    uint32_t offset;

    lcd_font_rgb565_to_rgb888(fg_color, &fg_r, &fg_g, &fg_b);
    lcd_font_rgb565_to_rgb888(bg_color, &bg_r, &bg_g, &bg_b);

    for (row = 0U; row < LCD_FONT_CHAR_HEIGHT; row++) {
        uint8_t line = glyph[row];

        for (col = 0U; col < LCD_FONT_CHAR_WIDTH; col++) {
            offset = ((uint32_t)row * LCD_FONT_CHAR_WIDTH + col) * 3U;
            if ((line & 0x01U) != 0U) {
                char_buf[offset + 0U] = fg_r;
                char_buf[offset + 1U] = fg_g;
                char_buf[offset + 2U] = fg_b;
            } else {
                char_buf[offset + 0U] = bg_r;
                char_buf[offset + 1U] = bg_g;
                char_buf[offset + 2U] = bg_b;
            }
            line >>= 1;
        }
    }

    lcd_core_set_window(x, y, x + LCD_FONT_CHAR_WIDTH - 1U, y + LCD_FONT_CHAR_HEIGHT - 1U);
    lcd_hal_data_stream_begin();
    lcd_hal_data_stream_write(char_buf, LCD_FONT_CHAR_BUF_SIZE);
    lcd_hal_data_stream_end();
}

/**
 * @brief 在指定位置绘制 ASCII 字符串
 */
void lcd_font_draw_string(uint16_t x, uint16_t y, const char *text, uint16_t fg_color, uint16_t bg_color)
{
    uint16_t cursor_x = x;

    if (text == NULL) {
        return;
    }

    while (*text != '\0') {
        if (cursor_x + LCD_FONT_CHAR_WIDTH > lcd_driver_get_width()) {
            break;
        }
        if (y + LCD_FONT_CHAR_HEIGHT > lcd_driver_get_height()) {
            break;
        }

        lcd_font_draw_char(cursor_x, y, *text, fg_color, bg_color);
        cursor_x += LCD_FONT_CHAR_WIDTH + LCD_FONT_CHAR_GAP;
        text++;
    }
}

/**
 * @brief 在屏幕中央绘制 ASCII 字符串
 */
void lcd_font_draw_string_centered(const char *text, uint16_t fg_color, uint16_t bg_color)
{
    size_t len;
    uint16_t text_width;
    uint16_t x;
    uint16_t y;

    if (text == NULL) {
        return;
    }

    len = strlen(text);
    text_width = (uint16_t)(len * (LCD_FONT_CHAR_WIDTH + LCD_FONT_CHAR_GAP));
    if (len > 0U) {
        text_width -= LCD_FONT_CHAR_GAP;
    }

    x = 0U;
    y = 0U;
    if (text_width < lcd_driver_get_width()) {
        x = (lcd_driver_get_width() - text_width) / 2U;
    }
    if (LCD_FONT_CHAR_HEIGHT < lcd_driver_get_height()) {
        y = (lcd_driver_get_height() - LCD_FONT_CHAR_HEIGHT) / 2U;
    }

    lcd_font_draw_string(x, y, text, fg_color, bg_color);
}
