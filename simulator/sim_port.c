#include "sim_port.h"

#include <stdarg.h>
#include <stdio.h>

#include "game_ui_port.h"

static const char *s_score_path = "simulator/.snake_score";
static game_ui_port_key_cb_t s_key_cb;
static void *s_key_ctx;
static uint32_t s_tick_ms;

void sim_port_set_score_path(const char *path)
{
    s_score_path = path ? path : "simulator/.snake_score";
}

void sim_port_advance_time(uint32_t ms)
{
    s_tick_ms += ms;
}

void sim_port_inject_key(uint8_t key, ad_keys_event_type_t type)
{
    if (!s_key_cb) {
        return;
    }
    ad_keys_event_t event = {
        .key = key,
        .type = type,
        .voltage_mv = 0,
        .held_ms = 0,
    };
    s_key_cb(&event, s_key_ctx);
}

void game_ui_port_log_i(const char *tag, const char *fmt, ...)
{
    printf("I (%s) ", tag ? tag : "game_ui");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    putchar('\n');
}

int game_ui_port_load_best(int *score)
{
    if (!score) {
        return -1;
    }
    *score = 0;
    FILE *f = fopen(s_score_path, "rb");
    if (!f) {
        return 0;
    }
    int ok = fread(score, sizeof(*score), 1, f) == 1;
    fclose(f);
    return ok ? 0 : -1;
}

int game_ui_port_save_best(int score)
{
    FILE *f = fopen(s_score_path, "wb");
    if (!f) {
        return -1;
    }
    int ok = fwrite(&score, sizeof(score), 1, f) == 1;
    fclose(f);
    return ok ? 0 : -1;
}

esp_err_t game_ui_port_call(game_ui_port_work_cb_t cb, void *user_data)
{
    if (cb) {
        cb(user_data);
    }
    return ESP_OK;
}

void game_ui_port_set_key_callback(game_ui_port_key_cb_t cb, void *user_ctx)
{
    s_key_cb = cb;
    s_key_ctx = user_ctx;
}

esp_err_t game_ui_port_start_task(game_ui_port_task_fn_t task, const char *name,
                                  uint32_t stack_size, uint32_t priority,
                                  void *arg)
{
    (void)task;
    (void)name;
    (void)stack_size;
    (void)priority;
    (void)arg;
    return ESP_OK;
}

uint32_t game_ui_port_tick_ms(void)
{
    return s_tick_ms;
}

void game_ui_port_delay_ms(uint32_t ms)
{
    sim_port_advance_time(ms);
}

bool game_ui_port_touch_calibration_valid(void)
{
    return true;
}

esp_err_t game_ui_port_request_touch_calibration(void)
{
    return ESP_OK;
}

extern const lv_font_t lv_font_cjk_20;
extern const lv_font_t lv_font_cjk_28;

const lv_font_t *game_ui_port_font_body(void)
{
    return &lv_font_cjk_20;
}

const lv_font_t *game_ui_port_font_title(void)
{
    return &lv_font_cjk_28;
}
