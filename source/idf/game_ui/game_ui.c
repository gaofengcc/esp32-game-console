#include "game_ui.h"

#include <math.h>
#include <string.h>

#include "game_ui_port.h"
#include "lvgl.h"
#include "assets/snake_16x16.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 默认使用 A 经典红苹果；编译时设为 1/2 可切换 B 卡通亮眼版/C 简洁版。 */
#ifndef GAME_UI_APPLE_STYLE
#define GAME_UI_APPLE_STYLE 0
#endif

#if GAME_UI_APPLE_STYLE == 0
#include "assets/apple_16x16_a.h"
#define GAME_UI_APPLE_IMAGE apple_16x16_a
#elif GAME_UI_APPLE_STYLE == 1
#include "assets/apple_16x16_b.h"
#define GAME_UI_APPLE_IMAGE apple_16x16_b
#elif GAME_UI_APPLE_STYLE == 2
#include "assets/apple_16x16_c.h"
#define GAME_UI_APPLE_IMAGE apple_16x16_c
#else
#error "GAME_UI_APPLE_STYLE must be 0 (A), 1 (B), or 2 (C)"
#endif

extern const lv_font_t lv_font_cjk_16;

#define GAME_UI_KEY_QUEUE_LEN 24
#define GAME_UI_TICK_MS 20U
#define GAME_UI_CELL_PX 16U
#define GAME_UI_STATUS_BAR_PX 32U
#define GAME_UI_BOARD_CELLS (SNAKE_BOARD_WIDTH * SNAKE_BOARD_HEIGHT)

/* 扭动动画参数：默认 3px（用户 2026-09-26 拍板）、50ms 更新一次，即 20Hz。 */
#ifndef GAME_UI_WIGGLE_ENABLE
#define GAME_UI_WIGGLE_ENABLE 1
#endif
#ifndef GAME_UI_WIGGLE_AMPLITUDE_PX
#define GAME_UI_WIGGLE_AMPLITUDE_PX 3
#endif
#ifndef GAME_UI_WIGGLE_UPDATE_MS
#define GAME_UI_WIGGLE_UPDATE_MS 50U
#endif
#ifndef GAME_UI_WIGGLE_CYCLE_MS
#define GAME_UI_WIGGLE_CYCLE_MS 1200U
#endif
#ifndef GAME_UI_WIGGLE_SEGMENT_PHASE_DEG
#define GAME_UI_WIGGLE_SEGMENT_PHASE_DEG 42
#endif

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

/* 菜单/状态栏上的键位提示集中定义，后续改键位只需要改这里。 */
#define GAME_UI_KEY1_TEXT "K1"
#define GAME_UI_KEY2_TEXT "K2"
#define GAME_UI_KEY3_TEXT "K3"
#define GAME_UI_KEY5_TEXT "K5"

#define COLOR_BG 0x0F1720
#define COLOR_PANEL 0x17232B
#define COLOR_TEXT 0xE8F1F2
#define COLOR_ACCENT 0x4DD0E1
#define COLOR_SNAKE_HEAD 0xAEEA00
#define COLOR_SNAKE_BODY 0x2E8B57
#define COLOR_FOOD 0xEF5350
#define COLOR_GRID 0x141E26
#define COLOR_BUTTON 0x263238
#define COLOR_BUTTON_PRESSED 0x34545E
#define COLOR_EYE 0x18332B

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
static lv_obj_t *s_snake_images[SNAKE_MAX_SEGMENTS];
static const lv_image_dsc_t *s_snake_sources[SNAKE_MAX_SEGMENTS];
static int16_t s_snake_rendered_x[SNAKE_MAX_SEGMENTS];
static int16_t s_snake_rendered_y[SNAKE_MAX_SEGMENTS];
static uint16_t s_snake_image_count;
static lv_obj_t *s_food_image;
static lv_obj_t *s_score_label;
static lv_obj_t *s_best_label;
static lv_obj_t *s_pause_overlay;
static lv_obj_t *s_end_screen;
static lv_obj_t *s_end_title;
static lv_obj_t *s_end_score;
static lv_obj_t *s_end_high_score;
static lv_font_t s_body_font_with_fallback;
static lv_font_t s_title_font_with_fallback;

static bool s_wrap_enabled = true;
static uint8_t s_speed_level = 0;
static int s_rendered_score = -1;
static int s_rendered_best_score = -1;
static bool s_rendered_paused;
static bool s_board_needs_full_refresh = true;
static snake_point_t s_rendered_food = {UINT8_MAX, UINT8_MAX};
static uint32_t s_wiggle_phase_ms;
static uint32_t s_wiggle_elapsed_ms;
static bool s_wiggle_dirty;
static int16_t s_wiggle_offsets[SNAKE_MAX_SEGMENTS];
static uint16_t s_wiggle_updated_objects;

static void game_ui_prepare_fonts(void)
{
    s_body_font_with_fallback = *game_ui_port_font_body();
    s_body_font_with_fallback.fallback = &lv_font_cjk_16;
    s_title_font_with_fallback = *game_ui_port_font_title();
    s_title_font_with_fallback.fallback = &lv_font_cjk_16;
}

static const lv_font_t *game_ui_font_body(void)
{
    return &s_body_font_with_fallback;
}

static const lv_font_t *game_ui_font_title(void)
{
    return &s_title_font_with_fallback;
}

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
                                         game_ui_font_body());
    lv_obj_center(label);
    if (label_out) {
        *label_out = label;
    }
    return button;
}

static lv_obj_t *game_ui_make_rect(lv_obj_t *parent, int32_t x, int32_t y,
                                   int32_t width, int32_t height,
                                   uint32_t color)
{
    lv_obj_t *rect = lv_obj_create(parent);
    lv_obj_remove_style_all(rect);
    lv_obj_set_pos(rect, x, y);
    lv_obj_set_size(rect, width, height);
    lv_obj_set_style_radius(rect, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(rect, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rect, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, LV_PART_MAIN);
    return rect;
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
    s_rendered_score = -1;
    s_rendered_best_score = -1;
    s_rendered_paused = false;
    s_board_needs_full_refresh = true;
    s_wiggle_phase_ms = 0;
    s_wiggle_elapsed_ms = 0;
    s_wiggle_dirty = true;
    memset(s_wiggle_offsets, 0, sizeof(s_wiggle_offsets));
    memset(s_snake_sources, 0, sizeof(s_snake_sources));
    for (uint16_t i = 0; i < SNAKE_MAX_SEGMENTS; ++i) {
        s_snake_rendered_x[i] = INT16_MIN;
        s_snake_rendered_y[i] = INT16_MIN;
    }
    s_render_pending = true;
}

static void game_ui_retry_game(void)
{
    snake_game_set_wrap(&s_game, s_wrap_enabled);
    snake_game_set_speed_level(&s_game, s_speed_level);
    snake_game_reset(&s_game);
    s_page = GAME_UI_PAGE_GAME;
    s_rendered_score = -1;
    s_rendered_best_score = -1;
    s_rendered_paused = false;
    s_board_needs_full_refresh = true;
    s_wiggle_phase_ms = 0;
    s_wiggle_elapsed_ms = 0;
    s_wiggle_dirty = true;
    memset(s_wiggle_offsets, 0, sizeof(s_wiggle_offsets));
    memset(s_snake_sources, 0, sizeof(s_snake_sources));
    for (uint16_t i = 0; i < SNAKE_MAX_SEGMENTS; ++i) {
        s_snake_rendered_x[i] = INT16_MIN;
        s_snake_rendered_y[i] = INT16_MIN;
    }
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
    lv_label_set_text_fmt(s_menu_speed_label, "速度：%s  (" GAME_UI_KEY2_TEXT ")",
                          game_ui_speed_name());
}

static void game_ui_wrap_clicked(lv_event_t *event)
{
    (void)event;
    s_wrap_enabled = !s_wrap_enabled;
    snake_game_set_wrap(&s_game, s_wrap_enabled);
    lv_label_set_text_fmt(s_menu_wrap_label, "穿墙：%s  (" GAME_UI_KEY3_TEXT ")",
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
                                      game_ui_font_title());
    lv_obj_set_width(s_menu_title, lv_pct(100));
    lv_obj_set_style_text_align(s_menu_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *start = game_ui_make_button(s_screen, "开始游戏  (" GAME_UI_KEY1_TEXT ")",
                                          NULL);
    lv_obj_set_width(start, lv_pct(100));
    lv_obj_set_height(start, 52);
    lv_obj_add_event_cb(start, game_ui_start_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *speed = game_ui_make_button(s_screen, "速度：慢  (" GAME_UI_KEY2_TEXT ")",
                                          &s_menu_speed_label);
    lv_obj_set_width(speed, lv_pct(100));
    lv_obj_set_height(speed, 48);
    lv_obj_add_event_cb(speed, game_ui_speed_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *wrap = game_ui_make_button(s_screen, "穿墙：开  (" GAME_UI_KEY3_TEXT ")",
                                         &s_menu_wrap_label);
    lv_obj_set_width(wrap, lv_pct(100));
    lv_obj_set_height(wrap, 48);
    lv_obj_add_event_cb(wrap, game_ui_wrap_clicked, LV_EVENT_CLICKED, NULL);

    s_menu_high_score = game_ui_make_label(s_screen, "最高分：0", COLOR_TEXT,
                                           game_ui_font_body());
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

    s_score_label = game_ui_make_label(s_status_bar, "得分 0", COLOR_TEXT,
                                       game_ui_font_body());
    lv_obj_align(s_score_label, LV_ALIGN_LEFT_MID, 0, 0);
    s_best_label = game_ui_make_label(s_status_bar, "最高分 0", COLOR_ACCENT,
                                      game_ui_font_body());
    lv_obj_align(s_best_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *pause_hint_label = game_ui_make_label(
        s_status_bar, GAME_UI_KEY5_TEXT " 暂停", COLOR_TEXT,
        game_ui_font_body());
    lv_obj_align(pause_hint_label, LV_ALIGN_RIGHT_MID, 0, 0);

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
            lv_obj_remove_style_all(cell);
            s_cells[index] = cell;
            lv_obj_set_pos(cell, x * GAME_UI_CELL_PX, y * GAME_UI_CELL_PX);
            lv_obj_set_size(cell, GAME_UI_CELL_PX, GAME_UI_CELL_PX);
            lv_obj_set_style_radius(cell, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(cell, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(cell, lv_color_hex(COLOR_GRID), LV_PART_MAIN);
            lv_obj_set_style_border_opa(cell, LV_OPA_30, LV_PART_MAIN);
            lv_obj_set_style_shadow_width(cell, 0, LV_PART_MAIN);
            lv_obj_set_style_bg_color(cell, lv_color_hex(COLOR_BG), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_PART_MAIN);
            s_cell_state[index] = 0;
        }
    }

    /* 棋盘内侧边界线：不占用格子尺寸，提醒孩子穿墙会从对面出来。 */
    (void)game_ui_make_rect(s_board, 0, 0,
                            SNAKE_BOARD_WIDTH * GAME_UI_CELL_PX, 2,
                            COLOR_ACCENT);
    (void)game_ui_make_rect(
        s_board, 0, SNAKE_BOARD_HEIGHT * GAME_UI_CELL_PX - 2,
        SNAKE_BOARD_WIDTH * GAME_UI_CELL_PX, 2, COLOR_ACCENT);
    (void)game_ui_make_rect(
        s_board, 0, 0, 2, SNAKE_BOARD_HEIGHT * GAME_UI_CELL_PX, COLOR_ACCENT);
    (void)game_ui_make_rect(
        s_board, SNAKE_BOARD_WIDTH * GAME_UI_CELL_PX - 2, 0, 2,
        SNAKE_BOARD_HEIGHT * GAME_UI_CELL_PX, COLOR_ACCENT);

    /* 蛇段图像按实际蛇长懒创建，避免 540 个隐藏对象耗尽 LVGL 内存池。 */
    s_snake_image_count = 0;
    s_food_image = lv_image_create(s_board);
    lv_obj_remove_style_all(s_food_image);
    lv_obj_set_size(s_food_image, GAME_UI_CELL_PX, GAME_UI_CELL_PX);
    lv_image_set_antialias(s_food_image, false);
    lv_image_set_src(s_food_image, &GAME_UI_APPLE_IMAGE);
    lv_obj_add_flag(s_food_image, LV_OBJ_FLAG_HIDDEN);

    /* 暂停遮罩覆盖游戏页，避免只显示一行容易忽略的提示。 */
    s_pause_overlay = lv_obj_create(s_game_screen);
    /* 只在暂停提示周围建立局部半透明面板，避免覆盖整块棋盘。 */
    /* 提示面板放在棋盘上方，避免遮住初始蛇身和扭动采样区域。 */
    lv_obj_set_pos(s_pause_overlay, 176, 40);
    lv_obj_set_size(s_pause_overlay, 128, 64);
    lv_obj_set_scrollbar_mode(s_pause_overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_pause_overlay, lv_color_hex(COLOR_BG),
                              LV_PART_MAIN);
    /*
     * 背景不使用覆盖整屏的半透明层：透明层会让 LVGL 在蛇身每次
     * 位移时重新合成整块屏幕。暂停时改为降低蛇图自身不透明度，
     * 视觉上仍是“罩住”蛇，但刷新区域保持在发生位移的体节附近。
     */
    lv_obj_set_style_bg_opa(s_pause_overlay, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pause_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_pause_overlay, 0, LV_PART_MAIN);
    lv_obj_t *pause_overlay_title = game_ui_make_label(
        s_pause_overlay, "已暂停", COLOR_ACCENT, &lv_font_cjk_16);
    lv_obj_align(pause_overlay_title, LV_ALIGN_CENTER, 0, -12);
    lv_obj_t *pause_overlay_hint = game_ui_make_label(
        s_pause_overlay, "按 " GAME_UI_KEY5_TEXT " 继续", COLOR_TEXT,
        &lv_font_cjk_16);
    lv_obj_align(pause_overlay_hint, LV_ALIGN_CENTER, 0, 26);
    lv_obj_add_flag(s_pause_overlay, LV_OBJ_FLAG_HIDDEN);

}

static void game_ui_create_end_screen(void)
{
    s_end_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_end_screen);
    lv_obj_set_style_pad_all(s_end_screen, 22, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_end_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_end_screen, 12, LV_PART_MAIN);

    s_end_title = game_ui_make_label(s_end_screen, "游戏结束", COLOR_FOOD,
                                     game_ui_font_title());
    lv_obj_set_width(s_end_title, lv_pct(100));
    lv_obj_set_style_text_align(s_end_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_end_score = game_ui_make_label(s_end_screen, "本局得分：0", COLOR_TEXT,
                                     game_ui_font_body());
    lv_obj_set_width(s_end_score, lv_pct(100));
    lv_obj_set_style_text_align(s_end_score, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_end_high_score = game_ui_make_label(s_end_screen, "最高分：0", COLOR_ACCENT,
                                          game_ui_font_body());
    lv_obj_set_width(s_end_high_score, lv_pct(100));
    lv_obj_set_style_text_align(s_end_high_score, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    lv_obj_t *retry = game_ui_make_button(
        s_end_screen, "再来一次 (" GAME_UI_KEY1_TEXT ")", NULL);
    lv_obj_set_width(retry, lv_pct(100));
    lv_obj_set_height(retry, 52);
    lv_obj_add_event_cb(retry, game_ui_retry_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back = game_ui_make_button(
        s_end_screen, "返回首页 (" GAME_UI_KEY5_TEXT ")", NULL);
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
    lv_label_set_text_fmt(s_menu_speed_label, "速度：%s  (" GAME_UI_KEY2_TEXT ")",
                          game_ui_speed_name());
    lv_label_set_text_fmt(s_menu_wrap_label, "穿墙：%s  (" GAME_UI_KEY3_TEXT ")",
                          s_wrap_enabled ? "开" : "关");
    const snake_state_t *state = snake_game_state(&s_game);
    lv_label_set_text_fmt(s_menu_high_score, "最高分：%d",
                          state ? state->best_score : 0);
    lv_screen_load(s_screen);
}

static bool game_ui_same_point(snake_point_t a, snake_point_t b)
{
    return a.x == b.x && a.y == b.y;
}

static snake_direction_t game_ui_opposite_direction(snake_direction_t direction)
{
    switch (direction) {
        case SNAKE_DIRECTION_UP: return SNAKE_DIRECTION_DOWN;
        case SNAKE_DIRECTION_DOWN: return SNAKE_DIRECTION_UP;
        case SNAKE_DIRECTION_LEFT: return SNAKE_DIRECTION_RIGHT;
        case SNAKE_DIRECTION_RIGHT:
        default: return SNAKE_DIRECTION_LEFT;
    }
}

/* 返回相邻节从 from 指向 to 的方向，兼容穿墙跨边界的相邻节。 */
static snake_direction_t game_ui_direction_between(snake_point_t from,
                                                   snake_point_t to)
{
    if (from.x == to.x) {
        if ((uint8_t)(from.y + 1U) == to.y ||
            (from.y == SNAKE_BOARD_HEIGHT - 1U && to.y == 0U)) {
            return SNAKE_DIRECTION_DOWN;
        }
        return SNAKE_DIRECTION_UP;
    }
    if ((uint8_t)(from.x + 1U) == to.x ||
        (from.x == SNAKE_BOARD_WIDTH - 1U && to.x == 0U)) {
        return SNAKE_DIRECTION_RIGHT;
    }
    return SNAKE_DIRECTION_LEFT;
}

static const lv_image_dsc_t *game_ui_body_source(snake_direction_t direction)
{
    switch (direction) {
        case SNAKE_DIRECTION_UP: return &snake_body_up;
        case SNAKE_DIRECTION_DOWN: return &snake_body_down;
        case SNAKE_DIRECTION_LEFT: return &snake_body_left;
        case SNAKE_DIRECTION_RIGHT:
        default: return &snake_body_right;
    }
}

static const lv_image_dsc_t *game_ui_head_source(snake_direction_t direction)
{
    switch (direction) {
        case SNAKE_DIRECTION_UP: return &snake_head_up;
        case SNAKE_DIRECTION_DOWN: return &snake_head_down;
        case SNAKE_DIRECTION_LEFT: return &snake_head_left;
        case SNAKE_DIRECTION_RIGHT:
        default: return &snake_head_right;
    }
}

static const lv_image_dsc_t *game_ui_tail_source(snake_direction_t direction)
{
    /* direction 是尾节指向身体的方向，资源同名端为粗端。 */
    switch (direction) {
        case SNAKE_DIRECTION_UP: return &snake_tail_up;
        case SNAKE_DIRECTION_DOWN: return &snake_tail_down;
        case SNAKE_DIRECTION_LEFT: return &snake_tail_left;
        case SNAKE_DIRECTION_RIGHT:
        default: return &snake_tail_right;
    }
}

static const lv_image_dsc_t *game_ui_turn_source(snake_direction_t a,
                                                 snake_direction_t b)
{
    bool up = a == SNAKE_DIRECTION_UP || b == SNAKE_DIRECTION_UP;
    bool down = a == SNAKE_DIRECTION_DOWN || b == SNAKE_DIRECTION_DOWN;
    bool left = a == SNAKE_DIRECTION_LEFT || b == SNAKE_DIRECTION_LEFT;
    bool right = a == SNAKE_DIRECTION_RIGHT || b == SNAKE_DIRECTION_RIGHT;
    if (up && right) return &snake_turn_up_right;
    if (right && down) return &snake_turn_right_down;
    if (down && left) return &snake_turn_down_left;
    return &snake_turn_left_up;
}

static const lv_image_dsc_t *game_ui_segment_source(
    const snake_state_t *state, uint16_t index)
{
    if (!state || index >= state->length) {
        return NULL;
    }
    if (index == 0U) {
        return game_ui_head_source(snake_game_get_direction(&s_game));
    }
    if (index + 1U >= state->length) {
        snake_direction_t toward_head =
            game_ui_direction_between(state->segments[index],
                                       state->segments[index - 1U]);
        return game_ui_tail_source(toward_head);
    }
    snake_direction_t toward_head =
        game_ui_direction_between(state->segments[index],
                                   state->segments[index - 1U]);
    snake_direction_t toward_tail =
        game_ui_direction_between(state->segments[index],
                                   state->segments[index + 1U]);
    if (game_ui_opposite_direction(toward_head) == toward_tail) {
        return game_ui_body_source(toward_head);
    }
    return game_ui_turn_source(toward_head, toward_tail);
}

static int16_t game_ui_wiggle_offset_for(uint16_t index)
{
#if GAME_UI_WIGGLE_ENABLE
    const float phase = ((float)s_wiggle_phase_ms * 2.0f * (float)M_PI) /
                        (float)GAME_UI_WIGGLE_CYCLE_MS;
    const float segment_phase =
        ((float)index * (float)GAME_UI_WIGGLE_SEGMENT_PHASE_DEG *
         (float)M_PI) / 180.0f;
    const float value = (float)GAME_UI_WIGGLE_AMPLITUDE_PX *
                        sinf(phase + segment_phase);
    int32_t rounded = (int32_t)(value >= 0.0f ? value + 0.5f : value - 0.5f);
    if (rounded > GAME_UI_WIGGLE_AMPLITUDE_PX) {
        rounded = GAME_UI_WIGGLE_AMPLITUDE_PX;
    }
    if (rounded < -GAME_UI_WIGGLE_AMPLITUDE_PX) {
        rounded = -GAME_UI_WIGGLE_AMPLITUDE_PX;
    }
    return (int16_t)rounded;
#else
    (void)index;
    return 0;
#endif
}

static bool game_ui_ensure_snake_image(uint16_t index)
{
    if (!s_board || index >= SNAKE_MAX_SEGMENTS) {
        return false;
    }
    if (s_snake_images[index]) {
        return true;
    }
    lv_obj_t *image = lv_image_create(s_board);
    if (!image) {
        return false;
    }
    lv_obj_remove_style_all(image);
    lv_obj_set_size(image, GAME_UI_CELL_PX, GAME_UI_CELL_PX);
    lv_image_set_antialias(image, false);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    s_snake_images[index] = image;
    s_snake_rendered_x[index] = INT16_MIN;
    s_snake_rendered_y[index] = INT16_MIN;
    if (index >= s_snake_image_count) {
        s_snake_image_count = (uint16_t)(index + 1U);
    }
    return true;
}

static bool game_ui_segment_is_horizontal(const snake_state_t *state,
                                          uint16_t index)
{
    if (!state || index >= state->length) {
        return true;
    }
    if (index == 0U) {
        snake_direction_t direction = snake_game_get_direction(&s_game);
        return direction == SNAKE_DIRECTION_LEFT ||
               direction == SNAKE_DIRECTION_RIGHT;
    }
    snake_direction_t direction =
        game_ui_direction_between(state->segments[index],
                                  state->segments[index - 1U]);
    return direction == SNAKE_DIRECTION_LEFT ||
           direction == SNAKE_DIRECTION_RIGHT;
}

static void game_ui_update_snake(const snake_state_t *state)
{
    if (!state || !s_board) {
        return;
    }
    s_wiggle_updated_objects = 0;
    for (uint16_t i = 0; i < state->length; ++i) {
        if (!game_ui_ensure_snake_image(i)) {
            break;
        }
        lv_obj_t *image = s_snake_images[i];
        if (!image) {
            continue;
        }
        const lv_image_dsc_t *source = game_ui_segment_source(state, i);
        if (source != s_snake_sources[i]) {
            lv_image_set_src(image, source);
            s_snake_sources[i] = source;
            ++s_wiggle_updated_objects;
        }
        int16_t offset = i == 0U ? 0 : game_ui_wiggle_offset_for(i);
        s_wiggle_offsets[i] = offset;
        int16_t x = (int16_t)state->segments[i].x * GAME_UI_CELL_PX;
        int16_t y = (int16_t)state->segments[i].y * GAME_UI_CELL_PX;
        if (game_ui_segment_is_horizontal(state, i)) {
            y = (int16_t)(y + offset);
        } else {
            x = (int16_t)(x + offset);
        }
        if (s_snake_rendered_x[i] != x || s_snake_rendered_y[i] != y) {
            lv_obj_set_pos(image, x, y);
            s_snake_rendered_x[i] = x;
            s_snake_rendered_y[i] = y;
            ++s_wiggle_updated_objects;
        }
        if (lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(image, LV_OBJ_FLAG_HIDDEN);
            ++s_wiggle_updated_objects;
        }
    }
    for (uint16_t i = state->length; i < s_snake_image_count; ++i) {
        lv_obj_t *image = s_snake_images[i];
        if (image && !lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
            ++s_wiggle_updated_objects;
        }
    }
}

/* 吃到食物时只放大状态栏得分标签，260ms 内回弹，不触碰整屏。 */
static void game_ui_score_anim_exec(void *obj, int32_t value)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)obj, value, LV_PART_MAIN);
}

static void game_ui_animate_score(void)
{
    if (!s_score_label) {
        return;
    }
    lv_anim_delete(s_score_label, game_ui_score_anim_exec);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_score_label);
    lv_anim_set_exec_cb(&anim, game_ui_score_anim_exec);
    lv_anim_set_values(&anim, 256, 320);
    lv_anim_set_duration(&anim, 130);
    lv_anim_set_reverse_duration(&anim, 130);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_early_apply(&anim, true);
    lv_anim_start(&anim);
}

static void game_ui_update_decorations(const snake_state_t *state)
{
    if (!state || !s_board || !s_food_image) {
        return;
    }

    if (!game_ui_same_point(state->food, s_rendered_food)) {
        int32_t food_x = (int32_t)state->food.x * GAME_UI_CELL_PX;
        int32_t food_y = (int32_t)state->food.y * GAME_UI_CELL_PX;
        lv_obj_set_pos(s_food_image, food_x, food_y);
        lv_obj_clear_flag(s_food_image, LV_OBJ_FLAG_HIDDEN);
        s_rendered_food = state->food;
    }
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
            uint8_t cell_state =
                (state->food.x == x && state->food.y == y) ? 3U : 0U;
            if (cell_state == s_cell_state[index]) {
                continue;
            }
            s_cell_state[index] = cell_state;
            /* 蛇和食物都由独立图像绘制，底格始终保持棋盘色。 */
            if (cell_state == 3U || cell_state == 0U) {
                lv_obj_set_style_bg_color(s_cells[index], lv_color_hex(COLOR_BG),
                                          LV_PART_MAIN);
            }
        }
    }
    game_ui_update_snake(state);
    game_ui_update_decorations(state);
}

static void game_ui_render_game(void *user_data)
{
    (void)user_data;
    if (!s_game_screen) {
        game_ui_create_game_screen();
    }
    if (s_board_needs_full_refresh) {
        memset(s_cell_state, 0xFF, sizeof(s_cell_state));
        s_board_needs_full_refresh = false;
    }
    game_ui_update_board();
    const snake_state_t *state = snake_game_state(&s_game);
    int score = state ? state->score : 0;
    int best_score = state ? state->best_score : 0;
    bool paused = state && state->paused;
    if (score != s_rendered_score) {
        lv_label_set_text_fmt(s_score_label, "得分 %d", score);
        if (s_rendered_score >= 0 && score > s_rendered_score) {
            game_ui_animate_score();
        }
        s_rendered_score = score;
    }
    if (best_score != s_rendered_best_score) {
        lv_label_set_text_fmt(s_best_label, "最高分 %d", best_score);
        s_rendered_best_score = best_score;
    }
    if (paused != s_rendered_paused) {
        if (paused) {
            lv_obj_clear_flag(s_pause_overlay, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_pause_overlay, LV_OBJ_FLAG_HIDDEN);
        }
        for (uint16_t i = 0; i < s_snake_image_count; ++i) {
            if (s_snake_images[i]) {
                lv_obj_set_style_opa(s_snake_images[i],
                                     paused ? LV_OPA_50 : LV_OPA_COVER,
                                     LV_PART_MAIN);
            }
        }
        s_rendered_paused = paused;
    }
    if (lv_screen_active() != s_game_screen) {
        lv_screen_load(s_game_screen);
    }
    s_wiggle_dirty = false;
}

static void game_ui_render_end(void *user_data)
{
    (void)user_data;
    const snake_state_t *state = snake_game_state(&s_game);
    if (!s_end_screen) {
        game_ui_create_end_screen();
    }
    const char *end_message = "游戏结束";
    if (state) {
        switch (state->game_over_reason) {
            case SNAKE_GAME_OVER_SELF_COLLISION:
                end_message = "哎呀，咬到自己啦！";
                break;
            case SNAKE_GAME_OVER_WALL:
                end_message = "哎呀，撞墙了！";
                break;
            case SNAKE_GAME_OVER_BOARD_FULL:
                end_message = "太厉害啦，棋盘装满了！";
                break;
            case SNAKE_GAME_OVER_NONE:
            default:
                break;
        }
    }
    lv_label_set_text(s_end_title, end_message);
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
    if (s_render_pending ||
        (s_page == GAME_UI_PAGE_GAME && s_wiggle_dirty)) {
        game_ui_render_current(NULL);
        s_render_pending = false;
        s_wiggle_dirty = false;
    }
}

static void game_ui_advance_wiggle(uint32_t elapsed_ms)
{
#if GAME_UI_WIGGLE_ENABLE
    if (s_page != GAME_UI_PAGE_GAME || elapsed_ms == 0U) {
        return;
    }
    s_wiggle_phase_ms =
        (s_wiggle_phase_ms + elapsed_ms) % GAME_UI_WIGGLE_CYCLE_MS;
    s_wiggle_elapsed_ms += elapsed_ms;
    if (s_wiggle_elapsed_ms >= GAME_UI_WIGGLE_UPDATE_MS) {
        s_wiggle_elapsed_ms %= GAME_UI_WIGGLE_UPDATE_MS;
        s_wiggle_dirty = true;
    }
#else
    (void)elapsed_ms;
#endif
}

void game_ui_update(uint32_t elapsed_ms)
{
    game_ui_key_event_t item;
    while (game_ui_pop_key(&item)) {
        game_ui_process_key_event(&item);
    }
    if (s_page == GAME_UI_PAGE_GAME) {
        const snake_state_t *before = snake_game_state(&s_game);
        snake_point_t before_head = {UINT8_MAX, UINT8_MAX};
        snake_point_t before_food = {UINT8_MAX, UINT8_MAX};
        uint16_t before_length = 0;
        int before_score = 0;
        bool before_paused = false;
        bool before_game_over = false;
        snake_direction_t before_direction = snake_game_get_direction(&s_game);
        if (before) {
            if (before->length > 0U) {
                before_head = before->segments[0];
            }
            before_food = before->food;
            before_length = before->length;
            before_score = before->score;
            before_paused = before->paused;
            before_game_over = before->game_over;
        }
        snake_game_advance(&s_game, elapsed_ms);
        const snake_state_t *after = snake_game_state(&s_game);
        snake_point_t after_head = {UINT8_MAX, UINT8_MAX};
        if (after && after->length > 0U) {
            after_head = after->segments[0];
        }
        if (!game_ui_same_point(before_head, after_head) ||
            before_food.x != (after ? after->food.x : UINT8_MAX) ||
            before_food.y != (after ? after->food.y : UINT8_MAX) ||
            before_length != (after ? after->length : 0U) ||
            before_score != (after ? after->score : 0) ||
            before_paused != (after ? after->paused : false) ||
            before_game_over != (after ? after->game_over : false) ||
            before_direction != snake_game_get_direction(&s_game)) {
            s_render_pending = true;
        }
        game_ui_advance_wiggle(elapsed_ms);
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
    game_ui_prepare_fonts();
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

uint32_t game_ui_get_wiggle_phase_ms(void)
{
    return s_wiggle_phase_ms;
}

int16_t game_ui_get_wiggle_offset(uint16_t segment_index)
{
    if (segment_index >= SNAKE_MAX_SEGMENTS) {
        return 0;
    }
    return s_wiggle_offsets[segment_index];
}

uint16_t game_ui_get_wiggle_updated_objects(void)
{
    return s_wiggle_updated_objects;
}
