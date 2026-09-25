#include "game_ui_port.h"

#include <stdarg.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_port.h"
#include "nvs.h"

void game_ui_port_log_i(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    esp_log_writev(ESP_LOG_INFO, tag, fmt, args);
    va_end(args);
    esp_log_write(ESP_LOG_INFO, tag, "\n");
}

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

esp_err_t game_ui_port_call(game_ui_port_work_cb_t cb, void *user_data)
{
    return lvgl_port_call(cb, user_data);
}

void game_ui_port_set_key_callback(game_ui_port_key_cb_t cb, void *user_ctx)
{
    ad_keys_set_event_callback(cb, user_ctx);
}

esp_err_t game_ui_port_start_task(game_ui_port_task_fn_t task, const char *name,
                                  uint32_t stack_size, uint32_t priority,
                                  void *arg)
{
    TaskHandle_t handle = NULL;
    return xTaskCreate(task, name, stack_size, arg, priority, &handle) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

uint32_t game_ui_port_tick_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void game_ui_port_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

bool game_ui_port_touch_calibration_valid(void)
{
    return lvgl_port_touch_calibration_valid();
}

esp_err_t game_ui_port_request_touch_calibration(void)
{
    return lvgl_port_request_touch_calibration();
}

const lv_font_t *game_ui_port_font_body(void)
{
    return &lv_font_cjk_20;
}

const lv_font_t *game_ui_port_font_title(void)
{
    return &lv_font_cjk_28;
}
