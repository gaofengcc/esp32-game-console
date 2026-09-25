/**
 * @file delay_adapter.h
 * @brief 延时适配器接口定义
 *
 * 提供跨平台的延时功能抽象,支持微秒级和毫秒级延时
 *
 * @copyright Copyright (c) 2024
 */

#ifndef DELAY_ADAPTER_H
#define DELAY_ADAPTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 微秒级延时
 *
 * @param us 延时时间,单位微秒
 */
void delay_us(uint32_t us);

/**
 * @brief 毫秒级延时
 *
 * @param ms 延时时间,单位毫秒
 */
void delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* DELAY_ADAPTER_H */
