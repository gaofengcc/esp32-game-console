/**
 * @file touch_driver.h
 * @brief 触摸屏驱动API接口
 *
 * XPT2046触摸屏驱动对外接口,用于ESP32-S3平台
 *
 * @copyright Copyright (c) 2024
 */

#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/* 类型定义                                                                  */
/*===========================================================================*/

/**
 * @brief 触摸配置结构体
 */
typedef struct {
    int pin_irq;            /* 触摸中断引脚 */
    int pin_cs;             /* 片选引脚 */
    int pin_clk;            /* 时钟引脚 */
    int pin_din;            /* 数据输入引脚(MOSI) */
    int pin_do;             /* 数据输出引脚(MISO) */
} touch_driver_config_t;

/**
 * @brief 触摸校准数据
 */
typedef struct {
    float xfac;             /* X校准因子 */
    float yfac;             /* Y校准因子 */
    int16_t xoff;           /* X偏移 */
    int16_t yoff;           /* Y偏移 */
    uint8_t touchtype;      /* 触摸类型(坐标系方向) */
    bool calibrated;        /* 是否已校准 */
} touch_calibration_t;

/**
 * @brief 触摸点信息
 */
typedef struct {
    uint16_t x;             /* X坐标 */
    uint16_t y;             /* Y坐标 */
    uint16_t raw_x;         /* 原始X坐标(物理值) */
    uint16_t raw_y;         /* 原始Y坐标(物理值) */
    bool pressed;           /* 是否按下 */
} touch_point_t;

/*===========================================================================*/
/* API函数声明                                                               */
/*===========================================================================*/

/**
 * @brief 初始化触摸驱动
 *
 * 初始化触摸屏硬件并尝试加载校准数据
 *
 * @param config 触摸配置参数,传NULL使用默认配置
 * @return ESP_OK: 成功,其他: 错误码
 */
esp_err_t touch_driver_init(const touch_driver_config_t *config);

/**
 * @brief 释放触摸驱动资源
 */
void touch_driver_deinit(void);

/**
 * @brief 扫描触摸状态
 *
 * 检测是否有触摸并更新坐标信息
 *
 * @return true: 有触摸, false: 无触摸
 */
bool touch_driver_scan(void);

/**
 * @brief 获取触摸点信息
 *
 * 内部会重新扫描,可能导致竞争条件。
 * 如需获取 scan() 后的缓存坐标而不重新扫描,
 * 请使用 touch_driver_get_cached_point()
 *
 * @param point 触摸点信息结构体
 * @return ESP_OK: 成功获取, ESP_ERR_INVALID_STATE: 无触摸
 */
esp_err_t touch_driver_get_point(touch_point_t *point);

/**
 * @brief 获取缓存的触摸点(不重新扫描)
 *
 * 返回最近一次 touch_driver_scan() 成功时缓存的坐标。
 * 不会触发 SPI 读取,适合在 scan() 之后立即调用。
 *
 * @param point 触摸点信息结构体输出
 * @return true: 有有效缓存数据, false: 无触摸或未初始化
 */
bool touch_driver_get_cached_point(touch_point_t *point);

/**
 * @brief 检查是否被按下
 *
 * @return true: 有触摸, false: 无触摸
 */
bool touch_driver_is_pressed(void);

/**
 * @brief 检测触摸硬件是否触发(IRQ 引脚)
 *
 * 不依赖校准数据, 适合简单触摸检测场景
 *
 * @return true: 检测到触摸, false: 无触摸
 */
bool touch_driver_is_touched(void);

/**
 * @brief 获取原始坐标(未经校准)
 *
 * @param x 原始X坐标输出
 * @param y 原始Y坐标输出
 * @return ESP_OK: 成功,其他: 错误码
 */
esp_err_t touch_driver_get_raw(uint16_t *x, uint16_t *y);

/**
 * @brief 执行触摸校准
 *
 * 显示校准界面并等待用户点击4个校准点
 * 注意:此函数会阻塞直到校准完成
 *
 * @return ESP_OK: 校准成功,其他: 错误码
 */
esp_err_t touch_driver_calibrate(void);

/**
 * @brief 保存校准数据
 *
 * @return ESP_OK: 保存成功,其他: 错误码
 */
esp_err_t touch_driver_save_calibration(void);

/**
 * @brief 加载校准数据
 *
 * @return ESP_OK: 加载成功,其他: 未校准或加载失败
 */
esp_err_t touch_driver_load_calibration(void);

/**
 * @brief 设置校准参数
 *
 * @param cal 校准数据
 * @return ESP_OK: 成功,其他: 错误码
 */
esp_err_t touch_driver_set_calibration(const touch_calibration_t *cal);

/**
 * @brief 获取校准参数
 *
 * @param cal 校准数据输出
 * @return ESP_OK: 成功,其他: 错误码
 */
esp_err_t touch_driver_get_calibration(touch_calibration_t *cal);

/**
 * @brief 清除校准数据
 *
 * @return ESP_OK: 清除成功,其他: 错误码
 */
esp_err_t touch_driver_clear_calibration(void);

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_DRIVER_H */
