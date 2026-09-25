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

void game_ui_port_log_i(const char *tag, const char *fmt, ...);

int game_ui_port_load_best(int *score);
int game_ui_port_save_best(int score);

esp_err_t game_ui_port_call(game_ui_port_work_cb_t cb, void *user_data);

void game_ui_port_set_key_callback(game_ui_port_key_cb_t cb, void *user_ctx);

esp_err_t game_ui_port_start_task(game_ui_port_task_fn_t task, const char *name,
                                  uint32_t stack_size, uint32_t priority,
                                  void *arg);
uint32_t game_ui_port_tick_ms(void);
void game_ui_port_delay_ms(uint32_t ms);

bool game_ui_port_touch_calibration_valid(void);
esp_err_t game_ui_port_request_touch_calibration(void);

const lv_font_t *game_ui_port_font_body(void);
const lv_font_t *game_ui_port_font_title(void);

#ifdef __cplusplus
}
#endif
