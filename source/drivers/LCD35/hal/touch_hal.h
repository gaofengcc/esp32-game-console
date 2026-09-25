/**
 * @file touch_hal.h
 * @brief 触摸屏硬件抽象层接口定义
 *
 * 提供触摸屏控制器(XPT2046)的硬件操作抽象
 *
 * @copyright Copyright (c) 2024
 */

#ifndef TOUCH_HAL_H
#define TOUCH_HAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 触摸HAL配置结构体
 */
typedef struct {
    int pin_irq;        /* 触摸中断引脚 */
    int pin_cs;         /* 片选引脚 */
    int pin_clk;        /* 时钟引脚 */
    int pin_din;        /* 数据输入(MOSI)引脚 */
    int pin_do;         /* 数据输出(MISO)引脚 */
} touch_hal_config_t;

/**
 * @brief 初始化触摸HAL层
 *
 * @param config HAL配置参数
 * @return 0: 成功, 其他: 错误码
 */
int lcd35_touch_hal_init(const touch_hal_config_t *config);

/**
 * @brief 释放触摸HAL资源
 */
void lcd35_touch_hal_deinit(void);

/**
 * @brief 检查触摸是否被按下
 *
 * @return true: 有触摸, false: 无触摸
 */
bool lcd35_touch_hal_is_pressed(void);

/**
 * @brief 读取触摸AD值
 *
 * @param cmd 读取命令(0xD0=X, 0x90=Y)
 * @return 12位AD值
 */
uint16_t lcd35_touch_hal_read_ad(uint8_t cmd);

/**
 * @brief 片选控制
 *
 * @param enable true: 选中, false: 释放
 */
void lcd35_touch_hal_cs(bool enable);

/**
 * @brief 微秒级延时
 *
 * @param us 微秒数
 */
void lcd35_touch_hal_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_HAL_H */
