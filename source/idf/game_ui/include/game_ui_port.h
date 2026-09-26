#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ad_keys.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*game_ui_port_work_cb_t)(void *user_data);
typedef void (*game_ui_port_key_cb_t)(const ad_keys_event_t *event,
                                      void *user_ctx);
typedef void (*game_ui_port_task_fn_t)(void *arg);

/**
 * @brief 输出一条 INFO 日志, 设备和仿真器各自落到自己的后端.
 *
 * @param tag 日志模块名, 不能为空.
 * @param fmt printf 格式串.
 * @return 无.
 */
void game_ui_port_log_i(const char *tag, const char *fmt, ...);

/**
 * @brief 读取持久化最高分.
 *
 * @param score 输出槽, 为空返回 -1; 无记录时写成 0.
 * @return 0 成功或尚无记录; -1 失败.
 */
int game_ui_port_load_best(int *score);

/**
 * @brief 写入持久化最高分.
 *
 * @param score 要保存的分数.
 * @return 0 成功; -1 失败.
 */
int game_ui_port_save_best(int score);

/**
 * @brief 把工作切到 LVGL 线程执行.
 *
 * @param cb 要执行的函数, 不能为空.
 * @param user_data 传给 cb 的上下文.
 * @return ESP_OK 或底层调度错误.
 */
esp_err_t game_ui_port_call(game_ui_port_work_cb_t cb, void *user_data);

/**
 * @brief 注册实体键事件回调.
 *
 * @param cb 按键回调, 为空表示取消.
 * @param user_ctx 回传给 cb 的上下文.
 * @return 无.
 */
void game_ui_port_set_key_callback(game_ui_port_key_cb_t cb, void *user_ctx);

/**
 * @brief 启动游戏逻辑任务. 设备端钉在核 0.
 *
 * @param task 任务函数, 不能为空.
 * @param name 任务名, 不能为空.
 * @param stack_size 栈字数, 不能为 0.
 * @param priority FreeRTOS 优先级.
 * @param arg 传给任务的参数.
 * @return ESP_OK 成功; 参数非法或内存不足时失败.
 */
esp_err_t game_ui_port_start_task(game_ui_port_task_fn_t task, const char *name,
                                  uint32_t stack_size, uint32_t priority,
                                  void *arg);

/**
 * @brief 取单调毫秒时间.
 *
 * @return 启动后经过的毫秒数.
 */
uint32_t game_ui_port_tick_ms(void);

/**
 * @brief 阻塞延时.
 *
 * @param ms 毫秒数.
 * @return 无.
 */
void game_ui_port_delay_ms(uint32_t ms);

/**
 * @brief 查询触摸校准数据是否有效.
 *
 * @return 已校准且可用为 true.
 */
bool game_ui_port_touch_calibration_valid(void);

/**
 * @brief 请求进入触摸校准流程.
 *
 * @return ESP_OK 或底层错误.
 */
esp_err_t game_ui_port_request_touch_calibration(void);

/**
 * @brief 取平台正文字体.
 *
 * @return 静态字体指针.
 */
const lv_font_t *game_ui_port_font_body(void);

/**
 * @brief 取平台标题字体.
 *
 * @return 静态字体指针.
 */
const lv_font_t *game_ui_port_font_title(void);

#ifdef __cplusplus
}
#endif
