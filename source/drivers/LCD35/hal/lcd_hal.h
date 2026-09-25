/**
 * @file lcd_hal.h
 * @brief LCD硬件抽象层接口定义
 *
 * 提供LCD控制器(ILI9488)的硬件操作抽象
 *
 * @copyright Copyright (c) 2024
 */

#ifndef LCD_HAL_H
#define LCD_HAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD HAL配置结构体
 */
typedef struct {
    int pin_led;        /* 背光控制引脚 */
    int pin_dc;         /* 数据/命令选择引脚 */
    int pin_rst;        /* 复位引脚 */
    int pin_cs;         /* 片选引脚 */
    int spi_host;       /* SPI主机号(2=HSPI, 3=VSPI) */
    int spi_sck;        /* SPI时钟引脚 */
    int spi_mosi;       /* SPI数据出引脚 */
    int spi_miso;       /* SPI数据入引脚 */
    uint32_t spi_freq;  /* SPI时钟频率(Hz) */
} lcd_hal_config_t;

/**
 * @brief 初始化LCD HAL层
 *
 * @param config HAL配置参数
 * @return 0: 成功, 其他: 错误码
 */
int lcd_hal_init(const lcd_hal_config_t *config);

/**
 * @brief 释放LCD HAL资源
 */
void lcd_hal_deinit(void);

/**
 * @brief 复位LCD控制器
 */
void lcd_hal_reset(void);

/**
 * @brief 写命令到LCD(8位)
 *
 * @param cmd 命令字节
 */
void lcd_hal_write_cmd(uint8_t cmd);

/**
 * @brief 写数据到LCD(8位)
 *
 * @param data 数据字节
 */
void lcd_hal_write_data8(uint8_t data);

/**
 * @brief 写数据到LCD(16位)
 *
 * @param data 16位数据
 */
void lcd_hal_write_data16(uint16_t data);

/**
 * @brief 批量写数据
 *
 * @param data 数据缓冲区
 * @param len 数据长度(字节)
 */
void lcd_hal_write_data_bulk(const uint8_t *data, uint32_t len);

/**
 * @brief 开始连续数据写入(保持 CS 选中, 用于大块 GRAM 填充)
 */
void lcd_hal_data_stream_begin(void);

/**
 * @brief 在连续数据流中写入缓冲区
 *
 * @param data 数据缓冲区
 * @param len 数据长度(字节)
 */
void lcd_hal_data_stream_write(const uint8_t *data, uint32_t len);

/**
 * @brief 结束连续数据写入(释放 CS)
 */
void lcd_hal_data_stream_end(void);

/**
 * @brief 设置背光亮度
 *
 * @param brightness 亮度值(0-100)
 */
void lcd_hal_set_backlight(uint8_t brightness);

/**
 * @brief 片选控制
 *
 * @param enable true: 选中, false: 释放
 */
void lcd_hal_cs(bool enable);

/**
 * @brief 毫秒级延时
 *
 * @param ms 毫秒数
 */
void lcd_hal_delay_ms(uint32_t ms);

/**
 * @brief 微秒级延时
 *
 * @param us 微秒数
 */
void lcd_hal_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* LCD_HAL_H */
