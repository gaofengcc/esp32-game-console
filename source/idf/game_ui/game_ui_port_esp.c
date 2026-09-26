#include "game_ui_port.h"

#include <stdarg.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_port.h"
#include "nvs.h"

#define GAME_UI_PORT_TAG "game_ui"
/* 游戏逻辑钉在核 0, LVGL 刷新留在核 1. */
#define GAME_UI_LOGIC_CORE 0

/**
 * @brief 输出一条 INFO 日志.
 *
 * @param tag 日志模块名.
 * @param fmt printf 格式串.
 * @return 无.
 */
void game_ui_port_log_i(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    esp_log_writev(ESP_LOG_INFO, tag, fmt, args);
    va_end(args);
    esp_log_write(ESP_LOG_INFO, tag, "\n");
}

/**
 * @brief 从 NVS namespace game/key high_score 读取最高分.
 *
 * @param score 输出槽, 为空返回 -1; 无记录时写成 0.
 * @return 0 成功或尚无记录; -1 失败.
 */
int game_ui_port_load_best(int *score)
{
    if (!score) {
        return -1;
    }
    *score = 0;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("game", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }
    if (err != ESP_OK) {
        return -1;
    }
    uint32_t value = 0;
    err = nvs_get_u32(handle, "high_score", &value);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }
    if (err != ESP_OK) {
        return -1;
    }
    *score = (int)value;
    return 0;
}

/**
 * @brief 把最高分写进 NVS.
 *
 * @param score 要保存的分数.
 * @return 0 成功; -1 失败.
 */
int game_ui_port_save_best(int score)
{
    nvs_handle_t handle;
    if (nvs_open("game", NVS_READWRITE, &handle) != ESP_OK) {
        return -1;
    }
    esp_err_t err = nvs_set_u32(handle, "high_score", (uint32_t)score);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK ? 0 : -1;
}

/**
 * @brief 把工作切到 LVGL 线程执行.
 *
 * @param cb 要执行的函数.
 * @param user_data 传给 cb 的上下文.
 * @return ESP_OK 或底层调度错误.
 */
esp_err_t game_ui_port_call(game_ui_port_work_cb_t cb, void *user_data)
{
    return lvgl_port_call(cb, user_data);
}

/**
 * @brief 注册实体键事件回调.
 *
 * @param cb 按键回调, 为空表示取消.
 * @param user_ctx 回传给 cb 的上下文.
 * @return 无.
 */
void game_ui_port_set_key_callback(game_ui_port_key_cb_t cb, void *user_ctx)
{
    ad_keys_set_event_callback(cb, user_ctx);
}

/**
 * @brief 在核 0 启动游戏逻辑任务, 不和 LVGL 刷新抢同一核.
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
                                  void *arg)
{
    TaskHandle_t handle = NULL;

    if (!task || !name || stack_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xTaskCreatePinnedToCore(task, name, stack_size, arg, priority,
                                &handle, GAME_UI_LOGIC_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(GAME_UI_PORT_TAG, "逻辑任务 %s 已钉到核 %d, 优先级 %lu",
             name, GAME_UI_LOGIC_CORE, (unsigned long)priority);
    return ESP_OK;
}

/**
 * @brief 取单调毫秒时间.
 *
 * @return 启动后经过的毫秒数.
 */
uint32_t game_ui_port_tick_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * @brief 阻塞延时.
 *
 * @param ms 毫秒数.
 * @return 无.
 */
void game_ui_port_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * @brief 查询触摸校准数据是否有效.
 *
 * @return 已校准且可用为 true.
 */
bool game_ui_port_touch_calibration_valid(void)
{
    return lvgl_port_touch_calibration_valid();
}

/**
 * @brief 请求进入触摸校准流程.
 *
 * @return ESP_OK 或底层错误.
 */
esp_err_t game_ui_port_request_touch_calibration(void)
{
    return lvgl_port_request_touch_calibration();
}

/**
 * @brief 取设备正文字体.
 *
 * @return 静态 20px CJK 字体指针.
 */
const lv_font_t *game_ui_port_font_body(void)
{
    return &lv_font_cjk_20;
}

/**
 * @brief 取设备标题字体.
 *
 * @return 静态 28px CJK 字体指针.
 */
const lv_font_t *game_ui_port_font_title(void)
{
    return &lv_font_cjk_28;
}
