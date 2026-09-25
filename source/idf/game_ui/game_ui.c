#include "game_ui.h"

#include <stdio.h>
#include <string.h>

#include "ad_keys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_port.h"

#define GAME_UI_EVENT_QUEUE_LEN 16
#define GAME_UI_HISTORY_LEN 10

typedef struct {
    ad_keys_event_t event;
} game_ui_event_item_t;

typedef struct {
    ad_keys_state_t state;
    uint16_t calibration_centers_mv[AD_KEYS_COUNT];
    ad_keys_event_t history[GAME_UI_HISTORY_LEN];
    uint8_t history_count;
} game_ui_update_t;

static QueueHandle_t s_event_queue;
static TaskHandle_t s_task;
static lv_obj_t *s_key_buttons[AD_KEYS_COUNT];
static lv_obj_t *s_voltage_label;
static lv_obj_t *s_state_label;
static lv_obj_t *s_calibration_label;
static lv_obj_t *s_calibration_values_label;
static lv_obj_t *s_event_list;
static bool s_calibration_requested;

static void game_ui_recalibrate_event(lv_event_t *event);

static const char *event_name(ad_keys_event_type_t type)
{
    switch (type) {
        case AD_KEYS_EVENT_PRESS: return "按下";
        case AD_KEYS_EVENT_LONG: return "长按";
        case AD_KEYS_EVENT_REPEAT: return "连发";
        case AD_KEYS_EVENT_RELEASE: return "释放";
        default: return "未知";
    }
}

static void game_ui_update_lvgl(void *user_data)
{
    const game_ui_update_t *update = (const game_ui_update_t *)user_data;
    if (!update || !s_voltage_label || !s_state_label) {
        return;
    }

    lv_label_set_text_fmt(s_voltage_label, "ADC: %u mV", update->state.voltage_mv);
    if (update->state.calibrating) {
        lv_label_set_text_fmt(s_state_label, "标定中：请按 K%u…K5",
                              (unsigned)(update->state.calibration_index + 1U));
        lv_label_set_text_fmt(s_calibration_label, "标定进度 %u/5",
                              (unsigned)update->state.calibration_index);
    } else if (!update->state.calibration_valid) {
        lv_label_set_text(s_state_label, "等待标定");
        lv_label_set_text(s_calibration_label, "未找到标定数据");
    } else {
        if (update->state.pressed) {
            lv_label_set_text_fmt(s_state_label, "当前按键：K%u",
                                  (unsigned)update->state.key);
        } else {
            lv_label_set_text(s_state_label, "当前按键：无");
        }
        lv_label_set_text(s_calibration_label, "标定完成，可重新标定");
    }
    char calibration_text[128];
    snprintf(calibration_text, sizeof(calibration_text),
             "K1: %4umV   K2: %4umV   K3: %4umV\n"
             "K4: %4umV   K5: %4umV",
             (unsigned)update->calibration_centers_mv[0],
             (unsigned)update->calibration_centers_mv[1],
             (unsigned)update->calibration_centers_mv[2],
             (unsigned)update->calibration_centers_mv[3],
             (unsigned)update->calibration_centers_mv[4]);
    lv_label_set_text(s_calibration_values_label, calibration_text);

    for (uint8_t i = 0; i < AD_KEYS_COUNT; ++i) {
        bool active = update->state.key == (uint8_t)(i + 1U);
        lv_obj_set_style_bg_color(s_key_buttons[i],
                                  active ? lv_color_hex(0x2E8B57) : lv_color_hex(0x263238),
                                  LV_PART_MAIN);
        lv_obj_set_style_border_color(s_key_buttons[i],
                                      active ? lv_color_hex(0xA5D6A7) : lv_color_hex(0x607D8B),
                                      LV_PART_MAIN);
    }

    lv_obj_clean(s_event_list);
    for (uint8_t i = 0; i < update->history_count; ++i) {
        const ad_keys_event_t *item = &update->history[i];
        lv_obj_t *label = lv_label_create(s_event_list);
        lv_label_set_text_fmt(label, "K%u %s %umV %ums",
                              (unsigned)item->key, event_name(item->type),
                              (unsigned)item->voltage_mv, (unsigned)item->held_ms);
        lv_obj_set_style_text_font(label, FONT_CJK, LV_PART_MAIN);
        lv_obj_set_width(label, lv_pct(100));
    }
}

static void game_ui_create(void *user_data)
{
    (void)user_data;
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(screen, 6, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "ESP32-S3 按键测试");
    lv_obj_set_style_text_font(title, FONT_CJK, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    lv_obj_t *row = lv_obj_create(screen);
    lv_obj_set_size(row, lv_pct(100), 54);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);

    for (uint8_t i = 0; i < AD_KEYS_COUNT; ++i) {
        lv_obj_t *button = lv_btn_create(row);
        s_key_buttons[i] = button;
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_height(button, 44);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x263238), LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, lv_color_hex(0x607D8B), LV_PART_MAIN);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text_fmt(label, "K%u", (unsigned)(i + 1U));
        lv_obj_set_style_text_font(label, FONT_CJK, LV_PART_MAIN);
        lv_obj_center(label);
    }

    s_voltage_label = lv_label_create(screen);
    lv_label_set_text(s_voltage_label, "ADC: 0 mV");
    lv_obj_set_style_text_font(s_voltage_label, FONT_CJK, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_voltage_label, lv_color_hex(0x80CBC4), LV_PART_MAIN);

    s_state_label = lv_label_create(screen);
    lv_label_set_text(s_state_label, "等待按键");
    lv_obj_set_style_text_font(s_state_label, FONT_CJK, LV_PART_MAIN);

    lv_obj_t *cal_row = lv_obj_create(screen);
    lv_obj_set_size(cal_row, lv_pct(100), 38);
    lv_obj_set_style_bg_opa(cal_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cal_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cal_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cal_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cal_row, 8, LV_PART_MAIN);

    s_calibration_label = lv_label_create(cal_row);
    lv_label_set_text(s_calibration_label, "标定状态");
    lv_obj_set_style_text_font(s_calibration_label, FONT_CJK, LV_PART_MAIN);
    lv_obj_set_flex_grow(s_calibration_label, 1);

    lv_obj_t *cal_button = lv_btn_create(cal_row);
    lv_obj_set_width(cal_button, 120);
    lv_obj_add_event_cb(cal_button, game_ui_recalibrate_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cal_button_label = lv_label_create(cal_button);
    lv_label_set_text(cal_button_label, "重新标定");
    lv_obj_set_style_text_font(cal_button_label, FONT_CJK, LV_PART_MAIN);
    lv_obj_center(cal_button_label);

    s_calibration_values_label = lv_label_create(screen);
    lv_label_set_text(s_calibration_values_label,
                      "K1:    0mV   K2:    0mV   K3:    0mV\n"
                      "K4:    0mV   K5:    0mV");
    lv_obj_set_style_text_font(s_calibration_values_label, FONT_CJK, LV_PART_MAIN);

    lv_obj_t *event_title = lv_label_create(screen);
    lv_label_set_text(event_title, "最近事件");
    lv_obj_set_style_text_font(event_title, FONT_CJK, LV_PART_MAIN);

    s_event_list = lv_obj_create(screen);
    lv_obj_set_width(s_event_list, lv_pct(100));
    lv_obj_set_flex_grow(s_event_list, 1);
    lv_obj_set_style_bg_color(s_event_list, lv_color_hex(0x17232B), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_event_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_event_list, lv_color_hex(0x37474F), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_event_list, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_event_list, LV_FLEX_FLOW_COLUMN);

    lv_screen_load(screen);

    /* 首次启动时先完成触摸校准，避免触摸层和 AD 标定页争用。 */
    if (!lvgl_port_touch_calibration_valid() && !ad_keys_is_calibrating()) {
        (void)lvgl_port_request_touch_calibration();
    }
}

static void game_ui_recalibrate_event(lv_event_t *event)
{
    (void)event;
    s_calibration_requested = false;
    (void)ad_keys_request_calibration();
}

static void game_ui_event_callback(const ad_keys_event_t *event, void *user_ctx)
{
    (void)user_ctx;
    if (!event || !s_event_queue) {
        return;
    }
    game_ui_event_item_t item = {.event = *event};
    (void)xQueueSend(s_event_queue, &item, 0);
}

static void game_ui_task(void *arg)
{
    (void)arg;
    ad_keys_event_t history[GAME_UI_HISTORY_LEN] = {0};
    uint8_t history_count = 0;

    for (;;) {
        ad_keys_state_t state = {0};
        if (ad_keys_get_state(&state) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (!lvgl_port_touch_calibration_valid() && !state.calibrating &&
            !s_calibration_requested) {
            (void)lvgl_port_request_touch_calibration();
            s_calibration_requested = true;
        }
        if (lvgl_port_touch_calibration_valid() &&
            !state.calibration_valid && !state.calibrating && !s_calibration_requested) {
            if (ad_keys_request_calibration() == ESP_OK) {
                s_calibration_requested = true;
            }
        }
        if (state.calibration_valid && !state.calibrating) {
            s_calibration_requested = false;
        }

        game_ui_event_item_t item;
        while (xQueueReceive(s_event_queue, &item, 0) == pdTRUE) {
            if (history_count < GAME_UI_HISTORY_LEN) {
                history[history_count++] = item.event;
            } else {
                memmove(&history[0], &history[1],
                        (GAME_UI_HISTORY_LEN - 1U) * sizeof(history[0]));
                history[GAME_UI_HISTORY_LEN - 1U] = item.event;
            }
        }

        game_ui_update_t update = {
            .state = state,
            .history_count = history_count,
        };
        (void)ad_keys_get_calibration_centers(update.calibration_centers_mv);
        memcpy(update.history, history, sizeof(history));
        (void)lvgl_port_call(game_ui_update_lvgl, &update);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t game_ui_init(void)
{
    if (s_task) {
        return ESP_OK;
    }
    s_event_queue = xQueueCreate(GAME_UI_EVENT_QUEUE_LEN, sizeof(game_ui_event_item_t));
    if (!s_event_queue) {
        return ESP_ERR_NO_MEM;
    }
    ad_keys_set_event_callback(game_ui_event_callback, NULL);
    esp_err_t err = lvgl_port_call(game_ui_create, NULL);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(game_ui_task, "game_ui", 6144, NULL, 4, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
