/**
 * @file lcd_driver.c
 * @brief LCD驱动API实现
 *
 * 提供用户友好的LCD操作接口
 *
 * @copyright Copyright (c) 2024
 */

#include "../include/lcd_driver.h"
#include "../include/lcd_config.h"
#include "../hal/lcd_hal.h"
#include "lcd_core.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "lcd_driver";

/* 静态配置 */
static lcd_driver_config_t s_config;
static bool s_initialized = false;

/**
 * @brief 初始化LCD驱动
 */
esp_err_t lcd_driver_init(const lcd_driver_config_t *config)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* 使用默认配置或用户配置 */
    if (config != NULL) {
        memcpy(&s_config, config, sizeof(lcd_driver_config_t));
    } else {
        /* 默认配置 */
        s_config.pin_led = 4;
        s_config.pin_dc = 10; 
        s_config.pin_rst = 9;
        s_config.pin_cs = 8;
        s_config.spi_sck = 12;
        s_config.spi_mosi = 11;
        s_config.spi_miso = 13;
        s_config.spi_freq = 40000000;
    }

    /* 初始化HAL层 */
    lcd_hal_config_t hal_config = {
        .pin_led = s_config.pin_led,
        .pin_dc = s_config.pin_dc,
        .pin_rst = s_config.pin_rst,
        .pin_cs = s_config.pin_cs,
        .spi_host = LCD_SPI_HOST,
        .spi_sck = s_config.spi_sck,
        .spi_mosi = s_config.spi_mosi,
        .spi_miso = s_config.spi_miso,
        .spi_freq = s_config.spi_freq
    };

    if (lcd_hal_init(&hal_config) != 0) {
        ESP_LOGE(TAG, "LCD HAL init failed");
        return ESP_FAIL;
    }

    /* 初始化核心层 */
    if (lcd_core_init() != 0) {
        ESP_LOGE(TAG, "LCD core init failed");
        lcd_hal_deinit();
        return ESP_FAIL;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "LCD driver initialized, size: %dx%d",
             lcd_core_get_width(), lcd_core_get_height());

    return ESP_OK;
}

/**
 * @brief 释放LCD驱动资源
 */
void lcd_driver_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    lcd_hal_deinit();
    s_initialized = false;
}

/**
 * @brief 清屏
 */
void lcd_driver_clear(uint16_t color)
{
    lcd_core_clear(color);
}

/**
 * @brief 设置显示方向
 *
 * @param dir LCD_DIR_0 / LCD_DIR_90 / LCD_DIR_180 / LCD_DIR_270
 * @note 切换方向后需自行重绘画面; 未初始化或非法 dir 时忽略
 */
void lcd_driver_set_direction(lcd_dir_t dir)
{
    if (!s_initialized) {
        return;
    }
    if ((uint8_t)dir > 3U) {
        return;
    }

    lcd_core_set_direction((uint8_t)dir);
}

/**
 * @brief 获取当前显示方向
 *
 * @return 当前方向枚举
 */
lcd_dir_t lcd_driver_get_direction(void)
{
    return (lcd_dir_t)lcd_core_get_direction();
}

/**
 * @brief 获取LCD宽度
 */
uint16_t lcd_driver_get_width(void)
{
    return lcd_core_get_width();
}

/**
 * @brief 获取LCD高度
 */
uint16_t lcd_driver_get_height(void)
{
    return lcd_core_get_height();
}

/**
 * @brief 设置窗口区域
 */
void lcd_driver_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t x_end = x + w - 1;
    uint16_t y_end = y + h - 1;

    /* 限制范围 */
    if (x_end >= lcd_core_get_width()) {
        x_end = lcd_core_get_width() - 1;
    }
    if (y_end >= lcd_core_get_height()) {
        y_end = lcd_core_get_height() - 1;
    }

    lcd_core_set_window(x, y, x_end, y_end);
}

/**
 * @brief 画点
 */
void lcd_driver_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= lcd_core_get_width() || y >= lcd_core_get_height()) {
        return;
    }

    lcd_core_draw_point(x, y, color);
}

/**
 * @brief 批量绘制像素（DMA批量传输优化版）
 *
 * 将RGB565像素批量转换为RGB888后，通过SPI DMA分块发送，
 * 避免逐像素逐字节的SPI事务开销。
 * 每次SPI传输限制在4KB以内，兼容ESP32-S3 SPI DMA硬件限制。
 */
void lcd_driver_draw_pixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data)
{
    if (data == NULL) {
        return;
    }

    lcd_driver_set_window(x, y, w, h);

    uint32_t total_pixels = (uint32_t)w * h;

    /* 每次SPI DMA传输最大4092字节(ESP32-S3限制)，对应1364个RGB888像素
     * 使用4KB对齐的DMA缓冲区，放在内部RAM */
    #define SPI_DMA_CHUNK_PIXELS  1360   /* 1360 * 3 = 4080 bytes < 4092 limit */
    #define RGB888_BUF_SIZE       (SPI_DMA_CHUNK_PIXELS * 3)
    static DMA_ATTR uint8_t rgb888_buf[RGB888_BUF_SIZE];

    lcd_hal_data_stream_begin();

    uint32_t offset = 0;
    uint32_t chunk_count = 0;
    while (offset < total_pixels) {
        uint32_t chunk_pixels = total_pixels - offset;
        if (chunk_pixels > SPI_DMA_CHUNK_PIXELS) {
            chunk_pixels = SPI_DMA_CHUNK_PIXELS;
        }

        /* 批量RGB565→RGB888转换 */
        const uint16_t *src = data + offset;
        uint8_t *dst = rgb888_buf;
        for (uint32_t i = 0; i < chunk_pixels; i++) {
            uint16_t color = src[i];
            *dst++ = (uint8_t)((color >> 8) & 0xF8);   /* R */
            *dst++ = (uint8_t)((color >> 3) & 0xFC);   /* G */
            *dst++ = (uint8_t)((color << 3) & 0xF8);   /* B */
        }

        /* DMA批量SPI传输 */
        lcd_hal_data_stream_write(rgb888_buf, chunk_pixels * 3);
        offset += chunk_pixels;
        chunk_count++;

        /* Periodically yield CPU to prevent watchdog timeout */
        if ((chunk_count % 8) == 0) {
            taskYIELD();
        }
    }

    lcd_hal_data_stream_end();
}

/**
 * @brief 填充矩形
 */
void lcd_driver_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint16_t x_end;
    uint16_t y_end;

    if (w == 0U || h == 0U) {
        return;
    }
    if (x >= lcd_core_get_width() || y >= lcd_core_get_height()) {
        return;
    }

    x_end = x + w - 1U;
    y_end = y + h - 1U;
    if (x_end >= lcd_core_get_width()) {
        x_end = lcd_core_get_width() - 1U;
    }
    if (y_end >= lcd_core_get_height()) {
        y_end = lcd_core_get_height() - 1U;
    }

    lcd_core_fill_rect(x, y, x_end, y_end, color);
}

/**
 * @brief 设置背光亮度
 */
void lcd_driver_set_backlight(uint8_t brightness)
{
    lcd_hal_set_backlight(brightness);
}

/**
 * @brief 打开显示
 */
void lcd_driver_display_on(void)
{
    lcd_hal_write_cmd(LCD_CMD_DISPON);
    lcd_hal_set_backlight(100);
}

/**
 * @brief 关闭显示
 */
void lcd_driver_display_off(void)
{
    lcd_hal_write_cmd(LCD_CMD_DISPOFF);
    lcd_hal_set_backlight(0);
}
