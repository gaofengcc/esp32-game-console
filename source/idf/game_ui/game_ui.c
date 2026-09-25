#include "game_ui.h"

#include <string.h>

#include "ad_keys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "nvs.h"
#include "snake_logic.h"

#define GAME_UI_KEY_QUEUE_LEN 24
#define GAME_UI_TICK_MS 20U
#define GAME_UI_CELL_PX 16U
#define GAME_UI_BOARD_CELLS (SNAKE_BOARD_WIDTH * SNAKE_BOARD_HEIGHT)

/* 实体键映射可通过编译选项覆盖。默认 K1 上、K2 下、K3 左、K4 右、K5 暂停。 */
#ifndef GAME_UI_KEY_UP
#define GAME_UI_KEY_UP 1U
#endif
#ifndef GAME_UI_KEY_DOWN
#define GAME_UI_KEY_DOWN 2U
#endif
#ifndef GAME_UI_KEY_LEFT
#define GAME_UI_KEY_LEFT 3U
#endif
#ifndef GAME_UI_KEY_RIGHT
#define GAME_UI_KEY_RIGHT 4U
#endif
#ifndef GAME_UI_KEY_PAUSE
#define GAME_UI_KEY_PAUSE 5U
#endif

#define COLOR_BG 0x0F1720
#define COLOR_PANEL 0x17232B
#define COLOR_TEXT 0xE8F1F2
#define COLOR_ACCENT 0x4DD0E1
#define COLOR_SNAKE_HEAD 0x66BB6A
#define COLOR_SNAKE_BODY 0x2E8B57
#define COLOR_FOOD 0xEF5350
#define COLOR_GRID 0x1D3038
#define COLOR_BUTTON 0x263238

typedef enum {
    GAME_UI_PAGE_MENU = 0,
    GAME_UI_PAGE_GAME,
    GAME_UI_PAGE_END,
} game_ui_page_t;

typedef struct {
    uint8_t key;
    ad_keys_event_type_t type;
} game_ui_key_event_t;

static const char *TAG = "game_ui";
static QueueHandle_t s_key_queue;
static TaskHandle_t s_task;
static snake_game_t s_game;
static game_ui_page_t s_page;
static bool s_touch_calibration_requested;

/* 所有 LVGL 对象只在 lvgl_port_call 或 LVGL 按钮回调中访问。 */
static lv_obj_t *s_screen;
static lv_obj_t *s_menu_title;
static lv_obj_t *s_menu_speed;
static lv_obj_t *s_menu_wrap;
static lv_obj_t *s_menu_high_score;
static lv_obj_t *s_board;
static lv_obj_t *s_cells[GAME_UI_BOARD_CELLS];
static uint8_t s_cell_state[GAME_UI_BOARD_CELLS];
static lv_obj_t *s_score_label;
static lv_obj_t *s_pause_label;
static lv_obj_t *s_end_screen;
static lv_obj_t *s_end_score;
static lv_obj_t *s_end_high_score;

static bool s_wrap_enabled = true;
static uint8_t s_speed_level = 0;

static int game_ui_nvs_load(void *ctx, int *score)
{
    (void)ctx;
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

static int game_ui_nvs_save(void *ctx, int score)
{
    (void)ctx;
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

static snake_input_t game_ui_input_from_key(uint8_t key)
{
    switch (key) {
        case GAME_UI_KEY_UP: return SNAKE_INPUT_UP;
        case GAME_UI_KEY_DOWN: return SNAKE_INPUT_DOWN;
        case GAME_UI_KEY_LEFT: return SNAKE_INPUT_LEFT;
        case GAME_UI_KEY_RIGHT: return SNAKE_INPUT_RIGHT;
        case GAME_UI_KEY_PAUSE: return SNAKE_INPUT_PAUSE;
        default: return SNAKE_INPUT_NONE;
    }
}

static void game_ui_set_screen_style(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
}

static lv_obj_t *game_ui_make_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FONT_CJK, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

static lv_obj_t *game_ui_make_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_BUTTON), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
    lv_obj_t *label = game_ui_make_label(button, text, COLOR_TEXT);
    lv_obj_center(label);
    return button;
}

static const char *game_ui_speed_name(void)
{
    switch (s_speed_level) {
        case 1: return "中";
        case 2: return "快";
        default: return "慢";
    }
}

static void game_ui_render_menu(void *user_data);
static void game_ui_render_game(void *user_data);
static void game_ui_render_end(void *user_data);

static void game_ui_start_clicked(lv_event_t *event)
{
    (void)event;
    snake_game_set_wrap(&s_game, s_wrap_enabled);
    snake_game_set_speed_level(&s_game, s_speed_level);
    snake_game_reset(&s_game);
    s_page = GAME_UI_PAGE_GAME;
    game_ui_render_game(NULL);
}

static void game_ui_speed_clicked(lv_event_t *event)
{
    (void)event;
    s_speed_level = (uint8_t)((s_speed_level + 1) % 3);
    snake_game_set_speed_level(&s_game, s_speed_level);
    lv_label_set_text_fmt(s_menu_speed, "速度：%s", game_ui_speed_name());
}

static void game_ui_wrap_clicked(lv_event_t *event)
{
    (void)event;
    s_wrap_enabled = !s_wrap_enabled;
    snake_game_set_wrap(&s_game, s_wrap_enabled);
    lv_label_set_text_fmt(s_menu_wrap, "穿墙：%s", s_wrap_enabled ? "开" : "关");
}

static void game_ui_retry_clicked(lv_event_t *event)
{
    (void)event;
    snake_game_set_wrap(&s_game, s_wrap_enabled);
    snake_game_set_speed_level(&s_game, s_speed_level);
    snake_game_reset(&s_game);
    s_page = GAME_UI_PAGE_GAME;
    game_ui_render_game(NULL);
}

static void game_ui_back_clicked(lv_event_t *event)
{
    (void)event;
    s_page = GAME_UI_PAGE_MENU;
    game_ui_render_menu(NULL);
}

static void game_ui_create_menu(void)
{
    s_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_screen);
    lv_obj_set_style_pad_all(s_screen, 22, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_screen, 12, LV_PART_MAIN);

    s_menu_title = game_ui_make_label(s_screen, "贪吃蛇", COLOR_TEXT);
    lv_obj_set_style_text_color(s_menu_title, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_menu_title, FONT_CJK, LV_PART_MAIN);
    lv_obj_set_width(s_menu_title, lv_pct(100));
    lv_obj_set_style_text_align(s_menu_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *start = game_ui_make_button(s_screen, "开始游戏");
    lv_obj_set_width(start, lv_pct(100));
    lv_obj_set_height(start, 44);
    lv_obj_add_event_cb(start, game_ui_start_clicked, LV_EVENT_CLICKED, NULL);

    s_menu_speed = game_ui_make_button(s_screen, "速度：慢");
    lv_obj_set_width(s_menu_speed, lv_pct(100));
    lv_obj_set_height(s_menu_speed, 40);
    lv_obj_add_event_cb(s_menu_speed, game_ui_speed_clicked, LV_EVENT_CLICKED, NULL);

    s_menu_wrap = game_ui_make_button(s_screen, "穿墙：开");
    lv_obj_set_width(s_menu_wrap, lv_pct(100));
    lv_obj_set_height(s_menu_wrap, 40);
    lv_obj_add_event_cb(s_menu_wrap, game_ui_wrap_clicked, LV_EVENT_CLICKED, NULL);

    s_menu_high_score = game_ui_make_label(s_screen, "最高分：0", COLOR_TEXT);
    lv_obj_set_width(s_menu_high_score, lv_pct(100));
    lv_obj_set_style_text_align(s_menu_high_score, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

static void game_ui_create_board(void)
{
    s_board = lv_obj_create(NULL);
    game_ui_set_screen_style(s_board);
    lv_obj_set_size(s_board, SNAKE_BOARD_WIDTH * GAME_UI_CELL_PX,
                    SNAKE_BOARD_HEIGHT * GAME_UI_CELL_PX);
    lv_obj_set_style_border_width(s_board, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_board, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_board, 0, LV_PART_MAIN);
    for (uint16_t y = 0; y < SNAKE_BOARD_HEIGHT; ++y) {
        for (uint16_t x = 0; x < SNAKE_BOARD_WIDTH; ++x) {
            uint16_t index = (uint16_t)(y * SNAKE_BOARD_WIDTH + x);
            lv_obj_t *cell = lv_obj_create(s_board);
            s_cells[index] = cell;
            lv_obj_set_pos(cell, x * GAME_UI_CELL_PX, y * GAME_UI_CELL_PX);
            lv_obj_set_size(cell, GAME_UI_CELL_PX, GAME_UI_CELL_PX);
            lv_obj_set_style_radius(cell, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(cell, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(cell, lv_color_hex(COLOR_GRID), LV_PART_MAIN);
            lv_obj_set_style_bg_color(cell, lv_color_hex(COLOR_BG), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_PART_MAIN);
            s_cell_state[index] = 0;
        }
    }
}

static void game_ui_render_menu(void *user_data)
{
    (void)user_data;
    if (!s_screen) {
        game_ui_create_menu();
    }
    lv_label_set_text_fmt(s_menu_speed, "速度：%s", game_ui_speed_name());
    lv_label_set_text_fmt(s_menu_wrap, "穿墙：%s", s_wrap_enabled ? "开" : "关");
    const snake_state_t *state = snake_game_state(&s_game);
    lv_label_set_text_fmt(s_menu_high_score, "最高分：%d", state ? state->best_score : 0);
    lv_screen_load(s_screen);
}

static uint8_t game_ui_cell_state(uint16_t x, uint16_t y,
                                  const snake_point_t *segments, uint16_t length,
                                  snake_point_t food)
{
    if (food.x == x && food.y == y) {
        return 3;
    }
    for (uint16_t i = 0; i < length; ++i) {
        if (segments[i].x == x && segments[i].y == y) {
            return i == 0 ? 2 : 1;
        }
    }
    return 0;
}

static void game_ui_update_board(void)
{
    uint16_t length = 0;
    const snake_state_t *state = snake_game_state(&s_game);
    if (!state) {
        return;
    }
    const snake_point_t *segments = state->segments;
    length = state->length;
    snake_point_t food = state->food;
    for (uint16_t y = 0; y < SNAKE_BOARD_HEIGHT; ++y) {
        for (uint16_t x = 0; x < SNAKE_BOARD_WIDTH; ++x) {
            uint16_t index = (uint16_t)(y * SNAKE_BOARD_WIDTH + x);
            uint8_t state = game_ui_cell_state(x, y, segments, length, food);
            if (state == s_cell_state[index]) {
                continue;
            }
            s_cell_state[index] = state;
            uint32_t color = COLOR_BG;
            if (state == 1) color = COLOR_SNAKE_BODY;
            if (state == 2) color = COLOR_SNAKE_HEAD;
            if (state == 3) color = COLOR_FOOD;
            lv_obj_set_style_bg_color(s_cells[index], lv_color_hex(color), LV_PART_MAIN);
        }
    }
}

static void game_ui_render_game(void *user_data)
{
    (void)user_data;
    if (!s_board) {
        game_ui_create_board();
        s_score_label = game_ui_make_label(s_board, "得分：0", COLOR_TEXT);
        lv_obj_set_pos(s_score_label, 6, 2);
        lv_obj_set_style_bg_color(s_score_label, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_score_label, LV_OPA_90, LV_PART_MAIN);
        s_pause_label = game_ui_make_label(s_board, "", COLOR_ACCENT);
        lv_obj_center(s_pause_label);
    }
    memset(s_cell_state, 0xFF, sizeof(s_cell_state));
    game_ui_update_board();
    const snake_state_t *state = snake_game_state(&s_game);
    lv_label_set_text_fmt(s_score_label, "得分：%d  速度：%ums",
                          state ? state->score : 0,
                          state ? (unsigned)state->speed_ms : 0U);
    if (state && state->paused) {
        lv_label_set_text(s_pause_label, "已暂停  K5继续");
    } else {
        lv_label_set_text(s_pause_label, "");
    }
    lv_screen_load(s_board);
}

static void game_ui_render_end(void *user_data)
{
    (void)user_data;
    const snake_state_t *state = snake_game_state(&s_game);
    if (!s_screen) {
        game_ui_create_menu();
    }
    if (!s_end_screen) {
        s_end_screen = lv_obj_create(NULL);
        game_ui_set_screen_style(s_end_screen);
        lv_obj_set_style_pad_all(s_end_screen, 22, LV_PART_MAIN);
        lv_obj_set_flex_flow(s_end_screen, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(s_end_screen, 12, LV_PART_MAIN);
        lv_obj_t *title = game_ui_make_label(s_end_screen, "游戏结束", COLOR_FOOD);
        lv_obj_set_width(title, lv_pct(100));
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        s_end_score = game_ui_make_label(s_end_screen, "本局得分：0", COLOR_TEXT);
        lv_obj_set_width(s_end_score, lv_pct(100));
        lv_obj_set_style_text_align(s_end_score, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        s_end_high_score = game_ui_make_label(s_end_screen, "最高分：0", COLOR_ACCENT);
        lv_obj_set_width(s_end_high_score, lv_pct(100));
        lv_obj_set_style_text_align(s_end_high_score, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_t *retry = game_ui_make_button(s_end_screen, "再来一次");
        lv_obj_set_width(retry, lv_pct(100));
        lv_obj_set_height(retry, 44);
        lv_obj_add_event_cb(retry, game_ui_retry_clicked, LV_EVENT_CLICKED, NULL);
        lv_obj_t *back = game_ui_make_button(s_end_screen, "返回首页");
        lv_obj_set_width(back, lv_pct(100));
        lv_obj_set_height(back, 40);
        lv_obj_add_event_cb(back, game_ui_back_clicked, LV_EVENT_CLICKED, NULL);
    }
    lv_label_set_text_fmt(s_end_score, "本局得分：%d",
                          state ? state->score : 0);
    lv_label_set_text_fmt(s_end_high_score, "最高分：%d",
                          state ? state->best_score : 0);
    lv_screen_load(s_end_screen);
}

static void game_ui_update_lvgl(void *user_data)
{
    (void)user_data;
    snake_status_t status = snake_game_get_status(&s_game);
    if (status == SNAKE_STATUS_GAME_OVER && s_page != GAME_UI_PAGE_END) {
        s_page = GAME_UI_PAGE_END;
        game_ui_render_end(NULL);
    } else if ((status == SNAKE_STATUS_RUNNING || status == SNAKE_STATUS_PAUSED) &&
               s_page == GAME_UI_PAGE_GAME) {
        game_ui_render_game(NULL);
    }
}

static void game_ui_event_callback(const ad_keys_event_t *event, void *user_ctx)
{
    (void)user_ctx;
    if (!event || !s_key_queue) {
        return;
    }
    if (event->type != AD_KEYS_EVENT_PRESS && event->type != AD_KEYS_EVENT_REPEAT) {
        return;
    }
    game_ui_key_event_t item = {.key = event->key, .type = event->type};
    (void)xQueueSend(s_key_queue, &item, 0);
}

static void game_ui_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        game_ui_key_event_t item;
        while (xQueueReceive(s_key_queue, &item, 0) == pdTRUE) {
            if (s_page == GAME_UI_PAGE_GAME) {
                snake_game_set_input(&s_game, game_ui_input_from_key(item.key));
            }
        }
        TickType_t now = xTaskGetTickCount();
        uint32_t elapsed = (uint32_t)((now - last) * portTICK_PERIOD_MS);
        last = now;
        if (s_page == GAME_UI_PAGE_GAME) {
            snake_game_advance(&s_game, elapsed);
            (void)lvgl_port_call(game_ui_update_lvgl, NULL);
        }
        if (!lvgl_port_touch_calibration_valid() && !s_touch_calibration_requested) {
            (void)lvgl_port_request_touch_calibration();
            s_touch_calibration_requested = true;
        }
        vTaskDelay(pdMS_TO_TICKS(GAME_UI_TICK_MS));
    }
}

static void game_ui_create_initial(void *user_data)
{
    (void)user_data;
    s_page = GAME_UI_PAGE_MENU;
    game_ui_render_menu(NULL);
}

esp_err_t game_ui_init(void)
{
    if (s_task) {
        return ESP_OK;
    }
    snake_config_t config;
    snake_config_default(&config);
    config.load_best = game_ui_nvs_load;
    config.save_best = game_ui_nvs_save;
    config.storage_ctx = NULL;
    config.wrap_walls = true;
    config.initial_speed_ms = SNAKE_SPEED_SLOW_MS;
    config.width = SNAKE_BOARD_WIDTH;
    config.height = SNAKE_BOARD_HEIGHT;
    snake_game_init(&s_game, &config);
    s_key_queue = xQueueCreate(GAME_UI_KEY_QUEUE_LEN, sizeof(game_ui_key_event_t));
    if (!s_key_queue) {
        return ESP_ERR_NO_MEM;
    }
    ad_keys_set_event_callback(game_ui_event_callback, NULL);
    esp_err_t err = lvgl_port_call(game_ui_create_initial, NULL);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(game_ui_task, "game_ui", 8192, NULL, 4, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "贪吃蛇 UI 已初始化，棋盘 %ux%u", SNAKE_BOARD_WIDTH, SNAKE_BOARD_HEIGHT);
    return ESP_OK;
}
