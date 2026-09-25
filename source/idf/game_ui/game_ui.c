#include "game_ui.h"

#include <string.h>

#include "game_ui_port.h"
#include "lvgl.h"

#define GAME_UI_KEY_QUEUE_LEN 24
#define GAME_UI_TICK_MS 20U
#define GAME_UI_CELL_PX 16U
#define GAME_UI_STATUS_BAR_PX 32U
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
#define COLOR_BUTTON_PRESSED 0x34545E

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
static game_ui_key_event_t s_key_queue[GAME_UI_KEY_QUEUE_LEN];
static uint8_t s_key_head;
static uint8_t s_key_tail;
static bool s_task_started;
static snake_game_t s_game;
static game_ui_page_t s_page;
static bool s_render_pending;
static bool s_touch_calibration_requested;

/* 所有 LVGL 对象只在 game_ui_port_call 或 LVGL 按钮回调中访问。 */
static lv_obj_t *s_screen;
static lv_obj_t *s_menu_title;
static lv_obj_t *s_menu_speed_label;
static lv_obj_t *s_menu_wrap_label;
static lv_obj_t *s_menu_high_score;
static lv_obj_t *s_game_screen;
static lv_obj_t *s_status_bar;
static lv_obj_t *s_board;
static lv_obj_t *s_cells[GAME_UI_BOARD_CELLS];
static uint8_t s_cell_state[GAME_UI_BOARD_CELLS];
static lv_obj_t *s_score_label;
static lv_obj_t *s_pause_label;
static lv_obj_t *s_end_screen;
static lv_obj_t *s_end_title;
static lv_obj_t *s_end_score;
static lv_obj_t *s_end_high_score;

static bool s_wrap_enabled = true;
static uint8_t s_speed_level = 0;

static int game_ui_load_best(void *ctx, int *score)
{
    (void)ctx;
    return game_ui_port_load_best(score);
}

static int game_ui_save_best(void *ctx, int score)
{
    (void)ctx;
    return game_ui_port_save_best(score);
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

static lv_obj_t *game_ui_make_label(lv_obj_t *parent, const char *text,
                                    uint32_t color, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

static lv_obj_t *game_ui_make_button(lv_obj_t *parent, const char *text,
                                     lv_obj_t **label_out)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_BUTTON), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_BUTTON_PRESSED),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(COLOR_TEXT),
                                  LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(button, 2, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_t *label = game_ui_make_label(button, text, COLOR_TEXT,
                                         game_ui_port_font_body());
    lv_obj_center(label);
    if (label_out) {
        *label_out = label;
    }
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
static void game_ui_render_current(void *user_data);

static void game_ui_start_game(void)
{
    snake_game_set_wrap(&s_game, s_wrap_enabled);
    snake_game_set_speed_level(&s_game, s_speed_level);
    snake_game_reset(&s_game);
    s_page = GAME_UI_PAGE_GAME;
    s_render_pending = true;
}

static void game_ui_retry_game(void)
{
    snake_game_set_wrap(&s_game, s_wrap_enabled);
    snake_game_set_speed_level(&s_game, s_speed_level);
    snake_game_reset(&s_game);
    s_page = GAME_UI_PAGE_GAME;
    s_render_pending = true;
}

static void game_ui_start_clicked(lv_event_t *event)
{
    (void)event;
    game_ui_start_game();
    game_ui_render_game(NULL);
    s_render_pending = false;
}

static void game_ui_speed_clicked(lv_event_t *event)
{
    (void)event;
    s_speed_level = (uint8_t)((s_speed_level + 1U) % 3U);
    snake_game_set_speed_level(&s_game, s_speed_level);
    lv_label_set_text_fmt(s_menu_speed_label, "速度：%s", game_ui_speed_name());
}

static void game_ui_wrap_clicked(lv_event_t *event)
{
    (void)event;
    s_wrap_enabled = !s_wrap_enabled;
    snake_game_set_wrap(&s_game, s_wrap_enabled);
    lv_label_set_text_fmt(s_menu_wrap_label, "穿墙：%s",
                          s_wrap_enabled ? "开" : "关");
}

static void game_ui_retry_clicked(lv_event_t *event)
{
    (void)event;
    game_ui_retry_game();
    game_ui_render_game(NULL);
    s_render_pending = false;
}

static void game_ui_back_clicked(lv_event_t *event)
{
    (void)event;
    s_page = GAME_UI_PAGE_MENU;
    s_render_pending = false;
    game_ui_render_menu(NULL);
}

static void game_ui_create_menu(void)
{
    s_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_screen);
    lv_obj_set_style_pad_all(s_screen, 22, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_screen, 10, LV_PART_MAIN);

    s_menu_title = game_ui_make_label(s_screen, "贪吃蛇", COLOR_ACCENT,
                                      game_ui_port_font_title());
    lv_obj_set_width(s_menu_title, lv_pct(100));
    lv_obj_set_style_text_align(s_menu_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *start = game_ui_make_button(s_screen, "开始游戏", NULL);
    lv_obj_set_width(start, lv_pct(100));
    lv_obj_set_height(start, 52);
    lv_obj_add_event_cb(start, game_ui_start_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *speed = game_ui_make_button(s_screen, "速度：慢",
                                          &s_menu_speed_label);
    lv_obj_set_width(speed, lv_pct(100));
    lv_obj_set_height(speed, 48);
    lv_obj_add_event_cb(speed, game_ui_speed_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *wrap = game_ui_make_button(s_screen, "穿墙：开",
                                         &s_menu_wrap_label);
    lv_obj_set_width(wrap, lv_pct(100));
    lv_obj_set_height(wrap, 48);
    lv_obj_add_event_cb(wrap, game_ui_wrap_clicked, LV_EVENT_CLICKED, NULL);

    s_menu_high_score = game_ui_make_label(s_screen, "最高分：0", COLOR_TEXT,
                                           game_ui_port_font_body());
    lv_obj_set_width(s_menu_high_score, lv_pct(100));
    lv_obj_set_style_text_align(s_menu_high_score, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
}

static void game_ui_create_game_screen(void)
{
    s_game_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_game_screen);

    s_status_bar = lv_obj_create(s_game_screen);
    lv_obj_set_size(s_status_bar, SNAKE_BOARD_WIDTH * GAME_UI_CELL_PX,
                    GAME_UI_STATUS_BAR_PX);
    lv_obj_set_pos(s_status_bar, 0, 0);
    lv_obj_set_style_bg_color(s_status_bar, lv_color_hex(COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_status_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_status_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_status_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_status_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_status_bar, 0, LV_PART_MAIN);

    s_score_label = game_ui_make_label(s_status_bar, "得分：0", COLOR_TEXT,
                                       game_ui_port_font_body());
    lv_obj_align(s_score_label, LV_ALIGN_LEFT_MID, 0, 0);

    s_board = lv_obj_create(s_game_screen);
    lv_obj_set_size(s_board, SNAKE_BOARD_WIDTH * GAME_UI_CELL_PX,
                    SNAKE_BOARD_HEIGHT * GAME_UI_CELL_PX);
    lv_obj_set_pos(s_board, 0, GAME_UI_STATUS_BAR_PX);
    lv_obj_set_style_bg_color(s_board, lv_color_hex(COLOR_BG), LV_PART_MAIN);
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
    s_pause_label = game_ui_make_label(s_board, "", COLOR_ACCENT,
                                       game_ui_port_font_body());
    lv_obj_center(s_pause_label);
}

static void game_ui_create_end_screen(void)
{
    s_end_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_end_screen);
    lv_obj_set_style_pad_all(s_end_screen, 22, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_end_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_end_screen, 12, LV_PART_MAIN);

    s_end_title = game_ui_make_label(s_end_screen, "游戏结束", COLOR_FOOD,
                                     game_ui_port_font_title());
    lv_obj_set_width(s_end_title, lv_pct(100));
    lv_obj_set_style_text_align(s_end_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_end_score = game_ui_make_label(s_end_screen, "本局得分：0", COLOR_TEXT,
                                     game_ui_port_font_body());
    lv_obj_set_width(s_end_score, lv_pct(100));
    lv_obj_set_style_text_align(s_end_score, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_end_high_score = game_ui_make_label(s_end_screen, "最高分：0", COLOR_ACCENT,
                                          game_ui_port_font_body());
    lv_obj_set_width(s_end_high_score, lv_pct(100));
    lv_obj_set_style_text_align(s_end_high_score, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    lv_obj_t *retry = game_ui_make_button(s_end_screen, "再来一次", NULL);
    lv_obj_set_width(retry, lv_pct(100));
    lv_obj_set_height(retry, 52);
    lv_obj_add_event_cb(retry, game_ui_retry_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back = game_ui_make_button(s_end_screen, "返回首页", NULL);
    lv_obj_set_width(back, lv_pct(100));
    lv_obj_set_height(back, 48);
    lv_obj_add_event_cb(back, game_ui_back_clicked, LV_EVENT_CLICKED, NULL);
}

static void game_ui_render_menu(void *user_data)
{
    (void)user_data;
    if (!s_screen) {
        game_ui_create_menu();
    }
    lv_label_set_text_fmt(s_menu_speed_label, "速度：%s", game_ui_speed_name());
    lv_label_set_text_fmt(s_menu_wrap_label, "穿墙：%s",
                          s_wrap_enabled ? "开" : "关");
    const snake_state_t *state = snake_game_state(&s_game);
    lv_label_set_text_fmt(s_menu_high_score, "最高分：%d",
                          state ? state->best_score : 0);
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
    const snake_state_t *state = snake_game_state(&s_game);
    if (!state) {
        return;
    }
    for (uint16_t y = 0; y < SNAKE_BOARD_HEIGHT; ++y) {
        for (uint16_t x = 0; x < SNAKE_BOARD_WIDTH; ++x) {
            uint16_t index = (uint16_t)(y * SNAKE_BOARD_WIDTH + x);
            uint8_t cell_state = game_ui_cell_state(
                x, y, state->segments, state->length, state->food);
            if (cell_state == s_cell_state[index]) {
                continue;
            }
            s_cell_state[index] = cell_state;
            uint32_t color = COLOR_BG;
            if (cell_state == 1) color = COLOR_SNAKE_BODY;
            if (cell_state == 2) color = COLOR_SNAKE_HEAD;
            if (cell_state == 3) color = COLOR_FOOD;
            lv_obj_set_style_bg_color(s_cells[index], lv_color_hex(color),
                                      LV_PART_MAIN);
        }
    }
}

static void game_ui_render_game(void *user_data)
{
    (void)user_data;
    if (!s_game_screen) {
        game_ui_create_game_screen();
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
    lv_screen_load(s_game_screen);
}

static void game_ui_render_end(void *user_data)
{
    (void)user_data;
    const snake_state_t *state = snake_game_state(&s_game);
    if (!s_end_screen) {
        game_ui_create_end_screen();
    }
    lv_label_set_text_fmt(s_end_score, "本局得分：%d",
                          state ? state->score : 0);
    lv_label_set_text_fmt(s_end_high_score, "最高分：%d",
                          state ? state->best_score : 0);
    lv_screen_load(s_end_screen);
}

static void game_ui_render_current(void *user_data)
{
    switch (s_page) {
        case GAME_UI_PAGE_GAME: game_ui_render_game(user_data); break;
        case GAME_UI_PAGE_END: game_ui_render_end(user_data); break;
        case GAME_UI_PAGE_MENU:
        default: game_ui_render_menu(user_data); break;
    }
}

static void game_ui_process_key_event(const game_ui_key_event_t *item)
{
    if (!item) {
        return;
    }
    if (s_page == GAME_UI_PAGE_MENU) {
        if (item->key == GAME_UI_KEY_UP) {
            game_ui_start_game();
        } else if (item->key == GAME_UI_KEY_DOWN) {
            s_speed_level = (uint8_t)((s_speed_level + 1U) % 3U);
            snake_game_set_speed_level(&s_game, s_speed_level);
            s_render_pending = true;
        } else if (item->key == GAME_UI_KEY_LEFT) {
            s_wrap_enabled = !s_wrap_enabled;
            snake_game_set_wrap(&s_game, s_wrap_enabled);
            s_render_pending = true;
        }
        return;
    }
    if (s_page == GAME_UI_PAGE_END) {
        if (item->key == GAME_UI_KEY_UP) {
            game_ui_retry_game();
        } else if (item->key == GAME_UI_KEY_PAUSE) {
            s_page = GAME_UI_PAGE_MENU;
            s_render_pending = true;
        }
        return;
    }
    snake_game_set_input(&s_game, game_ui_input_from_key(item->key));
}

static void game_ui_event_callback(const ad_keys_event_t *event, void *user_ctx)
{
    (void)user_ctx;
    if (!event || (event->type != AD_KEYS_EVENT_PRESS &&
                   event->type != AD_KEYS_EVENT_REPEAT)) {
        return;
    }
    uint8_t next = (uint8_t)((s_key_head + 1U) % GAME_UI_KEY_QUEUE_LEN);
    if (next == s_key_tail) {
        return;
    }
    s_key_queue[s_key_head] = (game_ui_key_event_t){
        .key = event->key,
        .type = event->type,
    };
    s_key_head = next;
}

static bool game_ui_pop_key(game_ui_key_event_t *item)
{
    if (s_key_tail == s_key_head) {
        return false;
    }
    if (item) {
        *item = s_key_queue[s_key_tail];
    }
    s_key_tail = (uint8_t)((s_key_tail + 1U) % GAME_UI_KEY_QUEUE_LEN);
    return true;
}

static void game_ui_update_lvgl(void *user_data)
{
    (void)user_data;
    snake_status_t status = snake_game_get_status(&s_game);
    if (status == SNAKE_STATUS_GAME_OVER && s_page != GAME_UI_PAGE_END) {
        s_page = GAME_UI_PAGE_END;
        s_render_pending = true;
    }
    if (s_render_pending || s_page == GAME_UI_PAGE_GAME) {
        game_ui_render_current(NULL);
        s_render_pending = false;
    }
}

void game_ui_update(uint32_t elapsed_ms)
{
    game_ui_key_event_t item;
    while (game_ui_pop_key(&item)) {
        game_ui_process_key_event(&item);
    }
    if (s_page == GAME_UI_PAGE_GAME) {
        snake_game_advance(&s_game, elapsed_ms);
    }
    (void)game_ui_port_call(game_ui_update_lvgl, NULL);
}

static void game_ui_task(void *arg)
{
    (void)arg;
    uint32_t last = game_ui_port_tick_ms();
    for (;;) {
        uint32_t now = game_ui_port_tick_ms();
        uint32_t elapsed = now - last;
        last = now;
        if (elapsed > 1000U) {
            elapsed = GAME_UI_TICK_MS;
        }
        game_ui_update(elapsed);
        if (!game_ui_port_touch_calibration_valid() &&
            !s_touch_calibration_requested) {
            (void)game_ui_port_request_touch_calibration();
            s_touch_calibration_requested = true;
        }
        game_ui_port_delay_ms(GAME_UI_TICK_MS);
    }
}

static void game_ui_create_initial(void *user_data)
{
    (void)user_data;
    s_page = GAME_UI_PAGE_MENU;
    s_render_pending = false;
    game_ui_render_menu(NULL);
}

esp_err_t game_ui_init(void)
{
    if (s_task_started) {
        return ESP_OK;
    }
    snake_config_t config;
    snake_config_default(&config);
    config.load_best = game_ui_load_best;
    config.save_best = game_ui_save_best;
    config.storage_ctx = NULL;
    config.wrap_walls = true;
    config.initial_speed_ms = SNAKE_SPEED_SLOW_MS;
    config.width = SNAKE_BOARD_WIDTH;
    config.height = SNAKE_BOARD_HEIGHT;
    snake_game_init(&s_game, &config);
    s_key_head = 0;
    s_key_tail = 0;
    game_ui_port_set_key_callback(game_ui_event_callback, NULL);
    esp_err_t err = game_ui_port_call(game_ui_create_initial, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = game_ui_port_start_task(game_ui_task, "game_ui", 8192, 4, NULL);
    if (err != ESP_OK) {
        return err;
    }
    s_task_started = true;
    game_ui_port_log_i(TAG, "贪吃蛇 UI 已初始化，棋盘 %ux%u",
                       SNAKE_BOARD_WIDTH, SNAKE_BOARD_HEIGHT);
    return ESP_OK;
}

const snake_state_t *game_ui_get_state(void)
{
    return snake_game_state(&s_game);
}

bool game_ui_force_food(snake_point_t food)
{
    return snake_game_force_food(&s_game, food);
}

void game_ui_force_self_collision(void)
{
    s_page = GAME_UI_PAGE_GAME;
    s_game.state.length = 4;
    s_game.state.segments[0] = (snake_point_t){10, 10};
    s_game.state.segments[1] = (snake_point_t){9, 10};
    s_game.state.segments[2] = (snake_point_t){10, 9};
    s_game.state.segments[3] = (snake_point_t){11, 9};
    s_game.direction = SNAKE_DIRECTION_UP;
    s_game.pending_direction = SNAKE_DIRECTION_UP;
    s_game.state.paused = false;
    s_game.state.game_over = false;
    s_render_pending = true;
}
