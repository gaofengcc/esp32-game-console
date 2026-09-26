#include "klotski_ui.h"

#include "game_ui_port.h"
#include "lvgl.h"
#include "assets/star_24x24_gold.h"
#include "assets/star_24x24_gray.h"

/* 布局: 状态栏 32px, 棋盘横向显示 (逻辑 4 列 x 5 行转置为显示 5 列 x 4 行,
   每格 64px, 320x256), 出口在右侧中间, 右侧信息栏 136px.
   显示坐标 = 逻辑坐标转置: 显示 x = 逻辑 y, 显示 y = 逻辑 x,
   棋子显示宽高互换, 方向键同步映射, 逻辑层不感知旋转 */
#define KLOTSKI_UI_CELL_PX 64
#define KLOTSKI_UI_BOARD_X 8
#define KLOTSKI_UI_BOARD_Y 40
#define KLOTSKI_UI_BOARD_W (KLOTSKI_UI_CELL_PX * 5)
#define KLOTSKI_UI_BOARD_H (KLOTSKI_UI_CELL_PX * 4)
#define KLOTSKI_UI_PANEL_X 336
#define KLOTSKI_UI_PANEL_W 136
#define KLOTSKI_UI_PIECE_PAD 3

/* 卡通交互参数：只影响显示动画，不改变华容道步数口径。 */
#define KLOTSKI_UI_ANIM_MS 110U
#define KLOTSKI_UI_BLINK_MS 450U
#define KLOTSKI_UI_WIN_DELAY_MS 700U

/* 选关列表: 24 关 + 返回。 */
#define KLOTSKI_UI_SELECT_ITEM_COUNT (KLOTSKI_LEVEL_COUNT + 1U)
#define KLOTSKI_UI_PAUSE_ITEM_COUNT 3U
#define KLOTSKI_UI_WIN_ITEM_COUNT 3U

/* 每关最佳成绩持久化: 单个 blob, 0xFFFF 表示未通关。 */
#define KLOTSKI_UI_BEST_KEY "klotski_best"
#define KLOTSKI_UI_BEST_NONE 0xFFFFU

/* 胜利面板底色, 与 tools/generate_klotski_stars.py 烘焙的星星底色一致 */
#define KLOTSKI_UI_WIN_PANEL_BG 0xFFF3D6

/* 卡通棋子配色: 底色 / 描边 / 文字 */
typedef struct {
    uint32_t bg;
    uint32_t edge;
    uint32_t text;
} klotski_ui_skin_t;

static const klotski_ui_skin_t s_skins[KLOTSKI_PIECE_COUNT] = {
    {0xEF5350, 0xB71C1C, 0xFFFFFF}, /* 曹操 红 */
    {0x66BB6A, 0x2E7D32, 0xFFFFFF}, /* 关羽 绿 */
    {0xAB47BC, 0x6A1B9A, 0xFFFFFF}, /* 张飞 紫 */
    {0x42A5F5, 0x1565C0, 0xFFFFFF}, /* 赵云 蓝 */
    {0xFFA726, 0xE65100, 0xFFFFFF}, /* 马超 橙 */
    {0x26C6DA, 0x00838F, 0xFFFFFF}, /* 黄忠 青 */
    {0xFFEE58, 0xF9A825, 0x5D4037}, /* 兵 黄 */
    {0xFFEE58, 0xF9A825, 0x5D4037},
    {0xFFEE58, 0xF9A825, 0x5D4037},
    {0xFFEE58, 0xF9A825, 0x5D4037},
};

/* 选关按钮按难度分底色 */
static const uint32_t s_tier_bg[3] = {0x2E4A38, 0x26334A, 0x4A2A30};
/* 选关文字色: 未通关灰, 通关后按星级 */
#define KLOTSKI_UI_COLOR_LOCKED 0x78909C
#define KLOTSKI_UI_COLOR_STAR1 0xE8F1F2
#define KLOTSKI_UI_COLOR_STAR2 0x4DD0E1
#define KLOTSKI_UI_COLOR_STAR3 0xFFD54F
#define KLOTSKI_UI_COLOR_LOCKED_BG 0x1B2830
/* 选关行右侧三星: 24px 原图, 行宽 = 3*24 + 2*2 间距 */
#define KLOTSKI_UI_SELECT_STAR_ROW_W 76
#define KLOTSKI_UI_SELECT_STAR_ROW_H 24
#define KLOTSKI_UI_SELECT_BTN_H 40

typedef enum {
    KLOTSKI_UI_PAGE_SELECT = 0,
    KLOTSKI_UI_PAGE_GAME,
} klotski_ui_page_t;

static const char *TAG = "klotski_ui";
static klotski_game_t s_game;
static klotski_ui_page_t s_page;
static game_ui_back_cb_t s_on_back;
static uint16_t s_best[KLOTSKI_LEVEL_COUNT];

/* 对局交互状态，只在 game_ui 任务读写。 */
static uint8_t s_cursor_x;
static uint8_t s_cursor_y;
static int8_t s_selected;
static uint8_t s_select_index;
static uint8_t s_pause_index;
static uint8_t s_win_index;
static bool s_pause_visible;
static bool s_win_visible;
static bool s_win_pending;
static bool s_win_filled;
static bool s_new_record;
static uint32_t s_win_elapsed_ms;
static uint32_t s_blink_ms;
static bool s_blink_on;
/* 最近一次被阻挡的日志时间，防止长按方向键刷屏。 */
static uint32_t s_last_block_log_ms;
static bool s_block_log_valid;

/* LVGL 对象, 只在 LVGL 线程 (render 路径) 访问 */
static lv_obj_t *s_select_screen;
static lv_obj_t *s_select_list;
static lv_obj_t *s_select_buttons[KLOTSKI_UI_SELECT_ITEM_COUNT];
static lv_obj_t *s_select_labels[KLOTSKI_UI_SELECT_ITEM_COUNT];
/* 每关右侧 3 颗星, 只在选关页 LVGL 线程访问 */
static lv_obj_t *s_select_stars[KLOTSKI_LEVEL_COUNT][3];
static lv_obj_t *s_game_screen;
static lv_obj_t *s_status_label;
static lv_obj_t *s_steps_label;
static lv_obj_t *s_min_label;
static lv_obj_t *s_best_label;
static lv_obj_t *s_board;
static lv_obj_t *s_pieces[KLOTSKI_PIECE_COUNT];
static lv_obj_t *s_cursor;
static lv_obj_t *s_pause_overlay;
static lv_obj_t *s_pause_buttons[KLOTSKI_UI_PAUSE_ITEM_COUNT];
static lv_obj_t *s_win_overlay;
static lv_obj_t *s_win_stars[3];
static lv_obj_t *s_win_info;
static lv_obj_t *s_win_record;
static lv_obj_t *s_win_buttons[KLOTSKI_UI_WIN_ITEM_COUNT];
static lv_obj_t *s_win_next_label;
/* 棋子在屏幕上的已显示 anchor, 用于判断是否需要补动画 */
static uint8_t s_disp_x[KLOTSKI_PIECE_COUNT];
static uint8_t s_disp_y[KLOTSKI_PIECE_COUNT];
static bool s_disp_valid;
/* 信息栏标签的上次显示值: lv_label_set_text_fmt 无旧值比较,
   每次调用都会重排版并无效化, 必须值变化才更新 */
static uint8_t s_label_level;
static uint16_t s_label_steps;
static uint16_t s_label_best;
/* 样式应用缓存: LVGL v9 的 lv_obj_set_style_* 无旧值比较,
   每次调用都无条件无效化对象, 必须只在状态变化时设置 */
static bool s_cursor_blink_applied;
static int8_t s_highlight_applied;
static uint8_t s_pause_focus_shown;
static uint8_t s_win_focus_shown;

static void klotski_ui_render_select(void *user_data);
static void klotski_ui_render_game(void *user_data);

/**
 * @brief 把华容道方向换成日志短名称.
 *
 * @param dir 逻辑方向.
 * @return 静态字符串.
 */
static const char *klotski_ui_dir_name(klotski_dir_t dir)
{
    switch (dir) {
        case KLOTSKI_DIR_UP:
            return "up";
        case KLOTSKI_DIR_DOWN:
            return "down";
        case KLOTSKI_DIR_LEFT:
            return "left";
        case KLOTSKI_DIR_RIGHT:
        default:
            return "right";
    }
}

/* 触摸回调, 定义在文件末尾, 由 LVGL 线程触发 */
static void klotski_ui_level_clicked(lv_event_t *event);
static void klotski_ui_back_clicked(lv_event_t *event);
static void klotski_ui_menu_clicked(lv_event_t *event);
static void klotski_ui_pause_resume_clicked(lv_event_t *event);
static void klotski_ui_pause_retry_clicked(lv_event_t *event);
static void klotski_ui_pause_back_clicked(lv_event_t *event);
static void klotski_ui_win_next_clicked(lv_event_t *event);
static void klotski_ui_win_retry_clicked(lv_event_t *event);
static void klotski_ui_win_back_clicked(lv_event_t *event);

/**
 * @brief 取难度档中文名.
 *
 * @param tier 难度档.
 * @return 静态字符串, 未知值按简单处理.
 */
static const char *klotski_ui_tier_name(uint8_t tier)
{
    if (tier == (uint8_t)KLOTSKI_TIER_NORMAL) {
        return "中等";
    }
    if (tier == (uint8_t)KLOTSKI_TIER_MASTER) {
        return "困难";
    }
    return "简单";
}

/**
 * @brief 按最佳步数相对最少步数换算通关星级.
 *
 * @param best 最佳步数, KLOTSKI_UI_BEST_NONE 表示未通关.
 * @param min_steps 关卡最少步数.
 * @return 0 未通关; 1~3 星.
 */
static uint8_t klotski_ui_stars_for_best(uint16_t best, uint16_t min_steps)
{
    if (best == KLOTSKI_UI_BEST_NONE) {
        return 0U;
    }
    if (min_steps > 0U && best <= min_steps) {
        return 3U;
    }
    if (min_steps > 0U && best <= (uint16_t)(min_steps * 3U / 2U)) {
        return 2U;
    }
    return 1U;
}

/**
 * @brief 取通关星级对应的文字颜色.
 *
 * @param stars 0 未通关, 1~3 星.
 * @return RGB888.
 */
static uint32_t klotski_ui_stars_color(uint8_t stars)
{
    if (stars >= 3U) {
        return KLOTSKI_UI_COLOR_STAR3;
    }
    if (stars == 2U) {
        return KLOTSKI_UI_COLOR_STAR2;
    }
    if (stars == 1U) {
        return KLOTSKI_UI_COLOR_STAR1;
    }
    return KLOTSKI_UI_COLOR_LOCKED;
}

/**
 * @brief 加载全部关卡的最佳成绩, 无记录时全部标记未通关.
 *
 * @return 无.
 */
static void klotski_ui_load_best(void)
{
    uint8_t i;

    if (game_ui_port_load_blob(KLOTSKI_UI_BEST_KEY, s_best, sizeof(s_best)) !=
        0) {
        for (i = 0U; i < KLOTSKI_LEVEL_COUNT; ++i) {
            s_best[i] = KLOTSKI_UI_BEST_NONE;
        }
    }
}

/**
 * @brief 写入全部关卡最佳成绩, 失败只打日志不阻塞游戏.
 *
 * @return 无.
 */
static void klotski_ui_save_best(void)
{
    if (game_ui_port_save_blob(KLOTSKI_UI_BEST_KEY, s_best, sizeof(s_best)) !=
        0) {
        game_ui_port_log_i(TAG, "最佳成绩保存失败");
    }
}

/**
 * @brief 进入指定关卡并切到对局页, 光标落在曹操上.
 *
 * @param level 关卡下标, 调用前必须保证合法.
 * @return 无.
 */
static void klotski_ui_begin_level(uint8_t level)
{
    const klotski_level_def_t *def;

    if (!klotski_game_load_level(&s_game, level)) {
        game_ui_port_log_i(TAG, "关卡 %u 加载失败", (unsigned)level);
        return;
    }
    s_page = KLOTSKI_UI_PAGE_GAME;
    s_cursor_x = s_game.state.pieces[KLOTSKI_PIECE_CAO].x;
    s_cursor_y = s_game.state.pieces[KLOTSKI_PIECE_CAO].y;
    s_selected = -1;
    s_pause_visible = false;
    s_win_visible = false;
    s_win_pending = false;
    s_win_filled = false;
    s_new_record = false;
    s_pause_index = 0U;
    s_win_index = 0U;
    s_disp_valid = false;
    s_last_block_log_ms = 0U;
    s_block_log_valid = false;
    game_ui_request_render();
    def = klotski_level_def(level);
    game_ui_port_log_i(TAG, "进入华容道第 %u 关 name=%s tier=%u min_steps=%u "
                       "cao=(%u,%u) best=%u",
                       (unsigned)(level + 1U), def ? def->name : "?",
                       def ? (unsigned)def->tier : 0U,
                       def ? (unsigned)def->min_steps : 0U,
                       (unsigned)s_game.state.pieces[KLOTSKI_PIECE_CAO].x,
                       (unsigned)s_game.state.pieces[KLOTSKI_PIECE_CAO].y,
                       (unsigned)s_best[level]);
}

/**
 * @brief 返回选关页.
 *
 * @return 无.
 */
static void klotski_ui_goto_select(void)
{
    s_page = KLOTSKI_UI_PAGE_SELECT;
    s_pause_visible = false;
    s_win_visible = false;
    s_win_pending = false;
    game_ui_request_render();
    game_ui_port_log_i(TAG, "返回华容道选关");
}

/**
 * @brief 通过外壳回调回到游戏选择页.
 *
 * @return 无.
 */
static void klotski_ui_goto_home(void)
{
    if (s_on_back) {
        s_on_back();
    }
}

/**
 * @brief 按星级刷新某关右侧三星图标. 只在 LVGL 线程调用.
 *
 * @param level 关卡下标.
 * @param stars 0 未通关, 1~3 星.
 * @return 无.
 */
static void klotski_ui_refresh_select_stars(uint8_t level, uint8_t stars)
{
    uint8_t j;

    if (level >= KLOTSKI_LEVEL_COUNT) {
        return;
    }
    for (j = 0U; j < 3U; ++j) {
        if (!s_select_stars[level][j]) {
            continue;
        }
        lv_image_set_src(s_select_stars[level][j],
                         j < stars ? &star_24x24_gold : &star_24x24_gray);
    }
}

/**
 * @brief 刷新选关列表按钮文案, 颜色, 星星和焦点. 只在 LVGL 线程调用.
 *
 * @return 无.
 */
static void klotski_ui_refresh_select(void)
{
    uint8_t i;

    for (i = 0U; i < KLOTSKI_LEVEL_COUNT; ++i) {
        const klotski_level_def_t *def = klotski_level_def(i);
        uint8_t stars;
        uint32_t color;
        uint8_t tier;

        if (!def || !s_select_labels[i] || !s_select_buttons[i]) {
            continue;
        }
        tier = def->tier % 3U;
        stars = klotski_ui_stars_for_best(s_best[i], def->min_steps);
        color = klotski_ui_stars_color(stars);
        /* 文案只保留关号, 关名和难度; 通关状态用颜色和右侧星星表达. */
        lv_label_set_text_fmt(s_select_labels[i], "第%u关  %s  %s",
                              (unsigned)(i + 1U), def->name,
                              klotski_ui_tier_name(def->tier));
        lv_obj_set_style_text_color(s_select_labels[i], lv_color_hex(color),
                                    LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            s_select_buttons[i],
            lv_color_hex(stars == 0U ? KLOTSKI_UI_COLOR_LOCKED_BG
                                     : s_tier_bg[tier]),
            LV_PART_MAIN);
        klotski_ui_refresh_select_stars(i, stars);
    }
    for (i = 0U; i < KLOTSKI_UI_SELECT_ITEM_COUNT; ++i) {
        game_ui_set_button_focus(s_select_buttons[i], i == s_select_index);
    }
    if (s_select_buttons[s_select_index]) {
        lv_obj_scroll_to_view(s_select_buttons[s_select_index], LV_ANIM_OFF);
    }
}

/**
 * @brief 刷新暂停弹窗按钮焦点, 只更新失焦和新聚焦的按钮. 只在 LVGL 线程调用.
 *
 * @return 无.
 */
static void klotski_ui_refresh_pause_focus(void)
{
    uint8_t prev = s_pause_focus_shown;

    if (prev == s_pause_index) {
        return;
    }
    if (prev < KLOTSKI_UI_PAUSE_ITEM_COUNT && s_pause_buttons[prev]) {
        game_ui_set_button_focus(s_pause_buttons[prev], false);
    }
    if (s_pause_buttons[s_pause_index]) {
        game_ui_set_button_focus(s_pause_buttons[s_pause_index], true);
    }
    s_pause_focus_shown = s_pause_index;
}

/**
 * @brief 刷新胜利弹窗按钮焦点, 只更新失焦和新聚焦的按钮. 只在 LVGL 线程调用.
 *
 * @return 无.
 */
static void klotski_ui_refresh_win_focus(void)
{
    uint8_t prev = s_win_focus_shown;

    if (prev == s_win_index) {
        return;
    }
    if (prev < KLOTSKI_UI_WIN_ITEM_COUNT && s_win_buttons[prev]) {
        game_ui_set_button_focus(s_win_buttons[prev], false);
    }
    if (s_win_buttons[s_win_index]) {
        game_ui_set_button_focus(s_win_buttons[s_win_index], true);
    }
    s_win_focus_shown = s_win_index;
}

/**
 * @brief 棋子锚点换成棋盘上的像素坐标 (横向显示: 逻辑与显示转置).
 *
 * @param x 逻辑锚点列.
 * @param y 逻辑锚点行.
 * @param px 输出像素 X, 不可为空.
 * @param py 输出像素 Y, 不可为空.
 * @return 无.
 */
static void klotski_ui_piece_px(uint8_t x, uint8_t y, int32_t *px, int32_t *py)
{
    *px = (int32_t)y * KLOTSKI_UI_CELL_PX + KLOTSKI_UI_PIECE_PAD;
    *py = (int32_t)x * KLOTSKI_UI_CELL_PX + KLOTSKI_UI_PIECE_PAD;
}

/**
 * @brief 让棋子对象带卡通回弹动画滑到目标格. 只在 LVGL 线程调用.
 *
 * @param obj 棋子对象.
 * @param x 目标像素 X.
 * @param y 目标像素 Y.
 * @return 无.
 */
static void klotski_ui_anim_piece(lv_obj_t *obj, int32_t x, int32_t y)
{
    lv_anim_t anim;

    if (!obj) {
        return;
    }
    lv_anim_delete(obj, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_delete(obj, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_duration(&anim, KLOTSKI_UI_ANIM_MS);
    lv_anim_set_path_cb(&anim, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_values(&anim, lv_obj_get_x(obj), x);
    lv_anim_start(&anim);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&anim, lv_obj_get_y(obj), y);
    lv_anim_start(&anim);
}

/**
 * @brief 按逻辑状态同步棋子位置, 变化的棋子补滑动动画. 只在 LVGL 线程调用.
 *
 * @return 无.
 */
static void klotski_ui_sync_pieces(void)
{
    uint8_t i;

    for (i = 0U; i < KLOTSKI_PIECE_COUNT; ++i) {
        const klotski_piece_t *piece = &s_game.state.pieces[i];
        int32_t px;
        int32_t py;

        if (!s_pieces[i]) {
            continue;
        }
        klotski_ui_piece_px(piece->x, piece->y, &px, &py);
        if (!s_disp_valid || s_disp_x[i] != piece->x ||
            s_disp_y[i] != piece->y) {
            if (s_disp_valid) {
                klotski_ui_anim_piece(s_pieces[i], px, py);
            } else {
                lv_obj_set_pos(s_pieces[i], px, py);
            }
            s_disp_x[i] = piece->x;
            s_disp_y[i] = piece->y;
        }
    }
    s_disp_valid = true;
}

/**
 * @brief 刷新光标位置和选中棋子的卡通高亮. 只在 LVGL 线程调用.
 *
 * @return 无.
 */
static void klotski_ui_sync_cursor(void)
{
    uint8_t i;

    if (s_cursor) {
        if (s_selected >= 0) {
            lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
        } else {
            /* 光标框自适应目标: 落在棋子上时包住整个棋子, 空格上为单格.
               set_pos/set_size 有 LVGL 早退, 可直接调 */
            int piece =
                klotski_game_piece_at(&s_game, s_cursor_x, s_cursor_y);
            int32_t cx;
            int32_t cy;
            int32_t cw;
            int32_t ch;

            if (piece >= 0) {
                const klotski_piece_t *target =
                    &s_game.state.pieces[piece];

                cx = (int32_t)target->y * KLOTSKI_UI_CELL_PX;
                cy = (int32_t)target->x * KLOTSKI_UI_CELL_PX;
                cw = (int32_t)target->h * KLOTSKI_UI_CELL_PX;
                ch = (int32_t)target->w * KLOTSKI_UI_CELL_PX;
            } else {
                cx = (int32_t)s_cursor_y * KLOTSKI_UI_CELL_PX;
                cy = (int32_t)s_cursor_x * KLOTSKI_UI_CELL_PX;
                cw = KLOTSKI_UI_CELL_PX;
                ch = KLOTSKI_UI_CELL_PX;
            }
            lv_obj_clear_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_cursor, cx, cy);
            lv_obj_set_size(s_cursor, cw, ch);
            /* border_opa 无早退, 只在闪烁态翻转时设置 */
            if (s_cursor_blink_applied != s_blink_on) {
                lv_obj_set_style_border_opa(
                    s_cursor, s_blink_on ? LV_OPA_COVER : LV_OPA_40,
                    LV_PART_MAIN);
                s_cursor_blink_applied = s_blink_on;
            }
        }
    }
    /* 棋子高亮: 只在拿起/放下变化时重设, 避免每次渲染无效化全部棋子 */
    if (s_highlight_applied != s_selected) {
        s_highlight_applied = s_selected;
        for (i = 0U; i < KLOTSKI_PIECE_COUNT; ++i) {
            bool active = ((int8_t)i == s_selected);

            if (!s_pieces[i]) {
                continue;
            }
            lv_obj_set_style_border_width(s_pieces[i], active ? 4 : 2,
                                          LV_PART_MAIN);
            lv_obj_set_style_border_color(
                s_pieces[i],
                lv_color_hex(active ? 0xFFF176U : s_skins[i].edge),
                LV_PART_MAIN);
            lv_obj_set_style_shadow_width(s_pieces[i], active ? 10 : 0,
                                          LV_PART_MAIN);
            lv_obj_set_style_shadow_color(s_pieces[i],
                                          lv_color_hex(0xFFF176U),
                                          LV_PART_MAIN);
            lv_obj_set_style_shadow_opa(s_pieces[i],
                                        active ? LV_OPA_60 : LV_OPA_TRANSP,
                                        LV_PART_MAIN);
        }
    }
}

/**
 * @brief 刷新状态栏和信息栏文案, 值变化才更新. 只在 LVGL 线程调用.
 *
 * @return 无.
 */
static void klotski_ui_sync_labels(void)
{
    const klotski_level_def_t *def = klotski_level_def(s_game.state.level);
    uint8_t level = s_game.state.level;
    uint16_t best = s_best[level];

    if (s_label_level != level) {
        if (s_status_label && def) {
            lv_label_set_text_fmt(s_status_label, "华容道  %s", def->name);
        }
        if (s_min_label && def) {
            lv_label_set_text_fmt(s_min_label, "最少 %u 步",
                                  (unsigned)def->min_steps);
        }
    }
    if (s_label_steps != s_game.state.steps && s_steps_label) {
        lv_label_set_text_fmt(s_steps_label, "%u",
                              (unsigned)s_game.state.steps);
    }
    if ((s_label_level != level || s_label_best != best) && s_best_label) {
        if (best == KLOTSKI_UI_BEST_NONE) {
            lv_label_set_text(s_best_label, "最佳 未通关");
        } else {
            lv_label_set_text_fmt(s_best_label, "最佳 %u 步",
                                  (unsigned)best);
        }
    }
    s_label_level = level;
    s_label_steps = s_game.state.steps;
    s_label_best = best;
}

/**
 * @brief 懒创建选关页: 标题 + 可滚动关卡列表. 只在 LVGL 线程调用.
 *
 * @return 无.
 */
static void klotski_ui_create_select(void)
{
    lv_obj_t *title;
    uint8_t i;

    s_select_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_select_screen);

    title = game_ui_make_label(s_select_screen, "华容道", GAME_UI_COLOR_ACCENT,
                               game_ui_font_title());
    lv_obj_set_pos(title, 0, 8);
    lv_obj_set_width(title, GAME_UI_SCREEN_W);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_select_list = lv_obj_create(s_select_screen);
    lv_obj_set_pos(s_select_list, 12, 48);
    lv_obj_set_size(s_select_list, GAME_UI_SCREEN_W - 24,
                    GAME_UI_SCREEN_H - 60);
    lv_obj_set_style_bg_opa(s_select_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_select_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_select_list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_select_list, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_select_list, LV_FLEX_FLOW_COLUMN);

    for (i = 0U; i < KLOTSKI_LEVEL_COUNT; ++i) {
        const klotski_level_def_t *def = klotski_level_def(i);
        uint8_t tier = def ? def->tier : 0U;
        lv_obj_t *star_row;
        uint8_t j;

        s_select_buttons[i] =
            game_ui_make_button(s_select_list, "", &s_select_labels[i]);
        lv_obj_set_width(s_select_buttons[i], lv_pct(100));
        lv_obj_set_height(s_select_buttons[i], KLOTSKI_UI_SELECT_BTN_H);
        lv_obj_set_style_bg_color(s_select_buttons[i],
                                  lv_color_hex(s_tier_bg[tier % 3U]),
                                  LV_PART_MAIN);
        /* 左侧关名 + 右侧三星, 取消 make_button 默认居中 */
        lv_obj_set_flex_flow(s_select_buttons[i], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(s_select_buttons[i], LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(s_select_buttons[i], 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(s_select_buttons[i], 8, LV_PART_MAIN);
        lv_obj_set_style_pad_column(s_select_buttons[i], 8, LV_PART_MAIN);
        lv_obj_set_align(s_select_labels[i], LV_ALIGN_DEFAULT);
        lv_obj_set_flex_grow(s_select_labels[i], 1);
        lv_obj_set_style_text_align(s_select_labels[i], LV_TEXT_ALIGN_LEFT,
                                    LV_PART_MAIN);

        star_row = lv_obj_create(s_select_buttons[i]);
        lv_obj_remove_style_all(star_row);
        lv_obj_set_size(star_row, KLOTSKI_UI_SELECT_STAR_ROW_W,
                        KLOTSKI_UI_SELECT_STAR_ROW_H);
        lv_obj_set_flex_flow(star_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(star_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(star_row, 2, LV_PART_MAIN);
        /* 奶油色徽章贴合星图烘焙底色, 避免深色按钮上露出色块 */
        lv_obj_set_style_bg_color(star_row, lv_color_hex(KLOTSKI_UI_WIN_PANEL_BG),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(star_row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(star_row, 4, LV_PART_MAIN);
        lv_obj_clear_flag(star_row, LV_OBJ_FLAG_CLICKABLE);
        for (j = 0U; j < 3U; ++j) {
            s_select_stars[i][j] = lv_image_create(star_row);
            lv_image_set_src(s_select_stars[i][j], &star_24x24_gray);
            lv_image_set_antialias(s_select_stars[i][j], false);
            lv_obj_clear_flag(s_select_stars[i][j], LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_add_event_cb(s_select_buttons[i], klotski_ui_level_clicked,
                            LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
    s_select_buttons[KLOTSKI_LEVEL_COUNT] =
        game_ui_make_button(s_select_list, "返回", NULL);
    lv_obj_set_width(s_select_buttons[KLOTSKI_LEVEL_COUNT], lv_pct(100));
    lv_obj_set_height(s_select_buttons[KLOTSKI_LEVEL_COUNT],
                      KLOTSKI_UI_SELECT_BTN_H);
    lv_obj_add_event_cb(s_select_buttons[KLOTSKI_LEVEL_COUNT],
                        klotski_ui_back_clicked, LV_EVENT_CLICKED, NULL);
}

/**
 * @brief 懒创建对局页: 状态栏, 棋盘, 棋子, 光标, 信息栏和弹窗. 只在 LVGL 线程调用.
 *
 * @return 无.
 */
static void klotski_ui_create_game(void)
{
    lv_obj_t *status;
    lv_obj_t *menu_btn;
    lv_obj_t *exit_mark;
    lv_obj_t *exit_text;
    lv_obj_t *panel;
    lv_obj_t *hint;
    lv_obj_t *pause_title;
    lv_obj_t *win_title;
    lv_obj_t *star_row;
    uint8_t i;

    s_game_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_game_screen);

    /* 顶部状态栏: 左侧关卡名, 右侧触摸菜单按钮 */
    status = game_ui_make_rect(s_game_screen, 0, 0, GAME_UI_SCREEN_W,
                               GAME_UI_STATUS_BAR_PX, GAME_UI_COLOR_PANEL);
    s_status_label = game_ui_make_label(status, "华容道", GAME_UI_COLOR_TEXT,
                                        game_ui_font_body());
    lv_obj_align(s_status_label, LV_ALIGN_LEFT_MID, 8, 0);
    menu_btn = game_ui_make_button(status, "菜单", NULL);
    lv_obj_set_size(menu_btn, 72, 26);
    lv_obj_align(menu_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(menu_btn, klotski_ui_menu_clicked, LV_EVENT_CLICKED,
                        NULL);

    /* 棋盘: 圆角木框 */
    s_board = lv_obj_create(s_game_screen);
    lv_obj_set_pos(s_board, KLOTSKI_UI_BOARD_X, KLOTSKI_UI_BOARD_Y);
    lv_obj_set_size(s_board, KLOTSKI_UI_BOARD_W, KLOTSKI_UI_BOARD_H);
    lv_obj_set_style_bg_color(s_board, lv_color_hex(0x8D6E4C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_board, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_board, lv_color_hex(0x5D4037),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(s_board, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_board, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_board, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);

    /* 出口标记: 转置后在右侧中间两格 (显示列 4, 行 1-2),
       先于棋子创建, 棋子滑过时盖住它 */
    exit_mark = game_ui_make_rect(s_board, KLOTSKI_UI_CELL_PX * 4,
                                  KLOTSKI_UI_CELL_PX,
                                  KLOTSKI_UI_CELL_PX, KLOTSKI_UI_CELL_PX * 2,
                                  0xA5D6A7);
    lv_obj_set_style_radius(exit_mark, 6, LV_PART_MAIN);
    exit_text = game_ui_make_label(exit_mark, "出口", 0x1B5E20,
                                   game_ui_font_body());
    lv_obj_center(exit_text);

    /* 棋子: 圆角色块 + 名字, 兵用深色字. 显示宽高为逻辑宽高互换 (转置) */
    for (i = 0U; i < KLOTSKI_PIECE_COUNT; ++i) {
        const klotski_piece_t *piece = &s_game.state.pieces[i];
        lv_obj_t *label;

        s_pieces[i] = lv_obj_create(s_board);
        lv_obj_remove_style_all(s_pieces[i]);
        lv_obj_set_size(s_pieces[i],
                        (int32_t)piece->h * KLOTSKI_UI_CELL_PX -
                            KLOTSKI_UI_PIECE_PAD * 2,
                        (int32_t)piece->w * KLOTSKI_UI_CELL_PX -
                            KLOTSKI_UI_PIECE_PAD * 2);
        lv_obj_set_style_bg_color(s_pieces[i], lv_color_hex(s_skins[i].bg),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_pieces[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(s_pieces[i], 10, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_pieces[i], 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_pieces[i],
                                      lv_color_hex(s_skins[i].edge),
                                      LV_PART_MAIN);
        lv_obj_clear_flag(s_pieces[i], LV_OBJ_FLAG_SCROLLABLE);
        label = game_ui_make_label(s_pieces[i], klotski_piece_name(i),
                                   s_skins[i].text, game_ui_font_body());
        lv_obj_center(label);
    }

    /* 光标: 白框闪烁, 选中棋子后隐藏 */
    s_cursor = lv_obj_create(s_board);
    lv_obj_remove_style_all(s_cursor);
    lv_obj_set_size(s_cursor, KLOTSKI_UI_CELL_PX, KLOTSKI_UI_CELL_PX);
    lv_obj_set_style_bg_opa(s_cursor, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_cursor, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_cursor, lv_color_hex(0xFFFFFF),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(s_cursor, 8, LV_PART_MAIN);
    lv_obj_clear_flag(s_cursor, LV_OBJ_FLAG_SCROLLABLE);

    /* 右侧信息栏 */
    panel = game_ui_make_rect(s_game_screen, KLOTSKI_UI_PANEL_X,
                              KLOTSKI_UI_BOARD_Y, KLOTSKI_UI_PANEL_W,
                              KLOTSKI_UI_BOARD_H, GAME_UI_COLOR_PANEL);
    lv_obj_set_style_radius(panel, 10, LV_PART_MAIN);

    hint = game_ui_make_label(panel, "步数", GAME_UI_COLOR_ACCENT,
                              game_ui_font_body());
    lv_obj_set_pos(hint, 10, 14);
    s_steps_label = game_ui_make_label(panel, "0", GAME_UI_COLOR_TEXT,
                                       game_ui_font_title());
    lv_obj_set_pos(s_steps_label, 10, 36);
    s_min_label = game_ui_make_label(panel, "最少 0 步", GAME_UI_COLOR_TEXT,
                                     game_ui_font_body());
    lv_obj_set_pos(s_min_label, 10, 84);
    s_best_label = game_ui_make_label(panel, "最佳 未通关", GAME_UI_COLOR_TEXT,
                                      game_ui_font_body());
    lv_obj_set_pos(s_best_label, 10, 112);
    hint = game_ui_make_label(panel, "方向键 移光标", GAME_UI_COLOR_TEXT,
                              game_ui_font_body());
    lv_obj_set_pos(hint, 10, 168);
    hint = game_ui_make_label(panel, "确定 拿起放下", GAME_UI_COLOR_TEXT,
                              game_ui_font_body());
    lv_obj_set_pos(hint, 10, 196);
    hint = game_ui_make_label(panel, "空格确定 菜单", GAME_UI_COLOR_TEXT,
                              game_ui_font_body());
    lv_obj_set_pos(hint, 10, 224);

    /* 暂停弹窗 */
    s_pause_overlay = lv_obj_create(s_game_screen);
    lv_obj_set_pos(s_pause_overlay, 100, 48);
    lv_obj_set_size(s_pause_overlay, 280, 220);
    lv_obj_set_scrollbar_mode(s_pause_overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_pause_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_pause_overlay, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_pause_overlay, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pause_overlay, lv_color_hex(GAME_UI_COLOR_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pause_overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pause_overlay, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_pause_overlay,
                                  lv_color_hex(GAME_UI_COLOR_ACCENT),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(s_pause_overlay, 6, LV_PART_MAIN);
    pause_title = game_ui_make_label(s_pause_overlay, "已暂停",
                                     GAME_UI_COLOR_ACCENT,
                                     game_ui_font_body());
    lv_obj_set_width(pause_title, lv_pct(100));
    lv_obj_set_style_text_align(pause_title, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    s_pause_buttons[0] =
        game_ui_make_button(s_pause_overlay, "继续", NULL);
    lv_obj_set_width(s_pause_buttons[0], lv_pct(100));
    lv_obj_set_height(s_pause_buttons[0], 40);
    lv_obj_add_event_cb(s_pause_buttons[0], klotski_ui_pause_resume_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_pause_buttons[1] =
        game_ui_make_button(s_pause_overlay, "重新开始", NULL);
    lv_obj_set_width(s_pause_buttons[1], lv_pct(100));
    lv_obj_set_height(s_pause_buttons[1], 40);
    lv_obj_add_event_cb(s_pause_buttons[1], klotski_ui_pause_retry_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_pause_buttons[2] =
        game_ui_make_button(s_pause_overlay, "返回选关", NULL);
    lv_obj_set_width(s_pause_buttons[2], lv_pct(100));
    lv_obj_set_height(s_pause_buttons[2], 40);
    lv_obj_add_event_cb(s_pause_buttons[2], klotski_ui_pause_back_clicked,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_pause_overlay, LV_OBJ_FLAG_HIDDEN);

    /* 胜利弹窗: 奶油色卡通面板 + 星星 */
    s_win_overlay = lv_obj_create(s_game_screen);
    lv_obj_set_pos(s_win_overlay, 90, 26);
    lv_obj_set_size(s_win_overlay, 300, 268);
    lv_obj_set_scrollbar_mode(s_win_overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_win_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_win_overlay, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_win_overlay, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_win_overlay,
                              lv_color_hex(KLOTSKI_UI_WIN_PANEL_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_win_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_win_overlay, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_win_overlay, lv_color_hex(0xF9A825),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(s_win_overlay, 14, LV_PART_MAIN);

    win_title = game_ui_make_label(s_win_overlay, "过关啦!", 0xE65100,
                                   game_ui_font_title());
    lv_obj_set_width(win_title, lv_pct(100));
    lv_obj_set_style_text_align(win_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    star_row = lv_obj_create(s_win_overlay);
    lv_obj_remove_style_all(star_row);
    lv_obj_set_width(star_row, lv_pct(100));
    lv_obj_set_height(star_row, 30);
    lv_obj_set_flex_flow(star_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(star_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(star_row, 10, LV_PART_MAIN);
    for (i = 0U; i < 3U; ++i) {
        s_win_stars[i] = lv_image_create(star_row);
        lv_image_set_src(s_win_stars[i], &star_24x24_gray);
        lv_image_set_antialias(s_win_stars[i], false);
    }

    s_win_info = game_ui_make_label(s_win_overlay, "", 0x5D4037,
                                    game_ui_font_body());
    lv_obj_set_width(s_win_info, lv_pct(100));
    lv_obj_set_style_text_align(s_win_info, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    s_win_record = game_ui_make_label(s_win_overlay, "", 0xE65100,
                                      game_ui_font_body());
    lv_obj_set_width(s_win_record, lv_pct(100));
    lv_obj_set_style_text_align(s_win_record, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    s_win_buttons[0] =
        game_ui_make_button(s_win_overlay, "下一关", &s_win_next_label);
    lv_obj_set_width(s_win_buttons[0], lv_pct(100));
    lv_obj_set_height(s_win_buttons[0], 36);
    lv_obj_add_event_cb(s_win_buttons[0], klotski_ui_win_next_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_win_buttons[1] =
        game_ui_make_button(s_win_overlay, "再玩一次", NULL);
    lv_obj_set_width(s_win_buttons[1], lv_pct(100));
    lv_obj_set_height(s_win_buttons[1], 36);
    lv_obj_add_event_cb(s_win_buttons[1], klotski_ui_win_retry_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_win_buttons[2] =
        game_ui_make_button(s_win_overlay, "返回选关", NULL);
    lv_obj_set_width(s_win_buttons[2], lv_pct(100));
    lv_obj_set_height(s_win_buttons[2], 36);
    lv_obj_add_event_cb(s_win_buttons[2], klotski_ui_win_back_clicked,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_win_overlay, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief 渲染选关页. 只在 LVGL 线程调用.
 *
 * @param user_data 未使用.
 * @return 无.
 */
static void klotski_ui_render_select(void *user_data)
{
    (void)user_data;
    if (!s_select_screen) {
        klotski_ui_create_select();
    }
    klotski_ui_refresh_select();
    lv_screen_load(s_select_screen);
}

/**
 * @brief 渲染对局页: 同步棋子, 光标, 文案和弹窗. 只在 LVGL 线程调用.
 *
 * @param user_data 未使用.
 * @return 无.
 */
static void klotski_ui_render_game(void *user_data)
{
    (void)user_data;
    if (!s_game_screen) {
        klotski_ui_create_game();
    }
    klotski_ui_sync_pieces();
    klotski_ui_sync_cursor();
    klotski_ui_sync_labels();

    if (s_pause_visible) {
        klotski_ui_refresh_pause_focus();
        lv_obj_clear_flag(s_pause_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        s_pause_focus_shown = 0xFFU; /* 下次打开时强制全量应用焦点 */
        lv_obj_add_flag(s_pause_overlay, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_win_visible) {
        /* 面板内容只在首次显示时填充: lv_image_set_src 和
           lv_label_set_text_fmt 均无旧值比较, 每次调用都会无效化重绘,
           移动焦点时重复执行会把整个弹窗和状态栏都刷一遍 */
        if (!s_win_filled) {
            const klotski_level_def_t *def =
                klotski_level_def(s_game.state.level);
            uint16_t steps = s_game.state.steps;
            uint16_t min_steps = def ? def->min_steps : 0U;
            uint8_t stars = 1U;
            uint8_t i;

            s_win_filled = true;
            if (min_steps > 0U && steps <= min_steps) {
                stars = 3U;
            } else if (min_steps > 0U &&
                       steps <= (uint16_t)(min_steps * 3U / 2U)) {
                stars = 2U;
            }
            for (i = 0U; i < 3U; ++i) {
                lv_image_set_src(s_win_stars[i],
                                 i < stars ? &star_24x24_gold
                                           : &star_24x24_gray);
            }
            lv_label_set_text_fmt(s_win_info, "用了 %u 步, 最少 %u 步",
                                  (unsigned)steps, (unsigned)min_steps);
            lv_label_set_text(s_win_record, s_new_record ? "新纪录!" : "");
            if (s_win_next_label) {
                lv_label_set_text(s_win_next_label,
                                  (s_game.state.level + 1U <
                                   KLOTSKI_LEVEL_COUNT)
                                      ? "下一关"
                                      : "返回选关");
            }
        }
        klotski_ui_refresh_win_focus();
        lv_obj_clear_flag(s_win_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        s_win_filled = false;
        s_win_focus_shown = 0xFFU; /* 下次打开时强制全量应用焦点 */
        lv_obj_add_flag(s_win_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    lv_screen_load(s_game_screen);
}

void klotski_ui_init(game_ui_back_cb_t on_back)
{
    uint8_t i;

    s_on_back = on_back;
    klotski_game_init(&s_game);
    klotski_ui_load_best();
    s_page = KLOTSKI_UI_PAGE_SELECT;
    s_cursor_x = 0U;
    s_cursor_y = 0U;
    s_selected = -1;
    s_select_index = 0U;
    s_pause_index = 0U;
    s_win_index = 0U;
    s_pause_visible = false;
    s_win_visible = false;
    s_win_pending = false;
    s_win_filled = false;
    s_new_record = false;
    s_win_elapsed_ms = 0U;
    s_blink_ms = 0U;
    s_blink_on = true;
    s_last_block_log_ms = 0U;
    s_block_log_valid = false;
    s_disp_valid = false;
    /* 取不可能值, 保证首次渲染时标签和样式完整应用 */
    s_label_level = 0xFFU;
    s_label_steps = 0xFFFFU;
    s_label_best = 0xFFFEU;
    s_cursor_blink_applied = false;
    s_highlight_applied = -2;
    s_pause_focus_shown = 0xFFU;
    s_win_focus_shown = 0xFFU;
    for (i = 0U; i < KLOTSKI_PIECE_COUNT; ++i) {
        s_disp_x[i] = 0U;
        s_disp_y[i] = 0U;
    }
}

void klotski_ui_enter_menu(void)
{
    /* 上次停在"返回"时, 再次进入不要让下一次确认立刻退出. */
    if (s_select_index >= KLOTSKI_LEVEL_COUNT) {
        s_select_index = 0U;
    }
    s_page = KLOTSKI_UI_PAGE_SELECT;
    s_pause_visible = false;
    s_win_visible = false;
    s_win_pending = false;
    game_ui_request_render();
    game_ui_port_log_i(TAG, "进入华容道选关");
}

void klotski_ui_render(void *user_data)
{
    if (s_page == KLOTSKI_UI_PAGE_GAME) {
        klotski_ui_render_game(user_data);
        return;
    }
    klotski_ui_render_select(user_data);
}

/**
 * @brief 对局中处理方向键: 已选中则滑棋子, 否则移光标.
 * 屏幕方向按横向显示映射成逻辑方向: 显示右 = 逻辑下, 显示下 = 逻辑右.
 *
 * @param key 实体键编号.
 * @return 无.
 */
static void klotski_ui_play_dir(uint8_t key)
{
    klotski_dir_t dir;

    switch (key) {
        case GAME_UI_KEY_UP:
            dir = KLOTSKI_DIR_LEFT;
            break;
        case GAME_UI_KEY_DOWN:
            dir = KLOTSKI_DIR_RIGHT;
            break;
        case GAME_UI_KEY_LEFT:
            dir = KLOTSKI_DIR_UP;
            break;
        case GAME_UI_KEY_RIGHT:
            dir = KLOTSKI_DIR_DOWN;
            break;
        default:
            return;
    }
    if (s_selected >= 0) {
        klotski_event_t event =
            klotski_game_move(&s_game, (uint8_t)s_selected, dir);

        if (event == KLOTSKI_EVENT_MOVED || event == KLOTSKI_EVENT_WIN) {
            s_cursor_x = s_game.state.pieces[s_selected].x;
            s_cursor_y = s_game.state.pieces[s_selected].y;
            game_ui_request_render();
            game_ui_port_log_i(TAG, "移动 %s dir=%s pos=(%u,%u) steps=%u",
                               klotski_piece_name((uint8_t)s_selected),
                               klotski_ui_dir_name(dir),
                               (unsigned)s_cursor_x, (unsigned)s_cursor_y,
                               (unsigned)s_game.state.steps);
        } else if (event == KLOTSKI_EVENT_BLOCKED) {
            uint32_t now = game_ui_port_tick_ms();

            if (!s_block_log_valid || (now - s_last_block_log_ms) >= 500U) {
                s_last_block_log_ms = now;
                s_block_log_valid = true;
                game_ui_port_log_i(TAG, "移动受阻 %s dir=%s pos=(%u,%u) steps=%u",
                                   klotski_piece_name((uint8_t)s_selected),
                                   klotski_ui_dir_name(dir),
                                   (unsigned)s_game.state.pieces[s_selected].x,
                                   (unsigned)s_game.state.pieces[s_selected].y,
                                   (unsigned)s_game.state.steps);
            }
        }
        if (event == KLOTSKI_EVENT_WIN) {
            uint16_t steps = s_game.state.steps;
            uint8_t level = s_game.state.level;
            const klotski_level_def_t *def = klotski_level_def(level);

            s_selected = -1;
            s_win_pending = true;
            s_win_elapsed_ms = 0U;
            if (steps < s_best[level]) {
                s_best[level] = steps;
                s_new_record = true;
                klotski_ui_save_best();
            }
            game_ui_port_log_i(TAG, "华容道第 %u 关通关 steps=%u min=%u "
                               "new_record=%u",
                               (unsigned)(level + 1U), (unsigned)steps,
                               def ? (unsigned)def->min_steps : 0U,
                               s_new_record ? 1U : 0U);
        }
        return;
    }
    if (dir == KLOTSKI_DIR_UP && s_cursor_y > 0U) {
        s_cursor_y--;
    } else if (dir == KLOTSKI_DIR_DOWN && s_cursor_y < KLOTSKI_ROWS - 1U) {
        s_cursor_y++;
    } else if (dir == KLOTSKI_DIR_LEFT && s_cursor_x > 0U) {
        s_cursor_x--;
    } else if (dir == KLOTSKI_DIR_RIGHT && s_cursor_x < KLOTSKI_COLS - 1U) {
        s_cursor_x++;
    }
    s_blink_on = true;
    s_blink_ms = 0U;
    game_ui_request_render();
}

/**
 * @brief 对局中处理 K5: 拿起/放下棋子, 空格上打开菜单.
 *
 * @return 无.
 */
static void klotski_ui_play_confirm(void)
{
    int piece;

    if (s_selected >= 0) {
        game_ui_port_log_i(TAG, "放下 %s pos=(%u,%u) steps=%u",
                           klotski_piece_name((uint8_t)s_selected),
                           (unsigned)s_game.state.pieces[s_selected].x,
                           (unsigned)s_game.state.pieces[s_selected].y,
                           (unsigned)s_game.state.steps);
        s_selected = -1;
        game_ui_request_render();
        return;
    }
    piece = klotski_game_piece_at(&s_game, s_cursor_x, s_cursor_y);
    if (piece >= 0) {
        s_selected = (int8_t)piece;
        game_ui_port_log_i(TAG, "拿起 %s cursor=(%u,%u) steps=%u",
                           klotski_piece_name((uint8_t)s_selected),
                           (unsigned)s_cursor_x, (unsigned)s_cursor_y,
                           (unsigned)s_game.state.steps);
        game_ui_request_render();
        return;
    }
    s_pause_index = 0U;
    s_pause_visible = true;
    game_ui_request_render();
    game_ui_port_log_i(TAG, "华容道打开菜单 level=%u steps=%u cursor=(%u,%u)",
                       (unsigned)(s_game.state.level + 1U),
                       (unsigned)s_game.state.steps, (unsigned)s_cursor_x,
                       (unsigned)s_cursor_y);
}

/**
 * @brief 暂停弹窗确认: 继续, 重新开始或返回选关.
 *
 * @return 无.
 */
static void klotski_ui_activate_pause(void)
{
    game_ui_port_log_i(TAG, "华容道暂停确认 index=%u level=%u steps=%u",
                       (unsigned)s_pause_index,
                       (unsigned)(s_game.state.level + 1U),
                       (unsigned)s_game.state.steps);
    if (s_pause_index == 1U) {
        klotski_ui_begin_level(s_game.state.level);
        return;
    }
    if (s_pause_index == 2U) {
        klotski_ui_goto_select();
        return;
    }
    s_pause_visible = false;
    game_ui_request_render();
}

/**
 * @brief 胜利弹窗确认: 下一关, 再玩一次或返回选关.
 *
 * @return 无.
 */
static void klotski_ui_activate_win(void)
{
    game_ui_port_log_i(TAG, "华容道胜利确认 index=%u level=%u steps=%u",
                       (unsigned)s_win_index,
                       (unsigned)(s_game.state.level + 1U),
                       (unsigned)s_game.state.steps);
    if (s_win_index == 0U) {
        if (s_game.state.level + 1U < KLOTSKI_LEVEL_COUNT) {
            klotski_ui_begin_level(s_game.state.level + 1U);
        } else {
            klotski_ui_goto_select();
        }
        return;
    }
    if (s_win_index == 1U) {
        klotski_ui_begin_level(s_game.state.level);
        return;
    }
    klotski_ui_goto_select();
}

void klotski_ui_handle_key(uint8_t key, ad_keys_event_type_t type)
{
    bool confirm = (key == GAME_UI_KEY_PAUSE);

    if (s_page == KLOTSKI_UI_PAGE_SELECT) {
        if (type != AD_KEYS_EVENT_PRESS && type != AD_KEYS_EVENT_REPEAT) {
            return;
        }
        if (confirm && type != AD_KEYS_EVENT_PRESS) {
            return;
        }
        if (key == GAME_UI_KEY_UP || key == GAME_UI_KEY_LEFT) {
            game_ui_move_index(&s_select_index, KLOTSKI_UI_SELECT_ITEM_COUNT,
                               -1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_DOWN || key == GAME_UI_KEY_RIGHT) {
            game_ui_move_index(&s_select_index, KLOTSKI_UI_SELECT_ITEM_COUNT,
                               1);
            game_ui_request_render();
        } else if (confirm) {
            if (s_select_index >= KLOTSKI_LEVEL_COUNT) {
                klotski_ui_goto_home();
            } else {
                klotski_ui_begin_level(s_select_index);
            }
        }
        return;
    }

    if (s_win_pending) {
        return;
    }
    if (s_win_visible) {
        if (type != AD_KEYS_EVENT_PRESS) {
            return;
        }
        if (key == GAME_UI_KEY_UP || key == GAME_UI_KEY_LEFT) {
            game_ui_move_index(&s_win_index, KLOTSKI_UI_WIN_ITEM_COUNT, -1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_DOWN || key == GAME_UI_KEY_RIGHT) {
            game_ui_move_index(&s_win_index, KLOTSKI_UI_WIN_ITEM_COUNT, 1);
            game_ui_request_render();
        } else if (confirm) {
            klotski_ui_activate_win();
        }
        return;
    }
    if (s_pause_visible) {
        if (type != AD_KEYS_EVENT_PRESS) {
            return;
        }
        if (key == GAME_UI_KEY_UP || key == GAME_UI_KEY_LEFT) {
            game_ui_move_index(&s_pause_index, KLOTSKI_UI_PAUSE_ITEM_COUNT,
                               -1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_DOWN || key == GAME_UI_KEY_RIGHT) {
            game_ui_move_index(&s_pause_index, KLOTSKI_UI_PAUSE_ITEM_COUNT,
                               1);
            game_ui_request_render();
        } else if (confirm) {
            klotski_ui_activate_pause();
        }
        return;
    }
    if (confirm) {
        if (type == AD_KEYS_EVENT_PRESS) {
            klotski_ui_play_confirm();
        }
        return;
    }
    klotski_ui_play_dir(key);
}

void klotski_ui_advance(uint32_t elapsed_ms)
{
    if (s_page != KLOTSKI_UI_PAGE_GAME) {
        return;
    }
    if (s_win_pending) {
        s_win_elapsed_ms += elapsed_ms;
        if (s_win_elapsed_ms >= KLOTSKI_UI_WIN_DELAY_MS) {
            s_win_pending = false;
            s_win_visible = true;
            s_win_index = 0U;
            game_ui_request_render();
            game_ui_port_log_i(TAG, "显示华容道胜利面板 level=%u steps=%u "
                               "new_record=%u",
                               (unsigned)(s_game.state.level + 1U),
                               (unsigned)s_game.state.steps,
                               s_new_record ? 1U : 0U);
        }
        return;
    }
    if (s_selected < 0 && !s_pause_visible && !s_win_visible &&
        elapsed_ms > 0U) {
        s_blink_ms += elapsed_ms;
        if (s_blink_ms >= KLOTSKI_UI_BLINK_MS) {
            s_blink_ms = 0U;
            s_blink_on = !s_blink_on;
            game_ui_request_render();
        }
    }
}

const char *klotski_ui_page_name(void)
{
    if (s_page == KLOTSKI_UI_PAGE_GAME) {
        if (s_win_visible || s_win_pending) {
            return "klotski_win";
        }
        if (s_pause_visible) {
            return "klotski_paused";
        }
        return "klotski";
    }
    return "klotski_select";
}

const klotski_state_t *klotski_ui_state(void)
{
    return klotski_game_state(&s_game);
}

void klotski_ui_start_level(uint8_t level)
{
    if (level >= KLOTSKI_LEVEL_COUNT) {
        return;
    }
    klotski_ui_begin_level(level);
}

void klotski_ui_force_win(void)
{
    if (s_page != KLOTSKI_UI_PAGE_GAME) {
        return;
    }
    s_game.state.pieces[KLOTSKI_PIECE_CAO].x = KLOTSKI_WIN_X;
    s_game.state.pieces[KLOTSKI_PIECE_CAO].y = KLOTSKI_WIN_Y;
    s_game.state.won = true;
    s_selected = -1;
    s_win_pending = true;
    s_win_elapsed_ms = KLOTSKI_UI_WIN_DELAY_MS;
    game_ui_request_render();
}

/* 以下触摸回调由 LVGL 线程触发, 只改状态并请求重绘, 与按键路径一致 */

static void klotski_ui_level_clicked(lv_event_t *event)
{
    uint8_t level = (uint8_t)(uintptr_t)lv_event_get_user_data(event);

    if (level < KLOTSKI_LEVEL_COUNT) {
        s_select_index = level;
        klotski_ui_begin_level(level);
        klotski_ui_render_game(NULL);
        (void)game_ui_consume_render();
    }
}

static void klotski_ui_back_clicked(lv_event_t *event)
{
    (void)event;
    s_select_index = KLOTSKI_LEVEL_COUNT;
    klotski_ui_goto_home();
}

static void klotski_ui_menu_clicked(lv_event_t *event)
{
    (void)event;
    if (s_page == KLOTSKI_UI_PAGE_GAME && !s_win_visible && !s_win_pending &&
        !s_pause_visible) {
        s_pause_index = 0U;
        s_pause_visible = true;
        klotski_ui_render_game(NULL);
        (void)game_ui_consume_render();
    }
}

static void klotski_ui_pause_resume_clicked(lv_event_t *event)
{
    (void)event;
    s_pause_index = 0U;
    klotski_ui_activate_pause();
    klotski_ui_render_game(NULL);
    (void)game_ui_consume_render();
}

static void klotski_ui_pause_retry_clicked(lv_event_t *event)
{
    (void)event;
    s_pause_index = 1U;
    klotski_ui_activate_pause();
    klotski_ui_render_game(NULL);
    (void)game_ui_consume_render();
}

static void klotski_ui_pause_back_clicked(lv_event_t *event)
{
    (void)event;
    s_pause_index = 2U;
    klotski_ui_activate_pause();
    if (s_page == KLOTSKI_UI_PAGE_SELECT) {
        klotski_ui_render_select(NULL);
    }
    (void)game_ui_consume_render();
}

static void klotski_ui_win_next_clicked(lv_event_t *event)
{
    (void)event;
    s_win_index = 0U;
    klotski_ui_activate_win();
    klotski_ui_render(NULL);
    (void)game_ui_consume_render();
}

static void klotski_ui_win_retry_clicked(lv_event_t *event)
{
    (void)event;
    s_win_index = 1U;
    klotski_ui_activate_win();
    klotski_ui_render_game(NULL);
    (void)game_ui_consume_render();
}

static void klotski_ui_win_back_clicked(lv_event_t *event)
{
    (void)event;
    s_win_index = 2U;
    klotski_ui_activate_win();
    if (s_page == KLOTSKI_UI_PAGE_SELECT) {
        klotski_ui_render_select(NULL);
    }
    (void)game_ui_consume_render();
}
