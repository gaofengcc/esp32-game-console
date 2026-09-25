/**
 * @file lcd_core.c
 * @brief LCD核心绘图逻辑
 *
 * 平台无关的ILI9488 LCD绘图功能实现
 *
 * @copyright Copyright (c) 2024
 */

#include "../include/lcd_config.h"
#include "../hal/lcd_hal.h"
#include "../adapter/delay_adapter.h"
#include <string.h>

/** 单行 RGB888 缓冲最大宽度(与 LCD_WIDTH 一致) */
#define LCD_CORE_LINE_BUF_SIZE      (LCD_WIDTH * 3U)

/** 大块填充时每 N 行让出一次 CPU, 避免看门狗超时 */
#define LCD_CORE_YIELD_LINE_INTERVAL    4U

/*===========================================================================*/
/* LCD设备信息结构体                                                         */
/*===========================================================================*/

typedef struct {
    uint16_t width;         /* LCD宽度 */
    uint16_t height;        /* LCD高度 */
    uint16_t id;            /* LCD ID */
    uint8_t  dir;           /* 方向: 0-0度, 1-90度, 2-180度, 3-270度 */
    uint16_t wramcmd;       /* 写GRAM命令 */
    uint16_t setxcmd;       /* 设置X坐标命令 */
    uint16_t setycmd;       /* 设置Y坐标命令 */
} _lcd_dev;

/*===========================================================================*/
/* 静态变量                                                                  */
/*===========================================================================*/

static _lcd_dev lcddev;                 /* LCD设备信息 */
static uint16_t point_color = 0x0000;   /* 当前画笔颜色 */
static uint16_t back_color = 0xFFFF;   /* 当前背景颜色 */
static bool lcd_initialized = false;    /* 初始化标志 */

/*===========================================================================*/
/* 内部函数声明(前向声明)                                                     */
/*===========================================================================*/

static void lcd_write_reg(uint8_t reg, uint8_t data);
static void lcd_set_direction(uint8_t direction);

/*===========================================================================*/
/* 内部API函数实现(供本文件内其他函数调用)                                     */
/*===========================================================================*/

static void lcd_core_set_window_internal(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    /* 设置X坐标 */
    lcd_hal_write_cmd(lcddev.setxcmd);
    lcd_hal_write_data8(x_start >> 8);
    lcd_hal_write_data8(x_start & 0xFF);
    lcd_hal_write_data8(x_end >> 8);
    lcd_hal_write_data8(x_end & 0xFF);

    /* 设置Y坐标 */
    lcd_hal_write_cmd(lcddev.setycmd);
    lcd_hal_write_data8(y_start >> 8);
    lcd_hal_write_data8(y_start & 0xFF);
    lcd_hal_write_data8(y_end >> 8);
    lcd_hal_write_data8(y_end & 0xFF);

    /* 准备写GRAM */
    lcd_hal_write_cmd(lcddev.wramcmd);
}

/**
 * @brief 将 RGB565 转为 ILI9488 使用的 RGB888 分量
 */
static void lcd_core_rgb565_to_rgb888(uint16_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)((color >> 8) & 0xF8);
    *g = (uint8_t)((color >> 3) & 0xFC);
    *b = (uint8_t)((color << 3) & 0xF8);
}

/**
 * @brief 在当前窗口内用纯色填充矩形区域(按行批量 SPI 写入)
 *
 * @param width 区域宽度
 * @param height 区域高度
 * @param r 红色分量
 * @param g 绿色分量
 * @param b 蓝色分量
 */
static void lcd_core_fill_area_rgb888(uint16_t width, uint16_t height, uint8_t r, uint8_t g, uint8_t b)
{
    static uint8_t line_buf[LCD_CORE_LINE_BUF_SIZE];
    uint32_t x;
    uint32_t y;
    uint32_t line_bytes;

    if (width == 0U || height == 0U || width > LCD_WIDTH) {
        return;
    }

    line_bytes = (uint32_t)width * 3U;
    for (x = 0U; x < width; x++) {
        line_buf[x * 3U + 0U] = r;
        line_buf[x * 3U + 1U] = g;
        line_buf[x * 3U + 2U] = b;
    }

    lcd_hal_data_stream_begin();
    for (y = 0U; y < height; y++) {
        lcd_hal_data_stream_write(line_buf, line_bytes);
        if ((y % LCD_CORE_YIELD_LINE_INTERVAL) == 0U) {
            delay_ms(0);
        }
    }
    lcd_hal_data_stream_end();
}

/**
 * @brief 清屏(内部)
 */
static void lcd_core_clear_internal(uint16_t color)
{
    uint8_t r;
    uint8_t g;
    uint8_t b;

    lcd_core_rgb565_to_rgb888(color, &r, &g, &b);
    lcd_core_set_window_internal(0, 0, lcddev.width - 1, lcddev.height - 1);
    lcd_core_fill_area_rgb888(lcddev.width, lcddev.height, r, g, b);
}

/*===========================================================================*/
/* 内部函数实现                                                              */
/*===========================================================================*/

/**
 * @brief 写寄存器
 */
static void lcd_write_reg(uint8_t reg, uint8_t data)
{
    lcd_hal_write_cmd(reg);
    lcd_hal_write_data8(data);
}

/**
 * @brief 设置显示方向
 */
static void lcd_set_direction(uint8_t direction)
{
    lcddev.setxcmd = LCD_CMD_CASET;
    lcddev.setycmd = LCD_CMD_PASET;
    lcddev.wramcmd = LCD_CMD_RAMWR;

    switch (direction) {
        case 0: /* 0度 */
            lcddev.width = LCD_WIDTH;
            lcddev.height = LCD_HEIGHT;
            lcd_write_reg(LCD_CMD_MADCTL, MADCTL_BGR);
            break;
        case 1: /* 90度 */
            lcddev.width = LCD_HEIGHT;
            lcddev.height = LCD_WIDTH;
            lcd_write_reg(LCD_CMD_MADCTL, MADCTL_BGR | MADCTL_MX | MADCTL_MV);
            break;
        case 2: /* 180度 */
            lcddev.width = LCD_WIDTH;
            lcddev.height = LCD_HEIGHT;
            lcd_write_reg(LCD_CMD_MADCTL, MADCTL_BGR | MADCTL_MX | MADCTL_MY);
            break;
        case 3: /* 270度 */
            lcddev.width = LCD_HEIGHT;
            lcddev.height = LCD_WIDTH;
            lcd_write_reg(LCD_CMD_MADCTL, MADCTL_BGR | MADCTL_MY | MADCTL_MV);
            break;
        default:
            break;
    }

    lcddev.dir = direction;
}

/**
 * @brief 设置显示方向 (运行时)
 *
 * @param direction 0~3, 与 LCD_DIR_0 / 90 / 180 / 270 对应
 */
void lcd_core_set_direction(uint8_t direction)
{
    if (!lcd_initialized) {
        return;
    }
    if (direction > 3U) {
        return;
    }
    if (lcddev.dir == direction) {
        return;
    }

    lcd_set_direction(direction);
}

/**
 * @brief 获取当前显示方向
 *
 * @return 0~3, 未初始化时返回 0
 */
uint8_t lcd_core_get_direction(void)
{
    return lcddev.dir;
}

/*===========================================================================*/
/* 对外API函数实现                                                            */
/*===========================================================================*/

/**
 * @brief 初始化LCD核心
 */
int lcd_core_init(void)
{
    if (lcd_initialized) {
        return 0;
    }

    /* 硬件复位 */
    lcd_hal_reset();
    lcd_hal_delay_ms(120);

    /* ILI9488初始化序列 */
    lcd_hal_write_cmd(LCD_CMD_GAMMASET);
    lcd_hal_write_data8(0xA9);
    lcd_hal_write_data8(0x51);
    lcd_hal_write_data8(0x2C);
    lcd_hal_write_data8(0x82);

    lcd_hal_write_cmd(LCD_CMD_PWCTR1);
    lcd_hal_write_data8(0x11);
    lcd_hal_write_data8(0x09);

    lcd_hal_write_cmd(LCD_CMD_PWCTR2);
    lcd_hal_write_data8(0x41);

    lcd_hal_write_cmd(LCD_CMD_VMCTR1);
    lcd_hal_write_data8(0x00);
    lcd_hal_write_data8(0x0A);
    lcd_hal_write_data8(0x80);

    lcd_hal_write_cmd(LCD_CMD_FRMCTR1);
    lcd_hal_write_data8(0xB0);
    lcd_hal_write_data8(0x11);

    lcd_hal_write_cmd(LCD_CMD_INVCTR);
    lcd_hal_write_data8(0x02);

    lcd_hal_write_cmd(LCD_CMD_DISSET5);
    lcd_hal_write_data8(0x02);
    lcd_hal_write_data8(0x42);

    lcd_hal_write_cmd(0xB7);
    lcd_hal_write_data8(0xC6);

    lcd_hal_write_cmd(0xBE);
    lcd_hal_write_data8(0x00);
    lcd_hal_write_data8(0x04);

    lcd_hal_write_cmd(0xE9);
    lcd_hal_write_data8(0x00);

    lcd_hal_write_cmd(LCD_CMD_MADCTL);
    lcd_hal_write_data8((1 << 3) | (0 << 7) | (1 << 6) | (1 << 5));

    lcd_hal_write_cmd(LCD_CMD_COLMOD);
    lcd_hal_write_data8(0x66);  /* 18bit颜色 */

    lcd_hal_write_cmd(LCD_CMD_GMCTRP1);
    lcd_hal_write_data8(0x00);
    lcd_hal_write_data8(0x07);
    lcd_hal_write_data8(0x10);
    lcd_hal_write_data8(0x09);
    lcd_hal_write_data8(0x17);
    lcd_hal_write_data8(0x0B);
    lcd_hal_write_data8(0x41);
    lcd_hal_write_data8(0x89);
    lcd_hal_write_data8(0x4B);
    lcd_hal_write_data8(0x0A);
    lcd_hal_write_data8(0x0C);
    lcd_hal_write_data8(0x0E);
    lcd_hal_write_data8(0x18);
    lcd_hal_write_data8(0x1B);
    lcd_hal_write_data8(0x0F);

    lcd_hal_write_cmd(LCD_CMD_GMCTRN1);
    lcd_hal_write_data8(0x00);
    lcd_hal_write_data8(0x17);
    lcd_hal_write_data8(0x1A);
    lcd_hal_write_data8(0x04);
    lcd_hal_write_data8(0x0E);
    lcd_hal_write_data8(0x06);
    lcd_hal_write_data8(0x2F);
    lcd_hal_write_data8(0x45);
    lcd_hal_write_data8(0x43);
    lcd_hal_write_data8(0x02);
    lcd_hal_write_data8(0x0A);
    lcd_hal_write_data8(0x09);
    lcd_hal_write_data8(0x32);
    lcd_hal_write_data8(0x36);
    lcd_hal_write_data8(0x0F);

    lcd_hal_write_cmd(LCD_CMD_SLPOUT);
    lcd_hal_delay_ms(120);

    lcd_hal_write_cmd(LCD_CMD_DISPON);

    /* 设置显示方向 */
    lcd_set_direction(LCD_USE_HORIZONTAL);

    /* 打开背光 */
    lcd_hal_set_backlight(100);

    /* 清屏为白色 */
    lcd_core_clear_internal(LCD_COLOR_WHITE);

    lcd_initialized = true;

    return 0;
}

/**
 * @brief 清屏
 */
void lcd_core_clear(uint16_t color)
{
    lcd_core_clear_internal(color);
}

/**
 * @brief 填充矩形区域
 */
void lcd_core_fill_rect(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t color)
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint16_t width;
    uint16_t height;

    if (x_end < x_start || y_end < y_start) {
        return;
    }

    width = x_end - x_start + 1U;
    height = y_end - y_start + 1U;
    lcd_core_rgb565_to_rgb888(color, &r, &g, &b);
    lcd_core_set_window_internal(x_start, y_start, x_end, y_end);
    lcd_core_fill_area_rgb888(width, height, r, g, b);
}

/**
 * @brief 设置窗口
 */
void lcd_core_set_window(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    lcd_core_set_window_internal(x_start, y_start, x_end, y_end);
}

/**
 * @brief 画点
 */
void lcd_core_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_core_set_window(x, y, x, y);

    /* 18位颜色格式 */
    lcd_hal_write_data8((color >> 8) & 0xF8);
    lcd_hal_write_data8((color >> 3) & 0xFC);
    lcd_hal_write_data8((color << 3) & 0xF8);
}

/**
 * @brief 获取当前画笔颜色
 */
uint16_t lcd_core_get_point_color(void)
{
    return point_color;
}

/**
 * @brief 设置画笔颜色
 */
void lcd_core_set_point_color(uint16_t color)
{
    point_color = color;
}

/**
 * @brief 获取背景颜色
 */
uint16_t lcd_core_get_back_color(void)
{
    return back_color;
}

/**
 * @brief 设置背景颜色
 */
void lcd_core_set_back_color(uint16_t color)
{
    back_color = color;
}

/**
 * @brief 获取LCD宽度
 */
uint16_t lcd_core_get_width(void)
{
    return lcddev.width;
}

/**
 * @brief 获取LCD高度
 */
uint16_t lcd_core_get_height(void)
{
    return lcddev.height;
}
