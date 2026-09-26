#pragma once

// ESP32-S3 单路电阻分压按键驱动。
// 组件内部只产生数据和事件，不直接访问 LVGL；UI 请在自己的任务上下文中更新。

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AD_KEYS_COUNT 5
#define AD_KEYS_GPIO 1

typedef enum {
    AD_KEYS_EVENT_PRESS = 0,   // 按下稳定后立即触发, 不等抬起
    AD_KEYS_EVENT_LONG,        // 保留枚举值, 驱动不再发送
    AD_KEYS_EVENT_REPEAT,      // 按下后按 repeat_delay_ms 开始, 再按 repeat_ms 连发
    AD_KEYS_EVENT_RELEASE,     // 释放
} ad_keys_event_type_t;

typedef struct {
    uint8_t key;               // 1..5；释放事件为上一次按键
    ad_keys_event_type_t type;
    uint16_t voltage_mv;       // 事件发生时的校准电压
    uint32_t held_ms;          // 已按住时长
} ad_keys_event_t;

typedef void (*ad_keys_event_cb_t)(const ad_keys_event_t *event, void *user_ctx);

typedef struct {
    uint16_t voltage_mv;       // 最近一次中值滤波后的电压
    uint8_t key;               // 当前稳定键号，0 表示无键/未知
    bool pressed;              // 当前是否识别到有效键
    bool calibrating;          // 是否处于标定流程
    uint8_t calibration_index; // 已采集数量，0..5
    bool calibration_valid;    // NVS 中是否有可用标定
} ad_keys_state_t;

// 兼容主应用常用命名：status 与 state 等价。
typedef ad_keys_state_t ad_keys_status_t;

typedef struct {
    int sample_period_ms;      // 默认 5ms
    int median_window;         // 默认 3, 仅支持 3/5/7
    int stable_samples;        // 连续多少次同一窗口才确认, 默认 2
    int debounce_ms;           // 去抖时间, 默认 5ms
    int repeat_delay_ms;       // 按下后多久发出第一次连发, 默认 200ms
    int repeat_ms;             // 连发周期, 默认 50ms
    uint16_t calibration_idle_delta_mv; // 标定/强制检测偏离阈值，默认160mV（安全下限）
    ad_keys_event_cb_t event_cb;
    void *event_user_ctx;
} ad_keys_config_t;

void ad_keys_config_default(ad_keys_config_t *config);

// 初始化 ADC1_CH0(GPIO1)、ADC 校准、NVS，并加载已有标定数据。
esp_err_t ad_keys_init(const ad_keys_config_t *config);
esp_err_t ad_keys_start(void);
esp_err_t ad_keys_stop(void);
esp_err_t ad_keys_deinit(void);

// 读取线程安全快照；可在 LVGL 任务中轮询。
esp_err_t ad_keys_get_state(ad_keys_state_t *state);
uint16_t ad_keys_get_voltage_mv(void);
uint8_t ad_keys_get_key(void);

// 标定控制。当前运行时采样默认关闭, 调用返回 ESP_ERR_NOT_SUPPORTED.
// 后续专用采样页打开 AD_KEYS_ENABLE_RUNTIME_SAMPLING 后再启用.
esp_err_t ad_keys_start_calibration(void);
esp_err_t ad_keys_request_calibration(void);
bool ad_keys_is_calibrating(void);
uint8_t ad_keys_calibration_index(void);
esp_err_t ad_keys_get_status(ad_keys_status_t *status);
void ad_keys_set_event_callback(ad_keys_event_cb_t event_cb, void *user_ctx);
esp_err_t ad_keys_get_calibration_centers(uint16_t centers_mv[AD_KEYS_COUNT]);
esp_err_t ad_keys_get_calibration_windows(uint16_t min_mv[AD_KEYS_COUNT],
                                          uint16_t max_mv[AD_KEYS_COUNT],
                                          uint16_t *idle_mv);
esp_err_t ad_keys_load_calibration(void);
esp_err_t ad_keys_clear_calibration(void);

// 开机强制标定. 运行时采样关闭时立即返回 false, 不阻塞.
bool ad_keys_boot_force_calibration_check(uint32_t window_ms);

// 兼容主应用命名；等价于 ad_keys_boot_force_calibration_check(1500)。
bool ad_keys_force_calibration_window(uint32_t window_ms);

#ifdef __cplusplus
}
#endif
