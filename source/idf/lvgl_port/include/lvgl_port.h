/**
 * @file lvgl_port.h
 * @brief LVGL platform porting layer for ESP32-S3 + ILI9488 + XPT2046
 *
 * Bridges LVGL to the existing LCD35 driver (lcd_driver + touch_driver)
 */

#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*lvgl_port_work_cb_t)(void *user_data);

/**
 * @brief Initialize LVGL with display and touch drivers
 *
 * - Creates LVGL display (480x320 RGB565) backed by lcd_driver
 * - Registers XPT2046 touch as LVGL input device
 * - Starts LVGL tick timer and task handler loop
 *
 * @return ESP_OK on success
 */
esp_err_t lvgl_port_init(void);

/**
 * @brief Get the active LVGL display object
 * @return Pointer to lv_display_t, or NULL if not initialized
 */
void *lvgl_port_get_display(void);

/**
 * @brief Get the active LVGL input device
 * @return Pointer to lv_indev_t, or NULL if not initialized
 */
void *lvgl_port_get_indev(void);

/**
 * @brief Run a callback synchronously in the LVGL handler task context.
 *
 * LVGL is not configured with an OS lock in this project, so code outside the
 * LVGL task must use this helper before touching LVGL objects, displays, or
 * draw buffers.
 *
 * @param cb Callback executed by the LVGL handler task
 * @param user_data Opaque callback argument
 * @return ESP_OK when the callback has completed
 */
esp_err_t lvgl_port_call(lvgl_port_work_cb_t cb, void *user_data);

/**
 * @brief Request the touch calibration overlay.
 *
 * Can be called from the LVGL task or another task. The calibration UI is
 * executed in the LVGL handler task and persists the result to NVS.
 *
 * @return ESP_OK if the calibration flow was started
 */
esp_err_t lvgl_port_request_touch_calibration(void);

/* 返回是否已有当前横屏尺寸对应的触摸校准数据。 */
bool lvgl_port_touch_calibration_valid(void);

/**
 * @brief Request the LVGL handler task to create the main screen
 *
 * Schedules game_home_create() to run in the LVGL handler
 * task context. Returns when screen creation is complete.
 *
 * @return ESP_OK on success, ESP_FAIL on timeout or error
 */
esp_err_t lvgl_port_deferred_create_main_screen(void);

/**
 * @brief Custom 16px CJK font (SimHei/思源黑体 subset)
 *
 * Contains GB2312 level-1 Chinese characters + ASCII for embedded UI.
 * Generated from SimHei (can be replaced with Source Han Sans / WenQuanYi).
 */
extern const lv_font_t lv_font_cjk_16;
extern const lv_font_t lv_font_cjk_20;
extern const lv_font_t lv_font_cjk_28;

#define FONT_CJK (&lv_font_cjk_16)

/**
 * @brief 创建最小首页。
 */
void game_home_create(void);

/**
 * @brief 兼容旧调用方的空更新接口。
 */
void game_home_update_data(const void *data);

#endif /* LVGL_PORT_H */
