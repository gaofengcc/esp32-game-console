/**
 * @file lcd_driver.h
 * @brief LCD驱动API接口
 *
 * ILI9488 LCD显示屏驱动对外接口,用于ESP32-S3平台
 *
 * @copyright Copyright (c) 2024
 */

#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/* 类型定义                                                                  */
/*===========================================================================*/

/**
 * @brief LCD配置结构体
 */
typedef struct {
    int pin_led;            /* 背光控制引脚 */
    int pin_dc;             /* 数据/命令选择引脚 */
    int pin_rst;            /* 复位引脚 */
    int pin_cs;             /* 片选引脚 */
    int spi_sck;            /* SPI时钟引脚 */
    int spi_mosi;           /* SPI数据输出引脚 */
    int spi_miso;           /* SPI数据输入引脚(可选,填-1表示不使用) */
    uint32_t spi_freq;      /* SPI时钟频率(Hz) */
} lcd_driver_config_t;

/**
 * @brief LCD方向枚举
 */
typedef enum {
    LCD_DIR_0 = 0,          /* 0度 (竖屏) */
    LCD_DIR_90,             /* 90度 (横屏) */
    LCD_DIR_180,            /* 180度 (竖屏倒置) */
    LCD_DIR_270,            /* 270度 (横屏倒置) */
} lcd_dir_t;

/*===========================================================================*/
/* API函数声明                                                               */
/*===========================================================================*/

/**
 * @brief 初始化LCD驱动
 *
 * 初始化LCD硬件并配置显示参数
 *
 * @param config LCD配置参数,传NULL使用默认配置
 * @return ESP_OK: 成功,其他: 错误码
 */
esp_err_t lcd_driver_init(const lcd_driver_config_t *config);

/**
 * @brief 释放LCD驱动资源
 */
void lcd_driver_deinit(void);

/**
 * @brief 清屏
 *
 * 用指定颜色填充整个屏幕
 *
 * @param color RGB565颜色值
 */
void lcd_driver_clear(uint16_t color);

/**
 * @brief 设置显示方向
 *
 * @param dir LCD_DIR_0 / LCD_DIR_90 / LCD_DIR_180 / LCD_DIR_270
 * @note 切换后宽高会变化, 需调用方重绘; 触摸坐标需另行校准
 */
void lcd_driver_set_direction(lcd_dir_t dir);

/**
 * @brief 获取当前显示方向
 *
 * @return LCD_DIR_0 ~ LCD_DIR_270, 未初始化时返回 LCD_DIR_0
 */
lcd_dir_t lcd_driver_get_direction(void);

/**
 * @brief 获取LCD宽度
 *
 * @return LCD宽度(像素)
 */
uint16_t lcd_driver_get_width(void);

/**
 * @brief 获取LCD高度
 *
 * @return LCD高度(像素)
 */
uint16_t lcd_driver_get_height(void);

/**
 * @brief 设置窗口区域
 *
 * 设置后续绘图操作的窗口范围
 *
 * @param x 起始X坐标
 * @param y 起始Y坐标
 * @param w 窗口宽度
 * @param h 窗口高度
 */
void lcd_driver_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/**
 * @brief 画点
 *
 * @param x X坐标
 * @param y Y坐标
 * @param color RGB565颜色值
 */
void lcd_driver_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief 批量绘制像素
 *
 * @param x 起始X坐标
 * @param y 起始Y坐标
 * @param w 宽度
 * @param h 高度
 * @param data RGB565像素数据缓冲区
 */
void lcd_driver_draw_pixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);

/**
 * @brief 填充矩形
 *
 * @param x 起始X坐标
 * @param y 起始Y坐标
 * @param w 宽度
 * @param h 高度
 * @param color RGB565颜色值
 */
void lcd_driver_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief 设置背光亮度
 *
 * @param brightness 亮度值(0-100)
 */
void lcd_driver_set_backlight(uint8_t brightness);

/**
 * @brief 打开显示
 */
void lcd_driver_display_on(void);

/**
 * @brief 关闭显示
 */
void lcd_driver_display_off(void);

/*===========================================================================*/
/* 颜色宏定义                                                                */
/*===========================================================================*/

#define LCD_COLOR_BLACK         0x0000  /* 黑色 */
#define LCD_COLOR_NAVY          0x000F  /* 深蓝色 */
#define LCD_COLOR_DARKGREEN     0x03E0  /* 深绿色 */
#define LCD_COLOR_DARKCYAN      0x03EF  /* 深青色 */
#define LCD_COLOR_MAROON        0x7800  /* 褐红色 */
#define LCD_COLOR_PURPLE        0x780F  /* 紫色 */
#define LCD_COLOR_OLIVE         0x7BE0  /* 橄榄色 */
#define LCD_COLOR_LIGHTGREY     0xC618  /* 浅灰色 */
#define LCD_COLOR_DARKGREY      0x7BEF  /* 深灰色 */
#define LCD_COLOR_BLUE          0x001F  /* 蓝色 */
#define LCD_COLOR_GREEN         0x07E0  /* 绿色 */
#define LCD_COLOR_CYAN          0x07FF  /* 青色 */
#define LCD_COLOR_RED           0xF800  /* 红色 */
#define LCD_COLOR_MAGENTA       0xF81F  /* 洋红色 */
#define LCD_COLOR_YELLOW        0xFFE0  /* 黄色 */
#define LCD_COLOR_WHITE         0xFFFF  /* 白色 */
#define LCD_COLOR_ORANGE        0xFD20  /* 橙色 */

#ifdef __cplusplus
}
#endif

#endif /* LCD_DRIVER_H */
