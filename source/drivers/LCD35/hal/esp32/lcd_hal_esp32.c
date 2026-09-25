/**
 * @file lcd_hal_esp32.c
 * @brief ESP32平台LCD HAL层实现
 *
 * 使用硬件SPI与ILI9488 LCD控制器通信
 *
 * @copyright Copyright (c) 2024
 */

#include "lcd_hal.h"
#include "gpio_adapter.h"
#include "spi_adapter.h"
#include "delay_adapter.h"
#include "driver/ledc.h"
#include <string.h>

/* 静态上下文 */
static struct {
    lcd_hal_config_t config;
    spi_handle_t spi_handle;
    bool initialized;
    uint8_t backlight_duty;
} s_ctx = {
    .initialized = false,
    .backlight_duty = 100
};

/**
 * @brief 初始化LCD HAL
 */
int lcd_hal_init(const lcd_hal_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    if (s_ctx.initialized) {
        return 0;
    }

    memcpy(&s_ctx.config, config, sizeof(lcd_hal_config_t));

    /* 初始化GPIO */
    lcd35_gpio_init(config->pin_led, LCD35_GPIO_MODE_OUTPUT);
    lcd35_gpio_init(config->pin_dc, LCD35_GPIO_MODE_OUTPUT);
    lcd35_gpio_init(config->pin_rst, LCD35_GPIO_MODE_OUTPUT);
    lcd35_gpio_init(config->pin_cs, LCD35_GPIO_MODE_OUTPUT);

    /* 设置初始电平 */
    lcd35_gpio_write(config->pin_led, LCD35_GPIO_LEVEL_HIGH);   /* 背光开启 */
    lcd35_gpio_write(config->pin_dc, LCD35_GPIO_LEVEL_HIGH);    /* 数据模式 */
    lcd35_gpio_write(config->pin_rst, LCD35_GPIO_LEVEL_HIGH);   /* 不复位 */
    lcd35_gpio_write(config->pin_cs, LCD35_GPIO_LEVEL_HIGH);     /* 不选中 */

    /* 初始化SPI */
    spi_config_t spi_cfg = {
        .sck_pin = config->spi_sck,
        .mosi_pin = config->spi_mosi,
        .miso_pin = config->spi_miso,
        .cs_pin = -1,  /* 软件控制CS */
        .clock_speed_hz = config->spi_freq,
        .mode = 0,     /* SPI模式0 */
        .bits_per_word = 8,
        .cs_active_low = true
    };

    s_ctx.spi_handle = spi_master_init(&spi_cfg);
    if (s_ctx.spi_handle == NULL) {
        return -1;
    }

    /* 初始化PWM背光控制 */
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = config->pin_led,
        .duty = 255,  /* 初始全亮 */
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);

    s_ctx.backlight_duty = 100;
    s_ctx.initialized = true;

    return 0;
}

/**
 * @brief 释放LCD HAL资源
 */
void lcd_hal_deinit(void)
{
    if (!s_ctx.initialized) {
        return;
    }

    spi_master_deinit(s_ctx.spi_handle);
    s_ctx.spi_handle = NULL;
    s_ctx.initialized = false;
}

/**
 * @brief 复位LCD
 */
void lcd_hal_reset(void)
{
    lcd35_gpio_write(s_ctx.config.pin_rst, LCD35_GPIO_LEVEL_LOW);
    delay_ms(100);
    lcd35_gpio_write(s_ctx.config.pin_rst, LCD35_GPIO_LEVEL_HIGH);
    delay_ms(50);
}

/**
 * @brief 写命令
 */
void lcd_hal_write_cmd(uint8_t cmd)
{
    lcd35_gpio_write(s_ctx.config.pin_dc, LCD35_GPIO_LEVEL_LOW);  /* 命令模式 */
    lcd35_gpio_write(s_ctx.config.pin_cs, LCD35_GPIO_LEVEL_LOW);  /* 选中 */

    uint8_t data = cmd;
    spi_write_byte(s_ctx.spi_handle, &data, 1);

    lcd35_gpio_write(s_ctx.config.pin_cs, LCD35_GPIO_LEVEL_HIGH); /* 释放 */
}

/**
 * @brief 写8位数据
 */
void lcd_hal_write_data8(uint8_t data)
{
    lcd35_gpio_write(s_ctx.config.pin_dc, LCD35_GPIO_LEVEL_HIGH); /* 数据模式 */
    lcd35_gpio_write(s_ctx.config.pin_cs, LCD35_GPIO_LEVEL_LOW);  /* 选中 */

    uint8_t byte = data;
    spi_write_byte(s_ctx.spi_handle, &byte, 1);

    lcd35_gpio_write(s_ctx.config.pin_cs, LCD35_GPIO_LEVEL_HIGH); /* 释放 */
}

/**
 * @brief 写16位数据
 */
void lcd_hal_write_data16(uint16_t data)
{
    lcd35_gpio_write(s_ctx.config.pin_dc, LCD35_GPIO_LEVEL_HIGH); /* 数据模式 */
    lcd35_gpio_write(s_ctx.config.pin_cs, LCD35_GPIO_LEVEL_LOW);  /* 选中 */

    uint8_t bytes[2] = {(data >> 8) & 0xFF, data & 0xFF};
    spi_write_byte(s_ctx.spi_handle, bytes, 2);

    lcd35_gpio_write(s_ctx.config.pin_cs, LCD35_GPIO_LEVEL_HIGH); /* 释放 */
}

/**
 * @brief 批量写数据
 */
void lcd_hal_write_data_bulk(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return;
    }

    lcd_hal_data_stream_begin();
    lcd_hal_data_stream_write(data, len);
    lcd_hal_data_stream_end();
}

/**
 * @brief 开始连续数据写入
 */
void lcd_hal_data_stream_begin(void)
{
    lcd35_gpio_write(s_ctx.config.pin_dc, LCD35_GPIO_LEVEL_HIGH);
    lcd35_gpio_write(s_ctx.config.pin_cs, LCD35_GPIO_LEVEL_LOW);
}

/**
 * @brief 连续数据流写入
 */
void lcd_hal_data_stream_write(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return;
    }

    spi_write_byte(s_ctx.spi_handle, data, len);
}

/**
 * @brief 结束连续数据写入
 */
void lcd_hal_data_stream_end(void)
{
    lcd35_gpio_write(s_ctx.config.pin_cs, LCD35_GPIO_LEVEL_HIGH);
}

/**
 * @brief 设置背光亮度
 */
void lcd_hal_set_backlight(uint8_t brightness)
{
    if (brightness > 100) {
        brightness = 100;
    }

    s_ctx.backlight_duty = brightness;

    /* 将0-100映射到0-255 */
    uint32_t duty = (brightness * 255) / 100;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/**
 * @brief 片选控制
 */
void lcd_hal_cs(bool enable)
{
    lcd35_gpio_level_t level = enable ? LCD35_GPIO_LEVEL_LOW : LCD35_GPIO_LEVEL_HIGH;
    lcd35_gpio_write(s_ctx.config.pin_cs, level);
}

/**
 * @brief 毫秒延时
 */
void lcd_hal_delay_ms(uint32_t ms)
{
    delay_ms(ms);
}

/**
 * @brief 微秒延时
 */
void lcd_hal_delay_us(uint32_t us)
{
    delay_us(us);
}
