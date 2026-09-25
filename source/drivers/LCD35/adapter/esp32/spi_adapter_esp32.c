/**
 * @file spi_adapter_esp32.c
 * @brief ESP32平台SPI适配器实现
 *
 * 使用ESP-IDF的SPI Master驱动实现硬件SPI
 * 同时支持软件SPI(用于触摸屏等低速设备)
 *
 * @copyright Copyright (c) 2024
 */

#include "spi_adapter.h"
#include "gpio_adapter.h"
#include "delay_adapter.h"
#include "driver/spi_master.h"
#include <string.h>
#include <stdlib.h>

/* SPI设备最大数量 */
#define SPI_MAX_DEVICES 2

/* SPI设备上下文结构体 */
typedef struct {
    spi_device_handle_t dev;    /* ESP-IDF SPI设备句柄 */
    int cs_pin;                 /* 片选引脚 */
    bool cs_active_low;         /* 片选低电平有效 */
    bool is_hw_spi;             /* 是否为硬件SPI */
    /* 软件SPI使用 */
    int sck_pin;
    int mosi_pin;
    int miso_pin;
} spi_context_t;

static spi_context_t s_spi_contexts[SPI_MAX_DEVICES];
static bool s_spi_bus_initialized[2] = {false, false}; /* SPI2_HOST, SPI3_HOST */

/**
 * @brief 获取空闲的SPI上下文索引
 */
static int get_free_context_index(void)
{
    for (int i = 0; i < SPI_MAX_DEVICES; i++) {
        if (s_spi_contexts[i].dev == NULL && !s_spi_contexts[i].is_hw_spi) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 初始化硬件SPI总线
 */
static esp_err_t init_spi_bus(int host, const spi_config_t *config)
{
    if (s_spi_bus_initialized[host]) {
        return ESP_OK;
    }

    spi_bus_config_t buscfg = {
        .miso_io_num = config->miso_pin,
        .mosi_io_num = config->mosi_pin,
        .sclk_io_num = config->sck_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,   /* Match ESP32-S3 SPI DMA single transaction limit */
    };

    esp_err_t ret = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        return ret;
    }

    s_spi_bus_initialized[host] = true;
    return ESP_OK;
}

/**
 * @brief SPI初始化实现
 */
spi_handle_t spi_master_init(const spi_config_t *config)
{
    if (config == NULL) {
        return NULL;
    }

    int ctx_idx = get_free_context_index();
    if (ctx_idx < 0) {
        return NULL;
    }

    spi_context_t *ctx = &s_spi_contexts[ctx_idx];
    memset(ctx, 0, sizeof(spi_context_t));

    /* 判断使用哪个SPI主机 */
    int host = SPI3_HOST; /* 默认使用SPI3(VSPI) */

    /* 根据引脚判断使用SPI2或SPI3 */
    if (config->sck_pin == 14 || config->mosi_pin == 13 || config->miso_pin == 12) {
        host = SPI2_HOST; /* HSPI */
    }

    /* 初始化SPI总线 */
    esp_err_t ret = init_spi_bus(host, config);
    if (ret != ESP_OK) {
        return NULL;
    }

    /* 配置SPI设备 */
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = config->clock_speed_hz,
        .mode = config->mode,
        .spics_io_num = config->cs_pin,
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY,
    };

    ret = spi_bus_add_device(host, &devcfg, &ctx->dev);
    if (ret != ESP_OK) {
        return NULL;
    }

    ctx->cs_pin = config->cs_pin;
    ctx->cs_active_low = config->cs_active_low;
    ctx->is_hw_spi = true;
    ctx->sck_pin = config->sck_pin;
    ctx->mosi_pin = config->mosi_pin;
    ctx->miso_pin = config->miso_pin;

    /* 配置CS引脚 */
    if (config->cs_pin >= 0) {
        lcd35_gpio_init(config->cs_pin, LCD35_GPIO_MODE_OUTPUT);
        lcd35_gpio_write(config->cs_pin, config->cs_active_low ? LCD35_GPIO_LEVEL_HIGH : LCD35_GPIO_LEVEL_LOW);
    }

    return (spi_handle_t)ctx;
}

/**
 * @brief SPI去初始化实现
 */
void spi_master_deinit(spi_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    spi_context_t *ctx = (spi_context_t *)handle;

    if (ctx->is_hw_spi && ctx->dev != NULL) {
        spi_bus_remove_device(ctx->dev);
        ctx->dev = NULL;
    }

    ctx->is_hw_spi = false;
}

/**
 * @brief SPI发送数据实现
 */
int spi_write_byte(spi_handle_t handle, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || len == 0) {
        return -1;
    }

    spi_context_t *ctx = (spi_context_t *)handle;

    if (!ctx->is_hw_spi) {
        return -1;
    }

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    t.rx_buffer = NULL;

    esp_err_t ret = spi_device_transmit(ctx->dev, &t);
    return (ret == ESP_OK) ? 0 : -1;
}

/**
 * @brief SPI接收数据实现
 */
int spi_read_byte(spi_handle_t handle, uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || len == 0) {
        return -1;
    }

    spi_context_t *ctx = (spi_context_t *)handle;

    if (!ctx->is_hw_spi) {
        return -1;
    }

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = NULL;
    t.rx_buffer = data;

    esp_err_t ret = spi_device_transmit(ctx->dev, &t);
    return (ret == ESP_OK) ? 0 : -1;
}

/**
 * @brief SPI全双工传输实现
 */
int spi_transfer_byte(spi_handle_t handle, const uint8_t *tx_data, uint8_t *rx_data, size_t len)
{
    if (handle == NULL || len == 0) {
        return -1;
    }

    spi_context_t *ctx = (spi_context_t *)handle;

    if (!ctx->is_hw_spi) {
        return -1;
    }

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = tx_data;
    t.rx_buffer = rx_data;

    esp_err_t ret = spi_device_transmit(ctx->dev, &t);
    return (ret == ESP_OK) ? 0 : -1;
}

/**
 * @brief 设置片选信号实现
 */
void spi_set_cs(spi_handle_t handle, bool active)
{
    if (handle == NULL) {
        return;
    }

    spi_context_t *ctx = (spi_context_t *)handle;

    if (ctx->cs_pin < 0) {
        return;
    }

    lcd35_gpio_level_t level;
    if (ctx->cs_active_low) {
        level = active ? LCD35_GPIO_LEVEL_LOW : LCD35_GPIO_LEVEL_HIGH;
    } else {
        level = active ? LCD35_GPIO_LEVEL_HIGH : LCD35_GPIO_LEVEL_LOW;
    }

    lcd35_gpio_write(ctx->cs_pin, level);
}
