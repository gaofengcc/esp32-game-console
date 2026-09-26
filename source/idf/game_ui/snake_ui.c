#include "snake_ui.h"

#include <math.h>
#include <string.h>

#include "game_ui_port.h"
#include "lvgl.h"
#include "assets/snake_16x16.h"

/* 部分工具链不默认暴露 M_PI，扭动动画只需要这个常量。 */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 0/1/2 对应三套苹果贴图，构建时可切换而无需改渲染逻辑。 */
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

/* 蛇身轻微扭动是纯 UI 动效，与逻辑步进相互独立。 */
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

/* 菜单：开始、速度、穿墙、触摸、返回。 */
#define GAME_UI_MENU_ITEM_COUNT 5U
#define GAME_UI_END_ITEM_COUNT 2U
#define GAME_UI_PAUSE_ITEM_COUNT 3U

typedef enum {
    SNAKE_UI_PAGE_MENU = 0,
    SNAKE_UI_PAGE_GAME,
    SNAKE_UI_PAGE_END,
} snake_ui_page_t;

static const char *TAG = "snake_ui";
/* 逻辑状态只在 game_ui 任务访问；LVGL 对象只在 render 回调访问。 */
static snake_game_t s_game;
static snake_ui_page_t s_page;
static game_ui_back_cb_t s_on_back;

static lv_obj_t *s_screen;
static lv_obj_t *s_menu_title;
static lv_obj_t *s_menu_buttons[GAME_UI_MENU_ITEM_COUNT];
static lv_obj_t *s_menu_speed_label;
static lv_obj_t *s_menu_wrap_label;
static lv_obj_t *s_menu_touch_label;
static lv_obj_t *s_menu_high_score;
static lv_obj_t *s_menu_hint;
static lv_obj_t *s_end_buttons[GAME_UI_END_ITEM_COUNT];
static lv_obj_t *s_pause_buttons[GAME_UI_PAUSE_ITEM_COUNT];
static uint8_t s_menu_index;
static uint8_t s_end_index;
static uint8_t s_pause_index;
static lv_obj_t *s_game_screen;
static lv_obj_t *s_status_bar;
static lv_obj_t *s_board;
static lv_obj_t *s_snake_images[SNAKE_MAX_SEGMENTS];
static const lv_image_dsc_t *s_snake_sources[SNAKE_MAX_SEGMENTS];
static int16_t s_snake_rendered_x[SNAKE_MAX_SEGMENTS];
static int16_t s_snake_rendered_y[SNAKE_MAX_SEGMENTS];
static uint16_t s_snake_image_count;
static lv_obj_t *s_food_image;
static lv_obj_t *s_score_label;
static lv_obj_t *s_best_label;
static lv_obj_t *s_speed_label;
static lv_obj_t *s_pause_overlay;
static lv_obj_t *s_end_screen;
static lv_obj_t *s_end_title;
static lv_obj_t *s_end_score;
static lv_obj_t *s_end_high_score;

/* 设置页的当前选项，下一局开始时同步到逻辑层。 */
static bool s_wrap_enabled = true;
static bool s_touch_control_enabled = false;
static uint8_t s_speed_level = 0;
/* 以下缓存是最近一次已应用到 LVGL 的值，避免每个 20ms tick 重写标签。 */
static int s_rendered_score = -1;
static int s_rendered_best = -1;
static int s_rendered_speed_ms = -1;
static bool s_rendered_paused;

/* LVGL v9 的 style/label setter 无旧值比较, 每次调用都会触发无效化重绘.
 * 以下缓存记录已应用到对象上的值, 仅在值变化时才真正调用 setter. */
static uint8_t s_menu_focus_shown = 0xFFU;   /* 菜单页已高亮的下标 */
static uint8_t s_pause_focus_shown = 0xFFU;  /* 暂停弹窗已高亮的下标 */
static uint8_t s_end_focus_shown = 0xFFU;    /* 结束页已高亮的下标 */
static uint8_t s_menu_shown_speed = 0xFFU;   /* 菜单页已显示的速度档 */
static int8_t s_menu_shown_wrap = -1;        /* 菜单页已显示的穿墙开关 */
static int8_t s_menu_shown_touch = -1;       /* 菜单页已显示的触摸开关 */
static int s_menu_shown_best = -1;           /* 菜单页已显示的最高分 */
static int s_end_shown_reason = -1;          /* 结束页已显示的结束原因 */
static int s_end_shown_score = -1;           /* 结束页已显示的本局得分 */
static int s_end_shown_best = -1;            /* 结束页已显示的最高分 */
static snake_point_t s_rendered_food = {UINT8_MAX, UINT8_MAX};
static uint32_t s_wiggle_phase_ms;
static uint32_t s_wiggle_elapsed_ms;
static bool s_wiggle_dirty;
static int16_t s_wiggle_offsets[SNAKE_MAX_SEGMENTS];
static uint16_t s_wiggle_updated_objects;

static void snake_ui_render_menu(void *user_data);
static void snake_ui_render_game(void *user_data);
static void snake_ui_render_end(void *user_data);
static void snake_ui_start_game(void);
static void snake_ui_retry_game(void);

/**
 * @brief 逻辑层读最高分回调, 转到 port 的 NVS/文件实现.
 *
 * @param ctx 未使用.
 * @param score 输出槽, 由 port 写入.
 * @return 0 成功; 非 0 失败.
 */
static int snake_ui_load_best(void *ctx, int *score)
{
    (void)ctx;
    return game_ui_port_load_best(score);
}

/**
 * @brief 逻辑层写最高分回调, 转到 port 的 NVS/文件实现.
 *
 * @param ctx 未使用.
 * @param score 要保存的分数.
 * @return 0 成功; 非 0 失败.
 */
static int snake_ui_save_best(void *ctx, int score)
{
    (void)ctx;
    return game_ui_port_save_best(score);
}

/**
 * @brief 把实体键编号映射成贪吃蛇输入.
 *
 * @param key GAME_UI_KEY_* 编号.
 * @return 对应输入; 无法识别时返回 SNAKE_INPUT_NONE.
 */
static snake_input_t snake_ui_input_from_key(uint8_t key)
{
    switch (key) {
        case GAME_UI_KEY_UP:
            return SNAKE_INPUT_UP;
        case GAME_UI_KEY_DOWN:
            return SNAKE_INPUT_DOWN;
        case GAME_UI_KEY_LEFT:
            return SNAKE_INPUT_LEFT;
        case GAME_UI_KEY_RIGHT:
            return SNAKE_INPUT_RIGHT;
        case GAME_UI_KEY_PAUSE:
            return SNAKE_INPUT_PAUSE;
        default:
            return SNAKE_INPUT_NONE;
    }
}

/**
 * @brief 刷新设置页按钮选中态和确定键提示.
 *
 * @return 无.
 */
static void snake_ui_refresh_menu_focus(void)
{
    uint8_t prev = s_menu_focus_shown;

    if (prev == s_menu_index) {
        return;
    }
    /* 只刷新失焦和新聚焦两个按钮, 避免全量 style 写入触发整屏重绘. */
    if (prev < GAME_UI_MENU_ITEM_COUNT && s_menu_buttons[prev]) {
        game_ui_set_button_focus(s_menu_buttons[prev], false);
    }
    if (s_menu_buttons[s_menu_index]) {
        game_ui_set_button_focus(s_menu_buttons[s_menu_index], true);
    }
    s_menu_focus_shown = s_menu_index;
    if (!s_menu_hint) {
        return;
    }
    if (s_menu_index == 1U) {
        lv_label_set_text(s_menu_hint, "确定 : 速度");
    } else if (s_menu_index == 2U) {
        lv_label_set_text(s_menu_hint, "确定 : 穿墙");
    } else if (s_menu_index == 3U) {
        lv_label_set_text(s_menu_hint, "确定 : 触摸");
    } else if (s_menu_index == 4U) {
        lv_label_set_text(s_menu_hint, "确定 : 返回");
    } else {
        lv_label_set_text(s_menu_hint, "确定 : 开始游戏");
    }
}

/**
 * @brief 刷新暂停弹窗两个按钮的选中态.
 *
 * @return 无.
 */
static void snake_ui_refresh_pause_focus(void)
{
    uint8_t prev = s_pause_focus_shown;

    if (prev == s_pause_index) {
        return;
    }
    if (prev < GAME_UI_PAUSE_ITEM_COUNT && s_pause_buttons[prev]) {
        game_ui_set_button_focus(s_pause_buttons[prev], false);
    }
    if (s_pause_buttons[s_pause_index]) {
        game_ui_set_button_focus(s_pause_buttons[s_pause_index], true);
    }
    s_pause_focus_shown = s_pause_index;
}

/**
 * @brief 刷新结束页两个按钮的选中态.
 *
 * @return 无.
 */
static void snake_ui_refresh_end_focus(void)
{
    uint8_t prev = s_end_focus_shown;

    if (prev == s_end_index) {
        return;
    }
    if (prev < GAME_UI_END_ITEM_COUNT && s_end_buttons[prev]) {
        game_ui_set_button_focus(s_end_buttons[prev], false);
    }
    if (s_end_buttons[s_end_index]) {
        game_ui_set_button_focus(s_end_buttons[s_end_index], true);
    }
    s_end_focus_shown = s_end_index;
}

/**
 * @brief 把内部速度档换成设置页中文名.
 *
 * @return "慢", "中" 或 "快". 静态字符串.
 */
static const char *snake_ui_speed_name(void)
{
    switch (s_speed_level) {
        case 1:
            return "中";
        case 2:
            return "快";
        default:
            return "慢";
    }
}

/**
 * @brief 把逻辑方向换成日志中的短名称.
 *
 * @param direction 当前方向.
 * @return 静态字符串.
 */
static const char *snake_ui_direction_name(snake_direction_t direction)
{
    switch (direction) {
        case SNAKE_DIRECTION_UP:
            return "up";
        case SNAKE_DIRECTION_DOWN:
            return "down";
        case SNAKE_DIRECTION_LEFT:
            return "left";
        case SNAKE_DIRECTION_RIGHT:
        default:
            return "right";
    }
}

/**
 * @brief 把结束原因换成日志中的短名称.
 *
 * @param reason 逻辑层结束原因.
 * @return 静态字符串.
 */
static const char *snake_ui_over_reason_name(snake_game_over_reason_t reason)
{
    switch (reason) {
        case SNAKE_GAME_OVER_SELF_COLLISION:
            return "self";
        case SNAKE_GAME_OVER_WALL:
            return "wall";
        case SNAKE_GAME_OVER_BOARD_FULL:
            return "full";
        case SNAKE_GAME_OVER_NONE:
        default:
            return "none";
    }
}

/**
 * @brief 刷新状态栏历史最高分, 并跟在得分右侧.
 *
 * @param best_score 要显示的最高分.
 * @return 无.
 */
static void snake_ui_refresh_best_label(int best_score)
{
    if (!s_best_label) {
        return;
    }
    lv_label_set_text_fmt(s_best_label, "最高 %d", best_score);
    if (s_score_label) {
        lv_obj_align_to(s_best_label, s_score_label, LV_ALIGN_OUT_RIGHT_MID,
                        12, 0);
    }
}

/**
 * @brief 把步进间隔换成越大越快的显示档.
 *
 * @param speed_ms 内部步进间隔, 越小越快.
 * @return 从 1 起往上加的显示档.
 */
static uint16_t snake_ui_speed_display_level(uint16_t speed_ms)
{
    uint16_t step = SNAKE_SPEED_PER_FOOD_MS;

    if (step == 0U || speed_ms >= SNAKE_SPEED_SLOW_MS) {
        return 1U;
    }
    return (uint16_t)(1U + ((SNAKE_SPEED_SLOW_MS - speed_ms) / step));
}

/**
 * @brief 清空对局渲染缓存和扭动相位, 下次按真实状态重画.
 *
 * @return 无.
 */
static void snake_ui_reset_render_cache(void)
{
    s_rendered_score = -1;
    s_rendered_best = -1;
    s_rendered_speed_ms = -1;
    /* 保留 s_rendered_paused, 下次渲染才能按真实暂停态收起弹窗并恢复蛇身透明度. */
    s_pause_index = 0U;
    s_wiggle_phase_ms = 0;
    s_wiggle_elapsed_ms = 0;
    s_wiggle_dirty = true;
    memset(s_wiggle_offsets, 0, sizeof(s_wiggle_offsets));
    memset(s_snake_sources, 0, sizeof(s_snake_sources));
    for (uint16_t i = 0; i < SNAKE_MAX_SEGMENTS; ++i) {
        s_snake_rendered_x[i] = INT16_MIN;
        s_snake_rendered_y[i] = INT16_MIN;
    }
}

/**
 * @brief 按当前设置重置逻辑并进入对局页.
 *
 * @return 无.
 */
static void snake_ui_start_game(void)
{
    const snake_state_t *state;

    snake_game_set_wrap(&s_game, s_wrap_enabled);
    snake_game_set_speed_level(&s_game, s_speed_level);
    snake_game_reset(&s_game);
    s_page = SNAKE_UI_PAGE_GAME;
    snake_ui_reset_render_cache();
    game_ui_request_render();
    state = snake_game_state(&s_game);
    game_ui_port_log_i(
        TAG,
        "进入游戏 speed_level=%u speed_ms=%u wrap=%u touch=%u "
        "head=(%u,%u) food=(%u,%u)",
        (unsigned)s_speed_level, state ? (unsigned)state->speed_ms : 0U,
        s_wrap_enabled ? 1U : 0U, s_touch_control_enabled ? 1U : 0U,
        state ? (unsigned)state->segments[0].x : 0U,
        state ? (unsigned)state->segments[0].y : 0U,
        state ? (unsigned)state->food.x : 0U,
        state ? (unsigned)state->food.y : 0U);
}

/**
 * @brief 再开一局, 保留速度和穿墙设置.
 *
 * @return 无.
 */
static void snake_ui_retry_game(void)
{
    const snake_state_t *state;

    snake_game_set_wrap(&s_game, s_wrap_enabled);
    snake_game_set_speed_level(&s_game, s_speed_level);
    snake_game_reset(&s_game);
    s_page = SNAKE_UI_PAGE_GAME;
    snake_ui_reset_render_cache();
    game_ui_request_render();
    state = snake_game_state(&s_game);
    game_ui_port_log_i(TAG, "再来一局 speed_ms=%u wrap=%u head=(%u,%u) food=(%u,%u)",
                       state ? (unsigned)state->speed_ms : 0U,
                       s_wrap_enabled ? 1U : 0U,
                       state ? (unsigned)state->segments[0].x : 0U,
                       state ? (unsigned)state->segments[0].y : 0U,
                       state ? (unsigned)state->food.x : 0U,
                       state ? (unsigned)state->food.y : 0U);
}

/**
 * @brief 通过外壳回调回到游戏选择页.
 *
 * @return 无.
 */
static void snake_ui_goto_select(void)
{
    if (s_on_back) {
        s_on_back();
    }
}

/**
 * @brief 确认设置页当前选项: 开始, 速度, 穿墙, 触摸或返回.
 *
 * @return 无.
 */
static void snake_ui_activate_menu(void)
{
    if (s_menu_index == 1U) {
        s_speed_level = (uint8_t)((s_speed_level + 1U) % 3U);
        snake_game_set_speed_level(&s_game, s_speed_level);
        game_ui_request_render();
        game_ui_port_log_i(TAG, "菜单确认速度 level=%u speed_ms=%u",
                           (unsigned)s_speed_level,
                           (unsigned)snake_game_get_speed_ms(&s_game));
    } else if (s_menu_index == 2U) {
        s_wrap_enabled = !s_wrap_enabled;
        snake_game_set_wrap(&s_game, s_wrap_enabled);
        game_ui_request_render();
        game_ui_port_log_i(TAG, "菜单确认穿墙 enabled=%u",
                           s_wrap_enabled ? 1U : 0U);
    } else if (s_menu_index == 3U) {
        s_touch_control_enabled = !s_touch_control_enabled;
        game_ui_request_render();
        game_ui_port_log_i(TAG, "菜单确认触摸 enabled=%u",
                           s_touch_control_enabled ? 1U : 0U);
    } else if (s_menu_index == 4U) {
        snake_ui_goto_select();
    } else {
        game_ui_port_log_i(TAG, "菜单确认开始游戏");
        snake_ui_start_game();
    }
}

/**
 * @brief 触摸点击"开始游戏".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void snake_ui_start_clicked(lv_event_t *event)
{
    (void)event;
    s_menu_index = 0U;
    snake_ui_start_game();
    snake_ui_render_game(NULL);
    (void)game_ui_consume_render();
}

/**
 * @brief 触摸点击速度按钮, 在三档间循环.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void snake_ui_speed_clicked(lv_event_t *event)
{
    (void)event;
    s_menu_index = 1U;
    s_speed_level = (uint8_t)((s_speed_level + 1U) % 3U);
    snake_game_set_speed_level(&s_game, s_speed_level);
    lv_label_set_text_fmt(s_menu_speed_label, "速度：%s", snake_ui_speed_name());
    snake_ui_refresh_menu_focus();
}

/**
 * @brief 触摸点击穿墙开关.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void snake_ui_wrap_clicked(lv_event_t *event)
{
    (void)event;
    s_menu_index = 2U;
    s_wrap_enabled = !s_wrap_enabled;
    snake_game_set_wrap(&s_game, s_wrap_enabled);
    lv_label_set_text_fmt(s_menu_wrap_label, "穿墙：%s",
                          s_wrap_enabled ? "开" : "关");
    snake_ui_refresh_menu_focus();
}

/**
 * @brief 触摸点击触摸转向开关.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void snake_ui_touch_clicked(lv_event_t *event)
{
    (void)event;
    s_menu_index = 3U;
    s_touch_control_enabled = !s_touch_control_enabled;
    lv_label_set_text_fmt(s_menu_touch_label, "触摸：%s",
                          s_touch_control_enabled ? "开" : "关");
    snake_ui_refresh_menu_focus();
    game_ui_port_log_i(TAG, "点击切换触摸");
}

/**
 * @brief 触摸点击结束页"再来一次".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void snake_ui_retry_clicked(lv_event_t *event)
{
    (void)event;
    s_end_index = 0U;
    snake_ui_retry_game();
    snake_ui_render_game(NULL);
    (void)game_ui_consume_render();
}

/**
 * @brief 触摸点击结束页"返回设置".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void snake_ui_back_clicked(lv_event_t *event)
{
    (void)event;
    s_menu_index = 0U;
    s_page = SNAKE_UI_PAGE_MENU;
    snake_ui_render_menu(NULL);
    (void)game_ui_consume_render();
    game_ui_port_log_i(TAG, "返回设置");
}

/**
 * @brief 触摸点击设置页"返回", 交给外壳回选择页.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void snake_ui_menu_back_clicked(lv_event_t *event)
{
    (void)event;
    s_menu_index = 4U;
    snake_ui_goto_select();
}

/**
 * @brief 确认暂停弹窗: 继续, 再来一次或返回设置.
 *
 * @return 无.
 */
static void snake_ui_activate_pause(void)
{
    game_ui_port_log_i(TAG, "暂停确认 index=%u score=%d speed_ms=%u",
                       (unsigned)s_pause_index, s_game.state.score,
                       (unsigned)s_game.state.speed_ms);
    if (s_pause_index == 1U) {
        game_ui_port_log_i(TAG, "暂停后重新开始");
        snake_ui_retry_game();
        return;
    }
    if (s_pause_index == 2U) {
        /* 先退出逻辑暂停态, 避免后台残留暂停标记影响下次开局渲染. */
        if (snake_game_get_status(&s_game) == SNAKE_STATUS_PAUSED) {
            snake_game_set_input(&s_game, SNAKE_INPUT_PAUSE);
        }
        s_menu_index = 0U;
        s_page = SNAKE_UI_PAGE_MENU;
        game_ui_request_render();
        game_ui_port_log_i(TAG, "暂停后返回设置");
        return;
    }
    if (snake_game_get_status(&s_game) == SNAKE_STATUS_PAUSED) {
        snake_game_set_input(&s_game, SNAKE_INPUT_PAUSE);
    }
    game_ui_request_render();
    game_ui_port_log_i(TAG, "暂停后继续");
}

/**
 * @brief 触摸点击暂停弹窗"继续".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void snake_ui_pause_resume_clicked(lv_event_t *event)
{
    (void)event;
    s_pause_index = 0U;
    snake_ui_activate_pause();
    snake_ui_render_game(NULL);
    (void)game_ui_consume_render();
}

/**
 * @brief 触摸点击暂停弹窗"再来一次".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void snake_ui_pause_retry_clicked(lv_event_t *event)
{
    (void)event;
    s_pause_index = 1U;
    snake_ui_activate_pause();
    snake_ui_render_game(NULL);
    (void)game_ui_consume_render();
}

/**
 * @brief 触摸点击暂停弹窗"返回", 回到贪吃蛇设置页.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void snake_ui_pause_back_clicked(lv_event_t *event)
{
    (void)event;
    s_pause_index = 2U;
    snake_ui_activate_pause();
    if (s_page == SNAKE_UI_PAGE_MENU) {
        snake_ui_render_menu(NULL);
    }
    (void)game_ui_consume_render();
}

/**
 * @brief 按蛇当前方向把触摸坐标映射成转弯输入.
 *
 * @param x 屏幕 X, 负值按 0 处理.
 * @param y 屏幕 Y, 负值按 0 处理.
 * @return 左右走映射上下, 上下走映射左右.
 */
snake_input_t snake_ui_map_touch(int32_t x, int32_t y)
{
    snake_direction_t direction = snake_game_get_direction(&s_game);

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (direction == SNAKE_DIRECTION_LEFT ||
        direction == SNAKE_DIRECTION_RIGHT) {
        return (y < (GAME_UI_SCREEN_H / 2)) ? SNAKE_INPUT_UP
                                            : SNAKE_INPUT_DOWN;
    }
    return (x < (GAME_UI_SCREEN_W / 2)) ? SNAKE_INPUT_LEFT
                                        : SNAKE_INPUT_RIGHT;
}

/**
 * @brief 打开或关闭触摸转向.
 *
 * @param enabled true 允许点棋盘转向.
 * @return 无.
 */
void snake_ui_set_touch_control(bool enabled)
{
    s_touch_control_enabled = enabled;
    game_ui_port_log_i(TAG, "触摸转向 enabled=%u", enabled ? 1U : 0U);
}

/**
 * @brief 查询触摸转向是否开启.
 *
 * @return 开启为 true.
 */
bool snake_ui_get_touch_control(void)
{
    return s_touch_control_enabled;
}

/**
 * @brief 棋盘点击: 仅触摸控制开启且对局进行中时转向.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void snake_ui_board_clicked(lv_event_t *event)
{
    lv_indev_t *indev;
    lv_point_t point;
    snake_input_t input;

    (void)event;
    if (!s_touch_control_enabled || s_page != SNAKE_UI_PAGE_GAME) {
        return;
    }
    if (snake_game_get_status(&s_game) != SNAKE_STATUS_RUNNING) {
        return;
    }
    indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_indev_get_point(indev, &point);
    input = snake_ui_map_touch(point.x, point.y);
    game_ui_port_log_i(TAG, "触摸转向 x=%d y=%d input=%u",
                       (int)point.x, (int)point.y, (unsigned)input);
    snake_game_set_input(&s_game, input);
    game_ui_request_render();
}

/**
 * @brief 懒创建贪吃蛇设置页, 只应在 LVGL 线程调用.
 *
 * @return 无.
 */
static void snake_ui_create_menu(void)
{
    s_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_screen);
    lv_obj_set_style_pad_all(s_screen, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_screen, 3, LV_PART_MAIN);

    s_menu_title = game_ui_make_label(s_screen, "贪吃蛇", GAME_UI_COLOR_ACCENT,
                                      game_ui_font_title());
    lv_obj_set_width(s_menu_title, lv_pct(100));
    lv_obj_set_style_text_align(s_menu_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_menu_high_score = game_ui_make_label(s_screen, "最高分：0",
                                           GAME_UI_COLOR_TEXT,
                                           game_ui_font_body());
    lv_obj_set_width(s_menu_high_score, lv_pct(100));
    lv_obj_set_style_text_align(s_menu_high_score, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    s_menu_buttons[0] = game_ui_make_button(s_screen, "开始游戏", NULL);
    lv_obj_set_width(s_menu_buttons[0], lv_pct(100));
    lv_obj_set_height(s_menu_buttons[0], 36);
    lv_obj_add_event_cb(s_menu_buttons[0], snake_ui_start_clicked,
                        LV_EVENT_CLICKED, NULL);

    s_menu_buttons[1] = game_ui_make_button(s_screen, "速度：慢",
                                            &s_menu_speed_label);
    lv_obj_set_width(s_menu_buttons[1], lv_pct(100));
    lv_obj_set_height(s_menu_buttons[1], 36);
    lv_obj_add_event_cb(s_menu_buttons[1], snake_ui_speed_clicked,
                        LV_EVENT_CLICKED, NULL);

    s_menu_buttons[2] = game_ui_make_button(s_screen, "穿墙：开",
                                            &s_menu_wrap_label);
    lv_obj_set_width(s_menu_buttons[2], lv_pct(100));
    lv_obj_set_height(s_menu_buttons[2], 36);
    lv_obj_add_event_cb(s_menu_buttons[2], snake_ui_wrap_clicked,
                        LV_EVENT_CLICKED, NULL);

    s_menu_buttons[3] = game_ui_make_button(s_screen, "触摸：关",
                                            &s_menu_touch_label);
    lv_obj_set_width(s_menu_buttons[3], lv_pct(100));
    lv_obj_set_height(s_menu_buttons[3], 36);
    lv_obj_add_event_cb(s_menu_buttons[3], snake_ui_touch_clicked,
                        LV_EVENT_CLICKED, NULL);

    s_menu_buttons[4] = game_ui_make_button(s_screen, "返回", NULL);
    lv_obj_set_width(s_menu_buttons[4], lv_pct(100));
    lv_obj_set_height(s_menu_buttons[4], 36);
    lv_obj_add_event_cb(s_menu_buttons[4], snake_ui_menu_back_clicked,
                        LV_EVENT_CLICKED, NULL);

    s_menu_hint = game_ui_make_label(s_screen, "确定 : 开始游戏",
                                     GAME_UI_COLOR_ACCENT, game_ui_font_body());
    lv_obj_set_width(s_menu_hint, lv_pct(100));
    lv_obj_set_style_text_align(s_menu_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_menu_index = 0U;
    snake_ui_refresh_menu_focus();
}

/**
 * @brief 在棋盘 DRAW_MAIN 里画网格, 避免为 540 格各建一个 lv_obj.
 *
 * @param event LVGL 绘制事件, 非 DRAW_MAIN 时直接返回.
 * @return 无.
 */
static void snake_ui_board_draw_grid(lv_event_t *event)
{
    lv_obj_t *obj;
    lv_layer_t *layer;
    lv_area_t coords;
    lv_draw_line_dsc_t line_dsc;
    uint16_t i;

    if (!event || lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
        return;
    }
    obj = lv_event_get_target_obj(event);
    layer = lv_event_get_layer(event);
    if (!obj || !layer) {
        return;
    }

    lv_obj_get_coords(obj, &coords);
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(GAME_UI_COLOR_GRID);
    line_dsc.opa = LV_OPA_30;
    line_dsc.width = 1;
    line_dsc.raw_end = 1;

    for (i = 1U; i < SNAKE_BOARD_WIDTH; ++i) {
        int32_t x = coords.x1 + ((int32_t)i * (int32_t)GAME_UI_CELL_PX);
        line_dsc.p1.x = x;
        line_dsc.p1.y = coords.y1;
        line_dsc.p2.x = x;
        line_dsc.p2.y = coords.y2;
        lv_draw_line(layer, &line_dsc);
    }
    for (i = 1U; i < SNAKE_BOARD_HEIGHT; ++i) {
        int32_t y = coords.y1 + ((int32_t)i * (int32_t)GAME_UI_CELL_PX);
        line_dsc.p1.x = coords.x1;
        line_dsc.p1.y = y;
        line_dsc.p2.x = coords.x2;
        line_dsc.p2.y = y;
        lv_draw_line(layer, &line_dsc);
    }
}

/**
 * @brief 懒创建对局页, 含状态栏, 棋盘, 食物和暂停弹窗.
 *
 * @return 无.
 */
static void snake_ui_create_game(void)
{
    game_ui_port_log_i(TAG, "开始创建游戏页, 棋盘 %ux%u",
                       SNAKE_BOARD_WIDTH, SNAKE_BOARD_HEIGHT);
    s_game_screen = lv_obj_create(NULL);
    if (!s_game_screen) {
        game_ui_port_log_i(TAG, "创建游戏页失败: screen 为空");
        return;
    }
    game_ui_set_screen_style(s_game_screen);

    s_status_bar = lv_obj_create(s_game_screen);
    lv_obj_set_size(s_status_bar, SNAKE_BOARD_WIDTH * GAME_UI_CELL_PX,
                    GAME_UI_STATUS_BAR_PX);
    lv_obj_set_pos(s_status_bar, 0, 0);
    lv_obj_set_style_bg_color(s_status_bar, lv_color_hex(GAME_UI_COLOR_PANEL),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_status_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_status_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_status_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_status_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_status_bar, 0, LV_PART_MAIN);

    s_score_label = game_ui_make_label(s_status_bar, "得分 0", GAME_UI_COLOR_TEXT,
                                       game_ui_font_body());
    lv_obj_align(s_score_label, LV_ALIGN_LEFT_MID, 0, 0);
    s_best_label = game_ui_make_label(s_status_bar, "最高 0",
                                     GAME_UI_COLOR_TEXT, game_ui_font_body());
    lv_obj_align_to(s_best_label, s_score_label, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    s_speed_label = game_ui_make_label(s_status_bar, "速度 1",
                                       GAME_UI_COLOR_ACCENT,
                                       game_ui_font_body());
    lv_obj_align(s_speed_label, LV_ALIGN_CENTER, 0, 0);
    {
        lv_obj_t *pause_hint_label = game_ui_make_label(
            s_status_bar, GAME_UI_KEY5_TEXT " 暂停", GAME_UI_COLOR_TEXT,
            game_ui_font_body());
        lv_obj_align(pause_hint_label, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    s_board = lv_obj_create(s_game_screen);
    if (!s_board) {
        game_ui_port_log_i(TAG, "创建游戏页失败: board 为空");
        return;
    }
    lv_obj_set_size(s_board, SNAKE_BOARD_WIDTH * GAME_UI_CELL_PX,
                    SNAKE_BOARD_HEIGHT * GAME_UI_CELL_PX);
    lv_obj_set_pos(s_board, 0, GAME_UI_STATUS_BAR_PX);
    lv_obj_set_style_bg_color(s_board, lv_color_hex(GAME_UI_COLOR_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_border_width(s_board, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_board, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_board, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_board, snake_ui_board_draw_grid, LV_EVENT_DRAW_MAIN,
                        NULL);
    lv_obj_add_event_cb(s_board, snake_ui_board_clicked, LV_EVENT_CLICKED, NULL);

    /* 棋盘内侧边界线：不占用格子尺寸，提醒孩子穿墙会从对面出来。 */
    (void)game_ui_make_rect(s_board, 0, 0,
                            SNAKE_BOARD_WIDTH * GAME_UI_CELL_PX, 2,
                            GAME_UI_COLOR_ACCENT);
    (void)game_ui_make_rect(
        s_board, 0, SNAKE_BOARD_HEIGHT * GAME_UI_CELL_PX - 2,
        SNAKE_BOARD_WIDTH * GAME_UI_CELL_PX, 2, GAME_UI_COLOR_ACCENT);
    (void)game_ui_make_rect(
        s_board, 0, 0, 2, SNAKE_BOARD_HEIGHT * GAME_UI_CELL_PX,
        GAME_UI_COLOR_ACCENT);
    (void)game_ui_make_rect(
        s_board, SNAKE_BOARD_WIDTH * GAME_UI_CELL_PX - 2, 0, 2,
        SNAKE_BOARD_HEIGHT * GAME_UI_CELL_PX, GAME_UI_COLOR_ACCENT);

    /* 蛇段图像按实际蛇长懒创建，避免 540 个隐藏对象耗尽 LVGL 内存池。 */
    s_snake_image_count = 0;
    s_food_image = lv_image_create(s_board);
    if (!s_food_image) {
        game_ui_port_log_i(TAG, "创建游戏页失败: food 图像为空");
        return;
    }
    lv_obj_remove_style_all(s_food_image);
    lv_obj_set_size(s_food_image, GAME_UI_CELL_PX, GAME_UI_CELL_PX);
    lv_image_set_antialias(s_food_image, false);
    lv_image_set_src(s_food_image, &GAME_UI_APPLE_IMAGE);
    lv_obj_add_flag(s_food_image, LV_OBJ_FLAG_HIDDEN);

    /* 暂停面板只覆盖局部, 避免整屏半透明导致蛇身每次位移都全屏重绘. */
    s_pause_overlay = lv_obj_create(s_game_screen);
    lv_obj_set_pos(s_pause_overlay, 100, 48);
    lv_obj_set_size(s_pause_overlay, 280, 220);
    lv_obj_set_scrollbar_mode(s_pause_overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_pause_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_pause_overlay, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_pause_overlay, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pause_overlay, lv_color_hex(GAME_UI_COLOR_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pause_overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pause_overlay, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_pause_overlay,
                                  lv_color_hex(GAME_UI_COLOR_ACCENT),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(s_pause_overlay, 6, LV_PART_MAIN);
    lv_obj_add_flag(s_pause_overlay, LV_OBJ_FLAG_CLICKABLE);
    /* 默认可滚动. 触摸抖动会被当成滑动, 按钮收不到 CLICKED. */
    lv_obj_remove_flag(s_pause_overlay, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t *pause_title = game_ui_make_label(
            s_pause_overlay, "已暂停", GAME_UI_COLOR_ACCENT,
            game_ui_font_body());
        lv_obj_set_width(pause_title, lv_pct(100));
        lv_obj_set_style_text_align(pause_title, LV_TEXT_ALIGN_CENTER,
                                    LV_PART_MAIN);
        s_pause_buttons[0] = game_ui_make_button(s_pause_overlay, "继续", NULL);
        lv_obj_set_width(s_pause_buttons[0], lv_pct(100));
        lv_obj_set_height(s_pause_buttons[0], 40);
        lv_obj_add_event_cb(s_pause_buttons[0], snake_ui_pause_resume_clicked,
                            LV_EVENT_CLICKED, NULL);
        s_pause_buttons[1] = game_ui_make_button(s_pause_overlay, "再来一次",
                                                 NULL);
        lv_obj_set_width(s_pause_buttons[1], lv_pct(100));
        lv_obj_set_height(s_pause_buttons[1], 40);
        lv_obj_add_event_cb(s_pause_buttons[1], snake_ui_pause_retry_clicked,
                            LV_EVENT_CLICKED, NULL);
        s_pause_buttons[2] = game_ui_make_button(s_pause_overlay, "返回", NULL);
        lv_obj_set_width(s_pause_buttons[2], lv_pct(100));
        lv_obj_set_height(s_pause_buttons[2], 40);
        lv_obj_add_event_cb(s_pause_buttons[2], snake_ui_pause_back_clicked,
                            LV_EVENT_CLICKED, NULL);
    }
    s_pause_index = 0U;
    snake_ui_refresh_pause_focus();
    lv_obj_add_flag(s_pause_overlay, LV_OBJ_FLAG_HIDDEN);
    game_ui_port_log_i(TAG, "游戏页创建完成");
}

/**
 * @brief 懒创建结束页.
 *
 * @return 无.
 */
static void snake_ui_create_end(void)
{
    s_end_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_end_screen);
    lv_obj_set_style_pad_all(s_end_screen, 22, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_end_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_end_screen, 12, LV_PART_MAIN);

    s_end_title = game_ui_make_label(s_end_screen, "游戏结束", GAME_UI_COLOR_FOOD,
                                     game_ui_font_title());
    lv_obj_set_width(s_end_title, lv_pct(100));
    lv_obj_set_style_text_align(s_end_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_end_score = game_ui_make_label(s_end_screen, "本局得分：0",
                                     GAME_UI_COLOR_TEXT, game_ui_font_body());
    lv_obj_set_width(s_end_score, lv_pct(100));
    lv_obj_set_style_text_align(s_end_score, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_end_high_score = game_ui_make_label(s_end_screen, "最高分：0",
                                          GAME_UI_COLOR_ACCENT,
                                          game_ui_font_body());
    lv_obj_set_width(s_end_high_score, lv_pct(100));
    lv_obj_set_style_text_align(s_end_high_score, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    s_end_buttons[0] = game_ui_make_button(s_end_screen, "再来一次", NULL);
    lv_obj_set_width(s_end_buttons[0], lv_pct(100));
    lv_obj_set_height(s_end_buttons[0], 52);
    lv_obj_add_event_cb(s_end_buttons[0], snake_ui_retry_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_end_buttons[1] = game_ui_make_button(s_end_screen, "返回设置", NULL);
    lv_obj_set_width(s_end_buttons[1], lv_pct(100));
    lv_obj_set_height(s_end_buttons[1], 48);
    lv_obj_add_event_cb(s_end_buttons[1], snake_ui_back_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_end_index = 0U;
    snake_ui_refresh_end_focus();
}

/**
 * @brief 刷新并显示贪吃蛇设置页.
 *
 * @param user_data 未使用, 仅为对齐 game_ui_port_call 签名.
 * @return 无.
 */
static void snake_ui_render_menu(void *user_data)
{
    const snake_state_t *state;

    (void)user_data;
    if (!s_screen) {
        snake_ui_create_menu();
    }
    /* 标签值缓存: 仅在值变化时重写, 避免重复 set_text_fmt 触发无效化. */
    if (s_menu_shown_speed != s_speed_level) {
        lv_label_set_text_fmt(s_menu_speed_label, "速度：%s",
                              snake_ui_speed_name());
        s_menu_shown_speed = s_speed_level;
    }
    if (s_menu_shown_wrap != (int8_t)s_wrap_enabled) {
        lv_label_set_text_fmt(s_menu_wrap_label, "穿墙：%s",
                              s_wrap_enabled ? "开" : "关");
        s_menu_shown_wrap = (int8_t)s_wrap_enabled;
    }
    if (s_menu_touch_label &&
        s_menu_shown_touch != (int8_t)s_touch_control_enabled) {
        lv_label_set_text_fmt(s_menu_touch_label, "触摸：%s",
                              s_touch_control_enabled ? "开" : "关");
        s_menu_shown_touch = (int8_t)s_touch_control_enabled;
    }
    state = snake_game_state(&s_game);
    {
        int best = state ? state->best_score : 0;

        if (s_menu_shown_best != best) {
            lv_label_set_text_fmt(s_menu_high_score, "最高分：%d", best);
            s_menu_shown_best = best;
        }
    }
    snake_ui_refresh_menu_focus();
    lv_screen_load(s_screen);
}

/**
 * @brief 比较两个格子坐标是否相同.
 *
 * @param a 第一个点.
 * @param b 第二个点.
 * @return 相同为 true.
 */
static bool snake_ui_same_point(snake_point_t a, snake_point_t b)
{
    return a.x == b.x && a.y == b.y;
}

/**
 * @brief 取相反方向.
 *
 * @param direction 当前方向.
 * @return 相反方向.
 */
static snake_direction_t snake_ui_opposite_direction(snake_direction_t direction)
{
    switch (direction) {
        case SNAKE_DIRECTION_UP:
            return SNAKE_DIRECTION_DOWN;
        case SNAKE_DIRECTION_DOWN:
            return SNAKE_DIRECTION_UP;
        case SNAKE_DIRECTION_LEFT:
            return SNAKE_DIRECTION_RIGHT;
        case SNAKE_DIRECTION_RIGHT:
        default:
            return SNAKE_DIRECTION_LEFT;
    }
}

/* 返回相邻节从 from 指向 to 的方向，兼容穿墙跨边界的相邻节。 */
/**
 * @brief 返回相邻节从 from 指向 to 的方向, 兼容穿墙跨边界.
 *
 * @param from 起点格子.
 * @param to 终点格子, 应与 from 相邻或隔墙相邻.
 * @return 四向之一.
 */
static snake_direction_t snake_ui_direction_between(snake_point_t from,
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

/**
 * @brief 取直行身体贴图.
 *
 * @param direction 身体朝向.
 * @return 对应 lv_image 资源指针.
 */
static const lv_image_dsc_t *snake_ui_body_source(snake_direction_t direction)
{
    switch (direction) {
        case SNAKE_DIRECTION_UP:
            return &snake_body_up;
        case SNAKE_DIRECTION_DOWN:
            return &snake_body_down;
        case SNAKE_DIRECTION_LEFT:
            return &snake_body_left;
        case SNAKE_DIRECTION_RIGHT:
        default:
            return &snake_body_right;
    }
}

/**
 * @brief 取蛇头贴图.
 *
 * @param direction 头部朝向.
 * @return 对应 lv_image 资源指针.
 */
static const lv_image_dsc_t *snake_ui_head_source(snake_direction_t direction)
{
    switch (direction) {
        case SNAKE_DIRECTION_UP:
            return &snake_head_up;
        case SNAKE_DIRECTION_DOWN:
            return &snake_head_down;
        case SNAKE_DIRECTION_LEFT:
            return &snake_head_left;
        case SNAKE_DIRECTION_RIGHT:
        default:
            return &snake_head_right;
    }
}

/**
 * @brief 取蛇尾贴图. direction 是尾节指向身体的方向.
 *
 * @param direction 尾节指向身体的方向.
 * @return 对应 lv_image 资源指针.
 */
static const lv_image_dsc_t *snake_ui_tail_source(snake_direction_t direction)
{
    /* direction 是尾节指向身体的方向，资源同名端为粗端。 */
    switch (direction) {
        case SNAKE_DIRECTION_UP:
            return &snake_tail_up;
        case SNAKE_DIRECTION_DOWN:
            return &snake_tail_down;
        case SNAKE_DIRECTION_LEFT:
            return &snake_tail_left;
        case SNAKE_DIRECTION_RIGHT:
        default:
            return &snake_tail_right;
    }
}

/**
 * @brief 按进入和离开方向取转弯贴图.
 *
 * @param a 第一节方向.
 * @param b 第二节方向.
 * @return 对应转弯资源指针.
 */
static const lv_image_dsc_t *snake_ui_turn_source(snake_direction_t a,
                                                  snake_direction_t b)
{
    bool up = a == SNAKE_DIRECTION_UP || b == SNAKE_DIRECTION_UP;
    bool down = a == SNAKE_DIRECTION_DOWN || b == SNAKE_DIRECTION_DOWN;
    bool left = a == SNAKE_DIRECTION_LEFT || b == SNAKE_DIRECTION_LEFT;
    bool right = a == SNAKE_DIRECTION_RIGHT || b == SNAKE_DIRECTION_RIGHT;

    if (up && right) {
        return &snake_turn_up_right;
    }
    if (right && down) {
        return &snake_turn_right_down;
    }
    if (down && left) {
        return &snake_turn_down_left;
    }
    return &snake_turn_left_up;
}

/**
 * @brief 按蛇节位置选择头, 身, 尾或转弯贴图.
 *
 * @param state 当前蛇状态, 为空返回 NULL.
 * @param index 节下标, 0 是头.
 * @return 贴图指针; 越界返回 NULL.
 */
static const lv_image_dsc_t *snake_ui_segment_source(const snake_state_t *state,
                                                     uint16_t index)
{
    if (!state || index >= state->length) {
        return NULL;
    }
    if (index == 0U) {
        return snake_ui_head_source(snake_game_get_direction(&s_game));
    }
    if (index + 1U >= state->length) {
        snake_direction_t toward_head =
            snake_ui_direction_between(state->segments[index],
                                       state->segments[index - 1U]);
        return snake_ui_tail_source(toward_head);
    }
    {
        snake_direction_t toward_head =
            snake_ui_direction_between(state->segments[index],
                                       state->segments[index - 1U]);
        snake_direction_t toward_tail =
            snake_ui_direction_between(state->segments[index],
                                       state->segments[index + 1U]);
        if (snake_ui_opposite_direction(toward_head) == toward_tail) {
            return snake_ui_body_source(toward_head);
        }
        return snake_ui_turn_source(toward_head, toward_tail);
    }
}

/**
 * @brief 计算蛇身扭动偏移. index 0 是头, 越大越靠近尾巴.
 *
 * @param index 蛇节下标.
 * @return 像素偏移, 头节调用方会强制为 0.
 */
static int16_t snake_ui_wiggle_offset_for(uint16_t index)
{
#if GAME_UI_WIGGLE_ENABLE
    const float phase = ((float)s_wiggle_phase_ms * 2.0f * (float)M_PI) /
                        (float)GAME_UI_WIGGLE_CYCLE_MS;
    const float segment_phase =
        ((float)index * (float)GAME_UI_WIGGLE_SEGMENT_PHASE_DEG *
         (float)M_PI) / 180.0f;
    const float value = (float)GAME_UI_WIGGLE_AMPLITUDE_PX *
                        sinf(phase - segment_phase);
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

/**
 * @brief 按需创建第 index 节的图像对象.
 *
 * @param index 蛇节下标, 必须小于 SNAKE_MAX_SEGMENTS.
 * @return 已有或新建成功为 true.
 */
static bool snake_ui_ensure_image(uint16_t index)
{
    lv_obj_t *image;

    if (!s_board || index >= SNAKE_MAX_SEGMENTS) {
        return false;
    }
    if (s_snake_images[index]) {
        return true;
    }
    image = lv_image_create(s_board);
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

/**
 * @brief 判断该节当前是横着走还是竖着走, 用来决定扭动轴向.
 *
 * @param state 当前蛇状态.
 * @param index 节下标.
 * @return 横向为 true.
 */
static bool snake_ui_segment_is_horizontal(const snake_state_t *state,
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
    {
        snake_direction_t direction =
            snake_ui_direction_between(state->segments[index],
                                       state->segments[index - 1U]);
        return direction == SNAKE_DIRECTION_LEFT ||
               direction == SNAKE_DIRECTION_RIGHT;
    }
}

/**
 * @brief 按逻辑状态更新各节贴图, 位置和显隐.
 *
 * @param state 当前蛇状态, 为空则忽略.
 * @return 无.
 */
static void snake_ui_update_snake(const snake_state_t *state)
{
    if (!state || !s_board) {
        return;
    }
    s_wiggle_updated_objects = 0;
    for (uint16_t i = 0; i < state->length; ++i) {
        lv_obj_t *image;
        const lv_image_dsc_t *source;
        int16_t offset;
        int16_t x;
        int16_t y;

        if (!snake_ui_ensure_image(i)) {
            break;
        }
        image = s_snake_images[i];
        if (!image) {
            continue;
        }
        source = snake_ui_segment_source(state, i);
        if (source != s_snake_sources[i]) {
            lv_image_set_src(image, source);
            s_snake_sources[i] = source;
            ++s_wiggle_updated_objects;
        }
        offset = i == 0U ? 0 : snake_ui_wiggle_offset_for(i);
        s_wiggle_offsets[i] = offset;
        x = (int16_t)state->segments[i].x * GAME_UI_CELL_PX;
        y = (int16_t)state->segments[i].y * GAME_UI_CELL_PX;
        if (snake_ui_segment_is_horizontal(state, i)) {
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
/**
 * @brief 得分标签缩放动画回调.
 *
 * @param obj 得分标签.
 * @param value LVGL 变换缩放值.
 * @return 无.
 */
static void snake_ui_score_anim_exec(void *obj, int32_t value)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)obj, value, LV_PART_MAIN);
}

/**
 * @brief 吃到食物时放大状态栏得分标签, 260ms 内回弹.
 *
 * @return 无.
 */
static void snake_ui_animate_score(void)
{
    lv_anim_t anim;

    if (!s_score_label) {
        return;
    }
    lv_anim_delete(s_score_label, snake_ui_score_anim_exec);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_score_label);
    lv_anim_set_exec_cb(&anim, snake_ui_score_anim_exec);
    lv_anim_set_values(&anim, 256, 320);
    lv_anim_set_duration(&anim, 130);
    lv_anim_set_reverse_duration(&anim, 130);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_early_apply(&anim, true);
    lv_anim_start(&anim);
}

/**
 * @brief 食物位置变化时只移动食物图, 不重绘整屏.
 *
 * @param state 当前蛇状态.
 * @return 无.
 */
static void snake_ui_update_decorations(const snake_state_t *state)
{
    if (!state || !s_board || !s_food_image) {
        return;
    }

    if (!snake_ui_same_point(state->food, s_rendered_food)) {
        int32_t food_x = (int32_t)state->food.x * GAME_UI_CELL_PX;
        int32_t food_y = (int32_t)state->food.y * GAME_UI_CELL_PX;
        lv_obj_set_pos(s_food_image, food_x, food_y);
        lv_obj_clear_flag(s_food_image, LV_OBJ_FLAG_HIDDEN);
        s_rendered_food = state->food;
    }
}

/**
 * @brief 刷新蛇身和食物装饰.
 *
 * @return 无.
 */
static void snake_ui_update_board(void)
{
    const snake_state_t *state = snake_game_state(&s_game);

    if (!state) {
        return;
    }
    snake_ui_update_snake(state);
    snake_ui_update_decorations(state);
}

/**
 * @brief 刷新对局页: 棋盘, 分数, 最高分, 速度和暂停弹窗.
 *
 * @param user_data 未使用, 仅为对齐 game_ui_port_call 签名.
 * @return 无.
 */
static void snake_ui_render_game(void *user_data)
{
    const snake_state_t *state;
    int score;
    int speed_ms;
    bool paused;

    (void)user_data;
    if (!s_game_screen) {
        snake_ui_create_game();
    }
    if (!s_game_screen || !s_board) {
        game_ui_port_log_i(TAG, "渲染游戏页中止: screen=%p board=%p",
                           (void *)s_game_screen, (void *)s_board);
        return;
    }
    snake_ui_update_board();
    state = snake_game_state(&s_game);
    score = state ? state->score : 0;
    speed_ms = state ? (int)state->speed_ms : (int)SNAKE_SPEED_SLOW_MS;
    paused = state && state->paused;
    if (score != s_rendered_score) {
        lv_label_set_text_fmt(s_score_label, "得分 %d", score);
        if (s_rendered_score >= 0 && score > s_rendered_score) {
            snake_ui_animate_score();
        }
        s_rendered_score = score;
        if (s_rendered_best >= 0) {
            snake_ui_refresh_best_label(s_rendered_best);
        }
    }
    {
        int best_score = state ? state->best_score : 0;

        if (best_score != s_rendered_best) {
            snake_ui_refresh_best_label(best_score);
            s_rendered_best = best_score;
        }
    }
    if (speed_ms != s_rendered_speed_ms) {
        uint16_t level = snake_ui_speed_display_level((uint16_t)speed_ms);
        lv_label_set_text_fmt(s_speed_label, "速度 %u", (unsigned)level);
        s_rendered_speed_ms = speed_ms;
        game_ui_port_log_i(TAG, "速度更新: 档%u 间隔%dms foods=%lu",
                           (unsigned)level, speed_ms,
                           state ? (unsigned long)state->foods_eaten : 0UL);
    }
    /* 弹窗每次按暂停态同步. 标志已是目标值时 LVGL 直接返回. */
    if (s_pause_overlay) {
        if (paused) {
            lv_obj_clear_flag(s_pause_overlay, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_pause_overlay, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (paused != s_rendered_paused) {
        for (uint16_t i = 0; i < s_snake_image_count; ++i) {
            if (s_snake_images[i]) {
                lv_obj_set_style_opa(s_snake_images[i],
                                     paused ? LV_OPA_50 : LV_OPA_COVER,
                                     LV_PART_MAIN);
            }
        }
        s_rendered_paused = paused;
    }
    if (paused) {
        snake_ui_refresh_pause_focus();
    }
    if (lv_screen_active() != s_game_screen) {
        lv_screen_load(s_game_screen);
    }
    s_wiggle_dirty = false;
}

/**
 * @brief 刷新并显示结束页文案和分数.
 *
 * @param user_data 未使用, 仅为对齐 game_ui_port_call 签名.
 * @return 无.
 */
static void snake_ui_render_end(void *user_data)
{
    const snake_state_t *state = snake_game_state(&s_game);
    const char *end_message = "游戏结束";

    (void)user_data;
    if (!s_end_screen) {
        snake_ui_create_end();
    }
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
    /* 结束页内容缓存: 原因/得分/最高分之一变化才重填, 焦点移动不再整页重写. */
    {
        int reason = state ? (int)state->game_over_reason : -1;
        int score = state ? state->score : 0;
        int best = state ? state->best_score : 0;

        if (reason != s_end_shown_reason || score != s_end_shown_score ||
            best != s_end_shown_best) {
            lv_label_set_text(s_end_title, end_message);
            lv_label_set_text_fmt(s_end_score, "本局得分：%d", score);
            lv_label_set_text_fmt(s_end_high_score, "最高分：%d", best);
            s_end_shown_reason = reason;
            s_end_shown_score = score;
            s_end_shown_best = best;
        }
    }
    snake_ui_refresh_end_focus();
    lv_screen_load(s_end_screen);
}

/**
 * @brief 推进扭动相位; 达到刷新间隔后置脏标记.
 *
 * @param elapsed_ms 距上次调用的毫秒数.
 * @return 无.
 */
static void snake_ui_advance_wiggle(uint32_t elapsed_ms)
{
#if GAME_UI_WIGGLE_ENABLE
    if (s_page != SNAKE_UI_PAGE_GAME || elapsed_ms == 0U) {
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

/**
 * @brief 初始化贪吃蛇逻辑和页状态, 不创建 LVGL 对象.
 *
 * @param on_back 返回游戏选择页的回调, 可为空.
 * @return 无.
 */
void snake_ui_init(game_ui_back_cb_t on_back)
{
    snake_config_t config;

    s_on_back = on_back;
    snake_config_default(&config);
    config.load_best = snake_ui_load_best;
    config.save_best = snake_ui_save_best;
    config.storage_ctx = NULL;
    config.wrap_walls = true;
    config.initial_speed_ms = SNAKE_SPEED_SLOW_MS;
    config.width = SNAKE_BOARD_WIDTH;
    config.height = SNAKE_BOARD_HEIGHT;
    snake_game_init(&s_game, &config);
    s_page = SNAKE_UI_PAGE_MENU;
    s_menu_index = 0U;
    s_end_index = 0U;
    s_pause_index = 0U;
}

/**
 * @brief 进入贪吃蛇设置页并请求外壳重绘.
 *
 * @return 无.
 */
void snake_ui_enter_menu(void)
{
    s_menu_index = 0U;
    s_page = SNAKE_UI_PAGE_MENU;
    game_ui_request_render();
    game_ui_port_log_i(TAG, "进入贪吃蛇设置");
}

/**
 * @brief 按当前内部页渲染设置, 对局或结束页.
 *
 * @param user_data 透传给具体渲染函数.
 * @return 无.
 */
void snake_ui_render(void *user_data)
{
    if (s_page == SNAKE_UI_PAGE_GAME &&
        snake_game_get_status(&s_game) == SNAKE_STATUS_GAME_OVER) {
        s_page = SNAKE_UI_PAGE_END;
        s_end_index = 0U;
        game_ui_port_log_i(TAG, "进入结束页 reason=%s score=%d best=%d",
                           snake_ui_over_reason_name(s_game.state.game_over_reason),
                           s_game.state.score, s_game.state.best_score);
    }
    if (s_page == SNAKE_UI_PAGE_GAME) {
        snake_ui_render_game(user_data);
        return;
    }
    if (s_page == SNAKE_UI_PAGE_END) {
        snake_ui_render_end(user_data);
        return;
    }
    snake_ui_render_menu(user_data);
}

/**
 * @brief 处理实体键: 菜单, 结束页, 暂停或对局转向.
 *
 * @param key 实体键编号, 与 GAME_UI_KEY_* 对应.
 * @param type 菜单, 结束页和暂停弹窗只响应 PRESS. 对局里方向键仍接受连发.
 * @return 无.
 */
void snake_ui_handle_key(uint8_t key, ad_keys_event_type_t type)
{
    if (s_page == SNAKE_UI_PAGE_MENU) {
        if (type != AD_KEYS_EVENT_PRESS) {
            return;
        }
        if (key == GAME_UI_KEY_UP || key == GAME_UI_KEY_LEFT) {
            game_ui_move_index(&s_menu_index, GAME_UI_MENU_ITEM_COUNT, -1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_DOWN || key == GAME_UI_KEY_RIGHT) {
            game_ui_move_index(&s_menu_index, GAME_UI_MENU_ITEM_COUNT, 1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_PAUSE) {
            snake_ui_activate_menu();
        }
        return;
    }
    if (s_page == SNAKE_UI_PAGE_END) {
        if (type != AD_KEYS_EVENT_PRESS) {
            return;
        }
        if (key == GAME_UI_KEY_UP || key == GAME_UI_KEY_LEFT) {
            game_ui_move_index(&s_end_index, GAME_UI_END_ITEM_COUNT, -1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_DOWN || key == GAME_UI_KEY_RIGHT) {
            game_ui_move_index(&s_end_index, GAME_UI_END_ITEM_COUNT, 1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_PAUSE) {
            if (s_end_index == 1U) {
                s_menu_index = 0U;
                s_page = SNAKE_UI_PAGE_MENU;
                game_ui_request_render();
                game_ui_port_log_i(TAG, "结束页回设置");
            } else {
                snake_ui_retry_game();
            }
        }
        return;
    }
    if (snake_game_get_status(&s_game) == SNAKE_STATUS_PAUSED) {
        if (type != AD_KEYS_EVENT_PRESS) {
            return;
        }
        if (key == GAME_UI_KEY_UP || key == GAME_UI_KEY_LEFT) {
            game_ui_move_index(&s_pause_index, GAME_UI_PAUSE_ITEM_COUNT, -1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_DOWN || key == GAME_UI_KEY_RIGHT) {
            game_ui_move_index(&s_pause_index, GAME_UI_PAUSE_ITEM_COUNT, 1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_PAUSE) {
            snake_ui_activate_pause();
        }
        return;
    }
    if (key == GAME_UI_KEY_PAUSE) {
        if (type != AD_KEYS_EVENT_PRESS) {
            return;
        }
        s_pause_index = 0U;
        game_ui_port_log_i(TAG, "游戏内暂停");
    }
    snake_game_set_input(&s_game, snake_ui_input_from_key(key));
}

/**
 * @brief 推进贪吃蛇对局和扭动相位, 状态变化时请求重绘.
 *
 * @param elapsed_ms 距上次调用的毫秒数.
 * @return 无.
 */
void snake_ui_advance(uint32_t elapsed_ms)
{
    const snake_state_t *before;
    snake_point_t before_head = {UINT8_MAX, UINT8_MAX};
    snake_point_t before_food = {UINT8_MAX, UINT8_MAX};
    uint16_t before_length = 0;
    int before_score = 0;
    bool before_paused = false;
    bool before_game_over = false;
    snake_direction_t before_direction;
    const snake_state_t *after;
    snake_point_t after_head = {UINT8_MAX, UINT8_MAX};
    snake_direction_t after_direction;
    bool changed;

    if (s_page != SNAKE_UI_PAGE_GAME) {
        return;
    }
    before = snake_game_state(&s_game);
    before_direction = snake_game_get_direction(&s_game);
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
    after = snake_game_state(&s_game);
    if (after && after->length > 0U) {
        after_head = after->segments[0];
    }
    after_direction = snake_game_get_direction(&s_game);
    changed = !snake_ui_same_point(before_head, after_head) ||
        before_food.x != (after ? after->food.x : UINT8_MAX) ||
        before_food.y != (after ? after->food.y : UINT8_MAX) ||
        before_length != (after ? after->length : 0U) ||
        before_score != (after ? after->score : 0) ||
        before_paused != (after ? after->paused : false) ||
        before_game_over != (after ? after->game_over : false) ||
        before_direction != after_direction;
    if (changed) {
        game_ui_request_render();
    }
    /*
     * 普通逐格移动不记录；只在方向、得分、暂停或终局等可诊断状态
     * 变化时输出，避免 20ms 任务把串口刷满。
     */
    if (after && before_direction != after_direction) {
        game_ui_port_log_i(TAG, "转向 direction=%s head=(%u,%u)",
                           snake_ui_direction_name(after_direction),
                           (unsigned)after_head.x, (unsigned)after_head.y);
    }
    if (after && before_score != after->score) {
        game_ui_port_log_i(
            TAG,
            "吃到果子 score=%d length=%u speed_ms=%u foods=%lu "
            "head=(%u,%u) next_food=(%u,%u)",
            after->score, (unsigned)after->length, (unsigned)after->speed_ms,
            (unsigned long)after->foods_eaten, (unsigned)after_head.x,
            (unsigned)after_head.y, (unsigned)after->food.x,
            (unsigned)after->food.y);
    }
    if (after && before_paused != after->paused) {
        game_ui_port_log_i(TAG, "暂停状态 changed=%u score=%d head=(%u,%u)",
                           after->paused ? 1U : 0U, after->score,
                           (unsigned)after_head.x, (unsigned)after_head.y);
    }
    if (after && !before_game_over && after->game_over) {
        game_ui_port_log_i(
            TAG,
            "游戏结束 reason=%s score=%d best=%d length=%u head=(%u,%u)",
            snake_ui_over_reason_name(after->game_over_reason), after->score,
            after->best_score, (unsigned)after->length,
            (unsigned)after_head.x, (unsigned)after_head.y);
    }
    snake_ui_advance_wiggle(elapsed_ms);
}

/**
 * @brief 查询蛇身扭动是否需要刷新位置.
 *
 * @return 有脏标记为 true.
 */
bool snake_ui_wiggle_dirty(void)
{
    return s_wiggle_dirty;
}

/**
 * @brief 清掉扭动脏标记.
 *
 * @return 无.
 */
void snake_ui_clear_wiggle_dirty(void)
{
    s_wiggle_dirty = false;
}

/**
 * @brief 取当前贪吃蛇页诊断名.
 *
 * @return "menu", "game", "paused" 或 "end".
 */
const char *snake_ui_page_name(void)
{
    if (s_page == SNAKE_UI_PAGE_GAME &&
        snake_game_get_status(&s_game) == SNAKE_STATUS_PAUSED) {
        return "paused";
    }
    if (s_page == SNAKE_UI_PAGE_GAME) {
        return "game";
    }
    if (s_page == SNAKE_UI_PAGE_END) {
        return "end";
    }
    return "menu";
}

/**
 * @brief 取贪吃蛇逻辑只读快照.
 *
 * @return 逻辑状态指针.
 */
const snake_state_t *snake_ui_state(void)
{
    return snake_game_state(&s_game);
}

/**
 * @brief 测试辅助: 把食物放到指定格子.
 *
 * @param food 目标坐标.
 * @return 放置成功为 true.
 */
bool snake_ui_force_food(snake_point_t food)
{
    bool ok = snake_game_force_food(&s_game, food);

    game_ui_port_log_i(TAG, "测试放置食物 (%u,%u) result=%u",
                       (unsigned)food.x, (unsigned)food.y, ok ? 1U : 0U);
    return ok;
}

/**
 * @brief 测试辅助: 切到对局页并摆出自撞形状.
 *
 * @return 无.
 */
void snake_ui_force_self_collision(void)
{
    s_page = SNAKE_UI_PAGE_GAME;
    s_game.state.length = 4;
    s_game.state.segments[0] = (snake_point_t){10, 10};
    s_game.state.segments[1] = (snake_point_t){9, 10};
    s_game.state.segments[2] = (snake_point_t){10, 9};
    s_game.state.segments[3] = (snake_point_t){11, 9};
    s_game.direction = SNAKE_DIRECTION_UP;
    s_game.pending_direction = SNAKE_DIRECTION_UP;
    s_game.state.paused = false;
    s_game.state.game_over = false;
    game_ui_request_render();
    game_ui_port_log_i(TAG, "测试构造自撞 head=(%u,%u) length=%u",
                       (unsigned)s_game.state.segments[0].x,
                       (unsigned)s_game.state.segments[0].y,
                       (unsigned)s_game.state.length);
}

/**
 * @brief 取蛇身扭动相位.
 *
 * @return 当前周期内的毫秒相位.
 */
uint32_t snake_ui_get_wiggle_phase_ms(void)
{
    return s_wiggle_phase_ms;
}

/**
 * @brief 取某一节最近一次算出的扭动偏移.
 *
 * @param segment_index 蛇节下标, 0 是头.
 * @return 像素偏移; 越界返回 0.
 */
int16_t snake_ui_get_wiggle_offset(uint16_t segment_index)
{
    if (segment_index >= SNAKE_MAX_SEGMENTS) {
        return 0;
    }
    return s_wiggle_offsets[segment_index];
}

/**
 * @brief 取上一帧实际改过的蛇节对象数.
 *
 * @return 更新过的 LVGL 对象个数.
 */
uint16_t snake_ui_get_wiggle_updated_objects(void)
{
    return s_wiggle_updated_objects;
}
