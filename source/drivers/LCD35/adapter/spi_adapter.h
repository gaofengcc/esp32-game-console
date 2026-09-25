/**
 * @file spi_adapter.h
 * @brief SPI适配器接口定义
 *
 * 提供跨平台的SPI通信抽象,支持主控模式
 *
 * @copyright Copyright (c) 2024
 */

#ifndef SPI_ADAPTER_H
#define SPI_ADAPTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SPI句柄类型定义
 */
typedef void* spi_handle_t;

/**
 * @brief SPI配置结构体
 */
typedef struct {
    int sck_pin;            /* 时钟引脚 */
    int mosi_pin;           /* 主出从入引脚 */
    int miso_pin;           /* 主入从出引脚 */
    int cs_pin;             /* 片选引脚 */
    uint32_t clock_speed_hz;/* 时钟频率(Hz) */
    uint8_t mode;           /* SPI模式(0-3) */
    uint8_t bits_per_word;  /* 每字位数(通常为8) */
    bool cs_active_low;     /* 片选低电平有效 */
} spi_config_t;

/**
 * @brief 初始化SPI主机
 *
 * @param config SPI配置参数
 * @return SPI句柄,失败返回NULL
 */
spi_handle_t spi_master_init(const spi_config_t *config);

/**
 * @brief 释放SPI主机资源
 *
 * @param handle SPI句柄
 */
void spi_master_deinit(spi_handle_t handle);

/**
 * @brief 发送数据(8位)
 *
 * @param handle SPI句柄
 * @param data 发送数据缓冲区
 * @param len 数据长度(字节)
 * @return 0: 成功, 其他: 错误码
 */
int spi_write_byte(spi_handle_t handle, const uint8_t *data, size_t len);

/**
 * @brief 接收数据(8位)
 *
 * @param handle SPI句柄
 * @param data 接收数据缓冲区
 * @param len 数据长度(字节)
 * @return 0: 成功, 其他: 错误码
 */
int spi_read_byte(spi_handle_t handle, uint8_t *data, size_t len);

/**
 * @brief 同时发送和接收数据(全双工)
 *
 * @param handle SPI句柄
 * @param tx_data 发送数据缓冲区
 * @param rx_data 接收数据缓冲区
 * @param len 数据长度(字节)
 * @return 0: 成功, 其他: 错误码
 */
int spi_transfer_byte(spi_handle_t handle, const uint8_t *tx_data, uint8_t *rx_data, size_t len);

/**
 * @brief 设置片选信号
 *
 * @param handle SPI句柄
 * @param active 是否激活(true: 激活, false: 释放)
 */
void spi_set_cs(spi_handle_t handle, bool active);

#ifdef __cplusplus
}
#endif

#endif /* SPI_ADAPTER_H */
