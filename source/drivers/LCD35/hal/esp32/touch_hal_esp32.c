/**
 * @file touch_hal_esp32.c
 * @brief ESP32平台触摸HAL层实现
 *
 * 使用软件SPI与XPT2046触摸控制器通信
 *
 * @copyright Copyright (c) 2024
 */

#include "touch_hal.h"
#include "gpio_adapter.h"
#include "delay_adapter.h"
#include <stddef.h>
#include <string.h>

/* 静态上下文 */
static struct {
    touch_hal_config_t config;
    bool initialized;
} s_ctx = {
    .initialized = false
};

/**
 * @brief 软件SPI写字节
 */
static void touch_spi_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        if (data & 0x80) {
            lcd35_gpio_write(s_ctx.config.pin_din, LCD35_GPIO_LEVEL_HIGH);
        } else {
            lcd35_gpio_write(s_ctx.config.pin_din, LCD35_GPIO_LEVEL_LOW);
        }
        data <<= 1;

        lcd35_gpio_write(s_ctx.config.pin_clk, LCD35_GPIO_LEVEL_LOW);
        delay_us(1);
        lcd35_gpio_write(s_ctx.config.pin_clk, LCD35_GPIO_LEVEL_HIGH);
        delay_us(1);
    }
}

/**
 * @brief 软件SPI读字节
 */
static uint16_t touch_spi_read_byte(void)
{
    uint16_t value = 0;

    for (int i = 0; i < 16; i++) {
        value <<= 1;

        lcd35_gpio_write(s_ctx.config.pin_clk, LCD35_GPIO_LEVEL_LOW);
        delay_us(1);
        lcd35_gpio_write(s_ctx.config.pin_clk, LCD35_GPIO_LEVEL_HIGH);
        delay_us(1);

        if (lcd35_gpio_read(s_ctx.config.pin_do) == LCD35_GPIO_LEVEL_HIGH) {
            value++;
        }
    }

    return value;
}

/**
 * @brief 初始化触摸HAL
 */
int lcd35_touch_hal_init(const touch_hal_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    if (s_ctx.initialized) {
        return 0;
    }

    memcpy(&s_ctx.config, config, sizeof(touch_hal_config_t));

    /* 初始化GPIO */
    lcd35_gpio_init(config->pin_irq, LCD35_GPIO_MODE_INPUT);
    lcd35_gpio_init(config->pin_cs, LCD35_GPIO_MODE_OUTPUT);
    lcd35_gpio_init(config->pin_clk, LCD35_GPIO_MODE_OUTPUT);
    lcd35_gpio_init(config->pin_din, LCD35_GPIO_MODE_OUTPUT);
    lcd35_gpio_init(config->pin_do, LCD35_GPIO_MODE_INPUT);

    /* 设置初始电平 */
    lcd35_gpio_write(config->pin_cs, LCD35_GPIO_LEVEL_HIGH);    /* 不选中 */
    lcd35_gpio_write(config->pin_clk, LCD35_GPIO_LEVEL_HIGH);   /* 时钟高电平 */
    lcd35_gpio_write(config->pin_din, LCD35_GPIO_LEVEL_LOW);    /* 数据低 */

    s_ctx.initialized = true;

    return 0;
}

/**
 * @brief 释放触摸HAL资源
 */
void lcd35_touch_hal_deinit(void)
{
    s_ctx.initialized = false;
}

/**
 * @brief 检查是否被按下
 */
bool lcd35_touch_hal_is_pressed(void)
{
    if (!s_ctx.initialized) {
        return false;
    }

    /* 触摸中断低电平有效 */
    return (lcd35_gpio_read(s_ctx.config.pin_irq) == LCD35_GPIO_LEVEL_LOW);
}

/**
 * @brief 读取AD值
 */
uint16_t lcd35_touch_hal_read_ad(uint8_t cmd)
{
    if (!s_ctx.initialized) {
        return 0;
    }

    /* 选中触摸芯片 */
    lcd35_gpio_write(s_ctx.config.pin_cs, LCD35_GPIO_LEVEL_LOW);
    delay_us(1);

    /* 发送命令 */
    touch_spi_write_byte(cmd);

    /* 等待转换完成 */
    delay_us(6);

    /* 读取结果(16位,但只有高12位有效) */
    uint16_t value = touch_spi_read_byte();
    value >>= 4;  /* 右移4位,得到12位有效值 */

    /* 释放片选 */
    lcd35_gpio_write(s_ctx.config.pin_cs, LCD35_GPIO_LEVEL_HIGH);

    return value;
}

/**
 * @brief 片选控制
 */
void lcd35_touch_hal_cs(bool enable)
{
    if (!s_ctx.initialized) {
        return;
    }

    lcd35_gpio_level_t level = enable ? LCD35_GPIO_LEVEL_LOW : LCD35_GPIO_LEVEL_HIGH;
    lcd35_gpio_write(s_ctx.config.pin_cs, level);
}

/**
 * @brief 微秒延时
 */
void lcd35_touch_hal_delay_us(uint32_t us)
{
    delay_us(us);
}
