/**
 * @file touch_driver.c
 * @brief 触摸屏驱动API实现
 *
 * 提供用户友好的触摸屏操作接口
 *
 * @copyright Copyright (c) 2024
 */

#include "../include/touch_driver.h"
#include "../include/touch_config.h"
#include "../hal/touch_hal.h"
#include "touch_core.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "touch_driver";

/* 静态配置 */
static touch_driver_config_t s_config;
static touch_point_t s_point = {0};
static bool s_initialized = false;

/**
 * @brief 初始化触摸驱动
 */
esp_err_t touch_driver_init(const touch_driver_config_t *config)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* 使用默认配置或用户配置 */
    if (config != NULL) {
        memcpy(&s_config, config, sizeof(touch_driver_config_t));
    } else {
        /* 默认配置 */
        s_config.pin_irq = 5;
        s_config.pin_cs = 14;
        s_config.pin_clk = 15;
        s_config.pin_din = 7;
        s_config.pin_do = 6;
    }

    /* 初始化HAL层 */
    touch_hal_config_t hal_config = {
        .pin_irq = s_config.pin_irq,
        .pin_cs = s_config.pin_cs,
        .pin_clk = s_config.pin_clk,
        .pin_din = s_config.pin_din,
        .pin_do = s_config.pin_do
    };

    if (lcd35_touch_hal_init(&hal_config) != 0) {
        ESP_LOGE(TAG, "Touch HAL init failed");
        return ESP_FAIL;
    }

    /* 初始化核心层 */
    if (touch_core_init() != 0) {
        ESP_LOGE(TAG, "Touch core init failed");
        lcd35_touch_hal_deinit();
        return ESP_FAIL;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Touch driver initialized");

    return ESP_OK;
}

/**
 * @brief 释放触摸驱动资源
 */
void touch_driver_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    lcd35_touch_hal_deinit();
    s_initialized = false;
}

/**
 * @brief 扫描触摸状态
 */
bool touch_driver_scan(void)
{
    if (!s_initialized) {
        return false;
    }

    uint8_t res = touch_core_scan(0);

    if (res) {
        touch_core_get_coords(&s_point.x, &s_point.y);
        s_point.pressed = true;
    } else {
        s_point.pressed = false;
    }

    return s_point.pressed;
}

/**
 * @brief 获取触摸点信息
 */
esp_err_t touch_driver_get_point(touch_point_t *point)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (point == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 先扫描更新状态 */
    touch_driver_scan();

    if (!s_point.pressed) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(point, &s_point, sizeof(touch_point_t));
    return ESP_OK;
}

/**
 * @brief 获取缓存的触摸点(不重新扫描)
 */
bool touch_driver_get_cached_point(touch_point_t *point)
{
    if (!s_initialized || point == NULL) {
        return false;
    }

    if (!s_point.pressed) {
        return false;
    }

    memcpy(point, &s_point, sizeof(touch_point_t));
    return true;
}

/**
 * @brief 检查是否被按下
 */
bool touch_driver_is_pressed(void)
{
    if (!s_initialized) {
        return false;
    }

    return touch_core_is_pressed();
}

/**
 * @brief 检测触摸硬件 IRQ(不依赖校准)
 */
bool touch_driver_is_touched(void)
{
    if (!s_initialized) {
        return false;
    }

    return lcd35_touch_hal_is_pressed();
}

/**
 * @brief 获取原始坐标
 */
esp_err_t touch_driver_get_raw(uint16_t *x, uint16_t *y)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 需要直接访问HAL层读取原始数据 */
    if (lcd35_touch_hal_is_pressed()) {
        if (x != NULL) {
            *x = lcd35_touch_hal_read_ad(XPT2046_CMD_XPOS);
        }
        if (y != NULL) {
            *y = lcd35_touch_hal_read_ad(XPT2046_CMD_YPOS);
        }
        return ESP_OK;
    }

    return ESP_ERR_INVALID_STATE;
}

/**
 * @brief 执行触摸校准
 */
esp_err_t touch_driver_calibrate(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 校准功能需要显示校准界面,依赖于GUI或LCD驱动
     * 这里提供基本的校准逻辑框架,实际使用时需要结合LCD显示 */

    ESP_LOGI(TAG, "Starting touch calibration...");

    /* 提示:实际校准流程需要:
     * 1. 在屏幕四角显示校准点
     * 2. 等待用户依次点击4个点
     * 3. 计算校准因子
     * 4. 保存校准数据
     *
     * 由于本驱动与LCD驱动分离,校准功能需要由上层应用实现
     * 此处仅提供校准数据设置接口
     */

    return ESP_OK;
}

/**
 * @brief 保存校准数据
 */
esp_err_t touch_driver_save_calibration(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (touch_core_save_calibration() == 0) {
        ESP_LOGI(TAG, "Calibration data saved");
        return ESP_OK;
    }

    return ESP_FAIL;
}

/**
 * @brief 加载校准数据
 */
esp_err_t touch_driver_load_calibration(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (touch_core_load_calibration()) {
        ESP_LOGI(TAG, "Calibration data loaded");
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief 设置校准参数
 */
esp_err_t touch_driver_set_calibration(const touch_calibration_t *cal)
{
    if (!s_initialized || cal == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    touch_core_set_calibration(cal->xfac, cal->yfac, cal->xoff, cal->yoff, cal->touchtype);

    ESP_LOGI(TAG, "Calibration set: xfac=%.4f, yfac=%.4f, xoff=%d, yoff=%d",
             cal->xfac, cal->yfac, cal->xoff, cal->yoff);

    return ESP_OK;
}

/**
 * @brief 获取校准参数
 */
esp_err_t touch_driver_get_calibration(touch_calibration_t *cal)
{
    if (!s_initialized || cal == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    touch_core_get_calibration(&cal->xfac, &cal->yfac, &cal->xoff, &cal->yoff);

    /* 检查是否已校准 */
    cal->calibrated = (cal->xfac != 0 && cal->yfac != 0);

    return ESP_OK;
}

/**
 * @brief 清除校准数据
 */
esp_err_t touch_driver_clear_calibration(void)
{
    touch_calibration_t cal = {0};
    return touch_driver_set_calibration(&cal);
}
