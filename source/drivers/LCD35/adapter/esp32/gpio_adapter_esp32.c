/**
 * @file gpio_adapter_esp32.c
 * @brief ESP32平台GPIO适配器实现
 *
 * 使用ESP-IDF的GPIO驱动实现
 *
 * @copyright Copyright (c) 2024
 */

#include "gpio_adapter.h"
#include "driver/gpio.h"

/**
 * @brief GPIO初始化实现
 */
int lcd35_gpio_init(int pin, lcd35_gpio_mode_t mode)
{
    gpio_mode_t esp_mode;

    switch (mode) {
        case LCD35_GPIO_MODE_INPUT:
            esp_mode = GPIO_MODE_INPUT;
            break;
        case LCD35_GPIO_MODE_OUTPUT:
            esp_mode = GPIO_MODE_OUTPUT;
            break;
        case LCD35_GPIO_MODE_OUTPUT_OD:
            esp_mode = GPIO_MODE_OUTPUT_OD;
            break;
        default:
            return -1;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = esp_mode,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return -1;
    }

    return 0;
}

/**
 * @brief 设置GPIO输出电平实现
 */
void lcd35_gpio_write(int pin, lcd35_gpio_level_t level)
{
    gpio_set_level(pin, level);
}

/**
 * @brief 读取GPIO输入电平实现
 */
lcd35_gpio_level_t lcd35_gpio_read(int pin)
{
    return (lcd35_gpio_level_t)gpio_get_level(pin);
}

/**
 * @brief 快速设置GPIO为高电平
 */
void lcd35_gpio_set(int pin)
{
    gpio_set_level(pin, 1);
}

/**
 * @brief 快速设置GPIO为低电平
 */
void lcd35_gpio_clear(int pin)
{
    gpio_set_level(pin, 0);
}
