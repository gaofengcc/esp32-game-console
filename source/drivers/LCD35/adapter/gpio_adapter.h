/**
 * @file gpio_adapter.h
 * @brief GPIO适配器接口定义
 *
 * 提供跨平台的GPIO控制抽象,包括初始化、读写操作
 *
 * @copyright Copyright (c) 2024
 */

#ifndef GPIO_ADAPTER_H
#define GPIO_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD35 GPIO模式定义(避免与ESP-IDF的gpio_mode_t冲突)
 */
typedef enum {
    LCD35_GPIO_MODE_INPUT = 0,      /* 输入模式 */
    LCD35_GPIO_MODE_OUTPUT,         /* 输出模式 */
    LCD35_GPIO_MODE_OUTPUT_OD,      /* 开漏输出模式 */
} lcd35_gpio_mode_t;

/**
 * @brief LCD35 GPIO电平定义
 */
typedef enum {
    LCD35_GPIO_LEVEL_LOW = 0,       /* 低电平 */
    LCD35_GPIO_LEVEL_HIGH = 1,      /* 高电平 */
} lcd35_gpio_level_t;

/**
 * @brief GPIO初始化
 *
 * @param pin GPIO引脚号
 * @param mode GPIO模式
 * @return 0: 成功, 其他: 错误码
 */
int lcd35_gpio_init(int pin, lcd35_gpio_mode_t mode);

/**
 * @brief 设置GPIO输出电平
 *
 * @param pin GPIO引脚号
 * @param level 电平值
 */
void lcd35_gpio_write(int pin, lcd35_gpio_level_t level);

/**
 * @brief 读取GPIO输入电平
 *
 * @param pin GPIO引脚号
 * @return LCD35_GPIO_LEVEL_LOW 或 LCD35_GPIO_LEVEL_HIGH
 */
lcd35_gpio_level_t lcd35_gpio_read(int pin);

/**
 * @brief 快速设置GPIO为高电平
 *
 * @param pin GPIO引脚号
 */
void lcd35_gpio_set(int pin);

/**
 * @brief 快速设置GPIO为低电平
 *
 * @param pin GPIO引脚号
 */
void lcd35_gpio_clear(int pin);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_ADAPTER_H */
