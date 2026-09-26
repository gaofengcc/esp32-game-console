#include "maze_ui.h"

#include "game_ui_port.h"
#include "lvgl.h"

/* 设置页：开始、模式、风格、返回。 */
#define GAME_UI_MAZE_MENU_ITEM_COUNT 4U
#define GAME_UI_MAZE_PAUSE_ITEM_COUNT 3U
/* 迷宫棋盘保持和逻辑层同样的 16px 网格。 */
#define GAME_UI_MAZE_CELL_PX 16U
#define GAME_UI_MAZE_BOARD_X 8
#define GAME_UI_MAZE_BOARD_W ((int32_t)MAZE_WIDTH * (int32_t)GAME_UI_MAZE_CELL_PX)
#define GAME_UI_MAZE_BOARD_H ((int32_t)MAZE_HEIGHT * (int32_t)GAME_UI_MAZE_CELL_PX)
/* 下面几个周期只影响角色/出口动画，不改变逻辑时间。 */
#define MAZE_UI_EXIT_PULSE_MS 560U
#define MAZE_UI_EXIT_REFRESH_MS 70U
#define MAZE_UI_PLAYER_BOB_MS 500U
#define MAZE_UI_PLAYER_BLINK_PERIOD_MS 1700U
#define MAZE_UI_PLAYER_BLINK_MS 150U

typedef struct {
    uint32_t wall;
    uint32_t wall_alt;
    uint32_t wall_edge;
    uint32_t path;
    uint32_t entrance;
    uint32_t exit_cell;
    uint32_t sight;
    uint32_t player;
    uint32_t guard;
    uint32_t frame;
    uint32_t status_bg;
    uint32_t status_fg;
} maze_ui_palette_t;

typedef enum {
    MAZE_UI_PAGE_MENU = 0,
    MAZE_UI_PAGE_GAME,
} maze_ui_page_t;

static const char *TAG = "maze_ui";
/* 逻辑状态只在 game_ui 任务更新，LVGL 对象只在 render 回调访问。 */
static maze_game_t s_maze;
static maze_ui_page_t s_page;
static game_ui_back_cb_t s_on_back;
static maze_ui_theme_t s_theme;
static uint32_t s_shot_seed;
static uint32_t s_exit_anim_ms;
static uint32_t s_exit_refresh_acc;
static maze_dir_t s_player_dir;
/* 同一次界面循环里, 同一方向只走一格. 只在 game_ui 任务读写. */
static bool s_step_this_tick;
static maze_input_t s_stepped_input;
/* 撞墙日志按时间节流；按住方向键时不会每 20ms 刷一行。 */
static uint32_t s_last_block_log_ms;
static bool s_block_log_valid;

static const maze_ui_palette_t s_palettes[MAZE_UI_THEME_COUNT] = {
    /* 沙漠: 木门 / 蓝衣探险者 / 紫袍守卫 */
    {0x6D3B14, 0x8A4B1C, 0x3E220C, 0xF3D39A, 0x4E342E, 0x00838F,
     0xB71C1C, 0x0277BD, 0x6A1B9A, 0xC9A66B, 0x5D4037, 0xFFF8E1},
    /* 雪地: 木屋门 / 红棉服 / 深紫守卫 */
    {0xF4FAFF, 0xE8F2F8, 0xB0C4CE, 0x3D7EA6, 0x6D4C41, 0xE65100,
     0x1565C0, 0xFF1744, 0x311B92, 0x90A4AE, 0x1F3A44, 0xE8F4F8},
    /* 森林: 树桩门 / 青斗篷 / 酒红守卫 */
    {0x1B5E20, 0x2E7D32, 0x0D3310, 0xC8E6A0, 0x5D4037, 0xF9A825,
     0xBF360C, 0x00ACC1, 0x880E4F, 0x8D6E4C, 0x1B3D1A, 0xE8F5E9},
    /* 太空: 青气闸 / 荧光绿宇航员 / 红甲守卫 */
    {0x1A1040, 0x2A1B63, 0x0A0620, 0xD7CCF0, 0x00B8D4, 0xD500F9,
     0x880E4F, 0x76FF03, 0xFF1744, 0x7E57C2, 0x0D0221, 0xEDE7F6},
    /* 海洋: 紫码头柱 / 黄雨衣 / 品红守卫 */
    {0x01579B, 0x0277BD, 0x013A63, 0xB3E5FC, 0x4A148C, 0xE65100,
     0xAD1457, 0xFFD600, 0xAD1457, 0x4FC3F7, 0x014F7A, 0xE0F7FA},
};

static const uint32_t s_player_skin[MAZE_UI_THEME_COUNT] = {
    0xE0A070U, 0xFFE4D0U, 0xF0C090U, 0xE8D8FFU, 0xF5C8A0U,
};

static const uint32_t s_exit_glow[MAZE_UI_THEME_COUNT] = {
    0x4DD0E1U, 0xFFD54FU, 0xFFF59DU, 0xF8BBD0U, 0xFFE082U,
};

static const char *const s_hero_front[GAME_UI_MAZE_CELL_PX] = {
    "................",
    "......oooo......",
    ".....ohhhho.....",
    "....ohhhhhho....",
    "....osssssso....",
    "....oseesseo....",
    "....osssssso....",
    ".....ossso......",
    "....occccco.....",
    "...occccccco....",
    "...occccccco....",
    "....occccco.....",
    ".....occoo......",
    "....oco..oco....",
    "....oo....oo....",
    "................",
};

static const char *const s_hero_back[GAME_UI_MAZE_CELL_PX] = {
    "................",
    "......oooo......",
    ".....ohhhho.....",
    "....ohhhhhho....",
    "....ohhhhhho....",
    "....occcccco....",
    "....occcccco....",
    ".....occco......",
    "....occccco.....",
    "...occccccco....",
    "...occccccco....",
    "....occccco.....",
    ".....occoo......",
    "....oco..oco....",
    "....oo....oo....",
    "................",
};

static const char *const s_guard_front[GAME_UI_MAZE_CELL_PX] = {
    "................",
    ".....oooooo.....",
    "....occcccco....",
    "...occcccccco...",
    "...ocxccccxco...",
    "...occcccccco...",
    "....occcccco....",
    "...occcccccco...",
    "...occcccccco...",
    "....occcccco....",
    ".....occoo......",
    "....oco..oco....",
    "....oo....oo....",
    "................",
    "................",
    "................",
};

static lv_obj_t *s_maze_menu_screen;
static lv_obj_t *s_maze_menu_buttons[GAME_UI_MAZE_MENU_ITEM_COUNT];
static lv_obj_t *s_maze_mode_label;
static lv_obj_t *s_maze_theme_label;
static lv_obj_t *s_maze_menu_hint;
static uint8_t s_maze_menu_index;
static lv_obj_t *s_maze_screen;
static lv_obj_t *s_maze_status;
static lv_obj_t *s_maze_info_label;
static lv_obj_t *s_maze_pause_hint;
static lv_obj_t *s_maze_board;
static lv_obj_t *s_maze_pause_overlay;
static lv_obj_t *s_maze_pause_buttons[GAME_UI_MAZE_PAUSE_ITEM_COUNT];
static uint8_t s_maze_pause_index;

/* LVGL v9 的 style/label setter 无旧值比较, 每次调用都会触发无效化重绘.
 * 以下缓存记录已应用到对象上的值, 仅在值变化时才真正调用 setter. */
static uint8_t s_maze_menu_focus_shown = 0xFFU;  /* 设置页已高亮的下标 */
static uint8_t s_maze_pause_focus_shown = 0xFFU; /* 暂停弹窗已高亮的下标 */
static int s_maze_menu_shown_mode = -1;         /* 设置页已显示的模式 */
static int s_maze_menu_shown_theme = -1;        /* 设置页已显示的风格 */
static const void *s_maze_pal_applied;          /* 已应用到对局页的调色板 */
static unsigned s_maze_info_shown_level = 0xFFFFU;   /* 状态栏已显示关卡 */
static uint32_t s_maze_info_shown_sec = 0xFFFFFFFFU; /* 状态栏已显示秒数 */
static int s_maze_info_shown_mode = -1;              /* 状态栏已显示模式 */
static int s_maze_info_shown_theme = -1;             /* 状态栏已显示风格 */

static void maze_ui_render_menu(void *user_data);
static void maze_ui_render_game(void *user_data);

/**
 * @brief 把方向/输入换成日志短名称.
 *
 * @param input 迷宫输入.
 * @return 静态字符串.
 */
static const char *maze_ui_input_name(maze_input_t input)
{
    switch (input) {
        case MAZE_INPUT_UP:
            return "up";
        case MAZE_INPUT_DOWN:
            return "down";
        case MAZE_INPUT_LEFT:
            return "left";
        case MAZE_INPUT_RIGHT:
            return "right";
        case MAZE_INPUT_PAUSE:
            return "pause";
        case MAZE_INPUT_NONE:
        default:
            return "none";
    }
}

/**
 * @brief 把迷宫模式换成设置页/状态栏上的中文名.
 *
 * @param mode 逻辑层模式. 未知值按简单模式处理.
 * @return 静态字符串, 调用方不要释放.
 */
static const char *maze_ui_mode_name(maze_mode_t mode)
{
    switch (mode) {
        case MAZE_MODE_TIMED:
            return "计时";
        case MAZE_MODE_CHALLENGE:
            return "闯关";
        case MAZE_MODE_SIMPLE:
        default:
            return "简单";
    }
}

/**
 * @brief 把风格枚举换成设置页/状态栏上的中文名.
 *
 * @param theme 当前风格. 未知值按沙漠处理.
 * @return 静态字符串, 调用方不要释放.
 */
static const char *maze_ui_theme_name(maze_ui_theme_t theme)
{
    switch (theme) {
        case MAZE_UI_THEME_SNOW:
            return "雪地";
        case MAZE_UI_THEME_FOREST:
            return "森林";
        case MAZE_UI_THEME_SPACE:
            return "太空";
        case MAZE_UI_THEME_OCEAN:
            return "海洋";
        case MAZE_UI_THEME_DESERT:
        default:
            return "沙漠";
    }
}

/**
 * @brief 取当前风格的配色表.
 *
 * @return 静态调色板指针, 不会为空.
 */
static const maze_ui_palette_t *maze_ui_palette(void)
{
    if ((unsigned)s_theme >= MAZE_UI_THEME_COUNT) {
        return &s_palettes[MAZE_UI_THEME_DESERT];
    }
    return &s_palettes[s_theme];
}

/**
 * @brief 按 0-255 权重混合两个 8bit 通道.
 *
 * @param from 起始值.
 * @param to 目标值.
 * @param amount 混合量, 0 为 from, 255 为 to.
 * @return 混合后的通道值.
 */
static uint8_t maze_ui_mix_chan(uint8_t from, uint8_t to, uint8_t amount)
{
    return (uint8_t)(((uint16_t)from * (255U - amount) +
                      (uint16_t)to * amount) / 255U);
}

/**
 * @brief 按 0-255 权重混合两个 RGB888 颜色.
 *
 * @param from 起始色.
 * @param to 目标色.
 * @param amount 混合量, 0 为 from, 255 为 to.
 * @return 混合后的 RGB888.
 */
static uint32_t maze_ui_mix_rgb(uint32_t from, uint32_t to, uint8_t amount)
{
    uint8_t r = maze_ui_mix_chan((uint8_t)(from >> 16), (uint8_t)(to >> 16),
                                 amount);
    uint8_t g = maze_ui_mix_chan((uint8_t)(from >> 8), (uint8_t)(to >> 8),
                                 amount);
    uint8_t b = maze_ui_mix_chan((uint8_t)from, (uint8_t)to, amount);

    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/**
 * @brief 把时间折成 0-255 三角波, 用于出口呼吸.
 *
 * @param ms 已经过的毫秒.
 * @param period 一个来回的周期, 至少 2.
 * @return 三角波幅度.
 */
static uint8_t maze_ui_triangle_u8(uint32_t ms, uint32_t period)
{
    uint32_t half;
    uint32_t phase;

    if (period < 2U) {
        return 0U;
    }
    half = period / 2U;
    phase = ms % period;
    if (phase > half) {
        phase = period - phase;
    }
    return (uint8_t)((phase * 255U) / half);
}

/**
 * @brief 在格子内填一块实心矩形, 超出格子的部分裁掉.
 *
 * @param layer LVGL 绘制层, 不可为空.
 * @param dsc 已初始化的矩形描述, 不可为空.
 * @param clip 出口格子区域.
 * @param x1 左.
 * @param y1 上.
 * @param x2 右.
 * @param y2 下.
 * @return 无.
 */
static void maze_ui_fill_xy(lv_layer_t *layer, lv_draw_rect_dsc_t *dsc,
                            const lv_area_t *clip, int32_t x1, int32_t y1,
                            int32_t x2, int32_t y2)
{
    lv_area_t area;

    if (!layer || !dsc || !clip) {
        return;
    }
    if (x1 < clip->x1) {
        x1 = clip->x1;
    }
    if (y1 < clip->y1) {
        y1 = clip->y1;
    }
    if (x2 > clip->x2) {
        x2 = clip->x2;
    }
    if (y2 > clip->y2) {
        y2 = clip->y2;
    }
    if ((x1 > x2) || (y1 > y2)) {
        return;
    }
    area.x1 = x1;
    area.y1 = y1;
    area.x2 = x2;
    area.y2 = y2;
    lv_draw_rect(layer, dsc, &area);
}

/**
 * @brief 把图案字符换成像素颜色.
 *
 * @param ch 图案字符.
 * @param cloth 衣服/主题色.
 * @param skin 肤色.
 * @return RGB888, 未知字符返回 0.
 */
static uint32_t maze_ui_glyph_color(char ch, uint32_t cloth, uint32_t skin)
{
    switch (ch) {
        case 'o':
            return maze_ui_mix_rgb(cloth, 0x000000U, 170U);
        case 'h':
            return maze_ui_mix_rgb(cloth, 0x000000U, 80U);
        case 's':
            return skin;
        case 'e':
            return 0x1A1A1AU;
        case 'c':
            return cloth;
        case 'x':
            return 0xFF1744U;
        default:
            return 0U;
    }
}

/**
 * @brief 在格子里画 16x16 像素图案, 可镜像, 可眨眼, 可上下晃.
 *
 * @param layer LVGL 绘制层.
 * @param board 棋盘裁剪区.
 * @param cell 角色所在格.
 * @param rows 16 行图案, 每行 16 字符.
 * @param bob 垂直偏移, 通常 0 或 -1.
 * @param mirror 为 true 时左右翻转.
 * @param blink 为 true 时眼睛改成肤色.
 * @param cloth 衣服色.
 * @param skin 肤色.
 * @return 无.
 */
static void maze_ui_draw_glyph(lv_layer_t *layer, const lv_area_t *board,
                               const lv_area_t *cell, const char *const *rows,
                               int32_t bob, bool mirror, bool blink,
                               uint32_t cloth, uint32_t skin)
{
    lv_draw_rect_dsc_t dsc;
    uint8_t row;
    uint8_t col;
    char ch;
    uint32_t color;
    int32_t ox;
    int32_t oy;

    if (!layer || !board || !cell || !rows) {
        return;
    }
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = 0;
    dsc.border_width = 0;
    for (row = 0U; row < GAME_UI_MAZE_CELL_PX; ++row) {
        if (!rows[row]) {
            continue;
        }
        for (col = 0U; col < GAME_UI_MAZE_CELL_PX; ++col) {
            ch = rows[row][col];
            if ((ch == '\0') || (ch == '.')) {
                continue;
            }
            if ((ch == 'e') && blink) {
                ch = 's';
            }
            color = maze_ui_glyph_color(ch, cloth, skin);
            ox = mirror ? ((int32_t)GAME_UI_MAZE_CELL_PX - 1 - (int32_t)col)
                        : (int32_t)col;
            oy = (int32_t)row + bob;
            dsc.bg_color = lv_color_hex(color);
            maze_ui_fill_xy(layer, &dsc, board, cell->x1 + ox, cell->y1 + oy,
                            cell->x1 + ox, cell->y1 + oy);
        }
    }
}

/**
 * @brief 画一扇 16px 门, 可选呼吸高光.
 *
 * @param layer LVGL 绘制层.
 * @param cell 门所在格.
 * @param color 门框主色.
 * @param glow 呼吸目标色, amount 为 0 时不用.
 * @param amount 0-255, 越大越亮.
 * @return 无.
 */
static void maze_ui_draw_door(lv_layer_t *layer, const lv_area_t *cell,
                              uint32_t color, uint32_t glow, uint8_t amount)
{
    lv_draw_rect_dsc_t dsc;
    lv_area_t inner;
    lv_area_t slit;
    lv_area_t knob;
    uint32_t frame;
    uint32_t panel;

    if (!layer || !cell) {
        return;
    }
    frame = maze_ui_mix_rgb(color, glow, amount);
    panel = maze_ui_mix_rgb(maze_ui_mix_rgb(color, 0xFFFFFFU, 50U), glow,
                            amount);

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = 0;
    dsc.border_width = 2;
    dsc.border_opa = LV_OPA_COVER;
    dsc.border_color = lv_color_hex(frame);
    dsc.bg_color = lv_color_hex(maze_ui_mix_rgb(color, 0x000000U, 90U));
    lv_draw_rect(layer, &dsc, cell);

    inner = *cell;
    inner.x1 += 4;
    inner.y1 += 3;
    inner.x2 -= 4;
    inner.y2 -= 1;
    if ((inner.x1 <= inner.x2) && (inner.y1 <= inner.y2)) {
        dsc.border_width = 0;
        dsc.bg_color = lv_color_hex(panel);
        lv_draw_rect(layer, &dsc, &inner);
    }

    slit.x1 = (cell->x1 + cell->x2) / 2;
    slit.x2 = slit.x1;
    slit.y1 = cell->y1 + 5;
    slit.y2 = cell->y2 - 2;
    dsc.border_width = 0;
    dsc.bg_color = lv_color_hex(maze_ui_mix_rgb(color, 0x000000U, 160U));
    lv_draw_rect(layer, &dsc, &slit);

    knob.x1 = cell->x2 - 6;
    knob.x2 = knob.x1 + 1;
    knob.y1 = (cell->y1 + cell->y2) / 2;
    knob.y2 = knob.y1 + 1;
    dsc.bg_color = lv_color_hex(maze_ui_mix_rgb(0xFFD54FU, 0xFFFFFFU, amount));
    lv_draw_rect(layer, &dsc, &knob);
}

/**
 * @brief 画入口门, 不呼吸.
 *
 * @param layer LVGL 绘制层.
 * @param cell 入口格.
 * @param color 门框色.
 * @return 无.
 */
static void maze_ui_draw_entrance(lv_layer_t *layer, const lv_area_t *cell,
                                  uint32_t color)
{
    maze_ui_draw_door(layer, cell, color, color, 0U);
}

/**
 * @brief 画出口门, 主题色并带呼吸高光.
 *
 * @param layer LVGL 绘制层.
 * @param cell 出口格.
 * @param color 门框主色.
 * @return 无.
 */
static void maze_ui_draw_exit(lv_layer_t *layer, const lv_area_t *cell,
                              uint32_t color)
{
    uint8_t pulse;
    uint8_t amount;
    uint32_t glow;

    pulse = maze_ui_triangle_u8(s_exit_anim_ms, MAZE_UI_EXIT_PULSE_MS);
    amount = (uint8_t)(((uint16_t)pulse * 180U) / 255U);
    if ((unsigned)s_theme >= MAZE_UI_THEME_COUNT) {
        glow = s_exit_glow[MAZE_UI_THEME_DESERT];
    } else {
        glow = s_exit_glow[s_theme];
    }
    maze_ui_draw_door(layer, cell, color, glow, amount);
}

/**
 * @brief 取当前主题肤色.
 *
 * @return RGB888.
 */
static uint32_t maze_ui_player_skin(void)
{
    if ((unsigned)s_theme >= MAZE_UI_THEME_COUNT) {
        return s_player_skin[MAZE_UI_THEME_DESERT];
    }
    return s_player_skin[s_theme];
}

/**
 * @brief 画玩家像素头像, 含朝向, 呼吸晃动和眨眼.
 *
 * @param layer LVGL 绘制层.
 * @param board 棋盘裁剪区.
 * @param cell 玩家所在格.
 * @param cloth 衣服色.
 * @return 无.
 */
static void maze_ui_draw_player(lv_layer_t *layer, const lv_area_t *board,
                                const lv_area_t *cell, uint32_t cloth)
{
    uint8_t pulse;
    int32_t bob;
    bool blink;
    bool mirror;
    const char *const *rows;

    pulse = maze_ui_triangle_u8(s_exit_anim_ms, MAZE_UI_PLAYER_BOB_MS);
    bob = (pulse > 128U) ? -1 : 0;
    blink = ((s_exit_anim_ms % MAZE_UI_PLAYER_BLINK_PERIOD_MS) <
             MAZE_UI_PLAYER_BLINK_MS);
    mirror = (s_player_dir == MAZE_DIR_LEFT);
    rows = (s_player_dir == MAZE_DIR_UP) ? s_hero_back : s_hero_front;
    maze_ui_draw_glyph(layer, board, cell, rows, bob, mirror, blink, cloth,
                       maze_ui_player_skin());
}

/**
 * @brief 画守卫像素小人, 用主题守卫色.
 *
 * @param layer LVGL 绘制层.
 * @param board 棋盘裁剪区.
 * @param cell 守卫所在格.
 * @param cloth 衣服色.
 * @return 无.
 */
static void maze_ui_draw_guard(lv_layer_t *layer, const lv_area_t *board,
                               const lv_area_t *cell, uint32_t cloth)
{
    uint8_t pulse;
    int32_t bob;
    bool blink;

    pulse = maze_ui_triangle_u8(s_exit_anim_ms + 180U, MAZE_UI_PLAYER_BOB_MS);
    bob = (pulse > 180U) ? -1 : 0;
    blink = ((s_exit_anim_ms % 2200U) < 80U);
    maze_ui_draw_glyph(layer, board, cell, s_guard_front, bob, false, blink,
                       cloth, 0xC0B0A0U);
}

/**
 * @brief 按当前下标刷新设置页按钮高亮, 并更新确定键提示.
 *
 * @return 无.
 */
static void maze_ui_refresh_menu_focus(void)
{
    uint8_t prev = s_maze_menu_focus_shown;

    if (prev == s_maze_menu_index) {
        return;
    }
    /* 只刷新失焦和新聚焦两个按钮, 避免全量 style 写入触发整屏重绘. */
    if (prev < GAME_UI_MAZE_MENU_ITEM_COUNT && s_maze_menu_buttons[prev]) {
        game_ui_set_button_focus(s_maze_menu_buttons[prev], false);
    }
    if (s_maze_menu_buttons[s_maze_menu_index]) {
        game_ui_set_button_focus(s_maze_menu_buttons[s_maze_menu_index], true);
    }
    s_maze_menu_focus_shown = s_maze_menu_index;
    if (!s_maze_menu_hint) {
        return;
    }
    if (s_maze_menu_index == 1U) {
        lv_label_set_text(s_maze_menu_hint, "确定 : 切换模式");
    } else if (s_maze_menu_index == 2U) {
        lv_label_set_text(s_maze_menu_hint, "确定 : 切换风格");
    } else if (s_maze_menu_index == 3U) {
        lv_label_set_text(s_maze_menu_hint, "确定 : 返回");
    } else {
        lv_label_set_text(s_maze_menu_hint, "确定 : 开始游戏");
    }
}

/**
 * @brief 按当前下标刷新暂停弹窗三个按钮的选中态.
 *
 * @return 无.
 */
static void maze_ui_refresh_pause_focus(void)
{
    uint8_t prev = s_maze_pause_focus_shown;

    if (prev == s_maze_pause_index) {
        return;
    }
    if (prev < GAME_UI_MAZE_PAUSE_ITEM_COUNT && s_maze_pause_buttons[prev]) {
        game_ui_set_button_focus(s_maze_pause_buttons[prev], false);
    }
    if (s_maze_pause_buttons[s_maze_pause_index]) {
        game_ui_set_button_focus(s_maze_pause_buttons[s_maze_pause_index],
                                 true);
    }
    s_maze_pause_focus_shown = s_maze_pause_index;
}

/**
 * @brief 离开对局, 回到迷宫设置页并请求重绘.
 *
 * @return 无.
 */
static void maze_ui_goto_menu(void)
{
    s_maze_menu_index = 0U;
    s_page = MAZE_UI_PAGE_MENU;
    game_ui_request_render();
    game_ui_port_log_i(TAG, "返回迷宫设置");
}

/**
 * @brief 通过外壳回调回到游戏选择页. 未注册回调时什么也不做.
 *
 * @return 无.
 */
static void maze_ui_goto_select(void)
{
    if (s_on_back) {
        s_on_back();
    }
}

/**
 * @brief 用当前模式重新播种并开一局, 切到对局页.
 *
 * @return 无.
 */
static void maze_ui_start_game(void)
{
    const maze_state_t *state;
    uint32_t seed;

    maze_game_set_mode(&s_maze, maze_game_get_mode(&s_maze));
    seed = (s_shot_seed != 0U) ? s_shot_seed : game_ui_port_tick_ms();
    maze_game_seed(&s_maze, seed);
    maze_game_start(&s_maze);
    s_page = MAZE_UI_PAGE_GAME;
    s_maze_pause_index = 0U;
    s_exit_anim_ms = 0U;
    s_exit_refresh_acc = 0U;
    s_player_dir = MAZE_DIR_RIGHT;
    s_step_this_tick = false;
    s_stepped_input = MAZE_INPUT_NONE;
    s_last_block_log_ms = 0U;
    s_block_log_valid = false;
    game_ui_request_render();
    state = maze_game_state(&s_maze);
    game_ui_port_log_i(
        TAG,
        "进入迷宫 mode=%s theme=%s seed=%lu level=%u "
        "entrance=(%u,%u) exit=(%u,%u) guard=%u guard_pos=(%u,%u)",
        maze_ui_mode_name(state ? state->mode : MAZE_MODE_SIMPLE),
        maze_ui_theme_name(s_theme), (unsigned long)seed,
        state ? (unsigned)state->level : 0U,
        state ? (unsigned)state->entrance.x : 0U,
        state ? (unsigned)state->entrance.y : 0U,
        state ? (unsigned)state->exit_cell.x : 0U,
        state ? (unsigned)state->exit_cell.y : 0U,
        (state && state->guard_active) ? 1U : 0U,
        state ? (unsigned)state->guard.x : 0U,
        state ? (unsigned)state->guard.y : 0U);
}

/**
 * @brief 在简单, 计时, 闯关三种模式间循环切换.
 *
 * @return 无.
 */
static void maze_ui_cycle_mode(void)
{
    maze_mode_t mode = maze_game_get_mode(&s_maze);

    if (mode == MAZE_MODE_SIMPLE) {
        mode = MAZE_MODE_TIMED;
    } else if (mode == MAZE_MODE_TIMED) {
        mode = MAZE_MODE_CHALLENGE;
    } else {
        mode = MAZE_MODE_SIMPLE;
    }
    maze_game_set_mode(&s_maze, mode);
    game_ui_request_render();
    game_ui_port_log_i(TAG, "迷宫切换模式 mode=%s",
                       maze_ui_mode_name(mode));
}

/**
 * @brief 在五种迷宫风格间循环切换.
 *
 * @return 无.
 */
static void maze_ui_cycle_theme(void)
{
    s_theme = (maze_ui_theme_t)(((unsigned)s_theme + 1U) % MAZE_UI_THEME_COUNT);
    game_ui_request_render();
    game_ui_port_log_i(TAG, "迷宫切换风格 theme=%s",
                       maze_ui_theme_name(s_theme));
}

/**
 * @brief 随机换成另一种风格, 保证与当前不同.
 *
 * @return 无.
 */
static void maze_ui_pick_other_theme(void)
{
    uint8_t current;
    uint8_t pick;

    if (MAZE_UI_THEME_COUNT < 2U) {
        return;
    }
    current = (uint8_t)s_theme;
    if (current >= MAZE_UI_THEME_COUNT) {
        current = 0U;
    }
    pick = (uint8_t)(game_ui_port_tick_ms() % (MAZE_UI_THEME_COUNT - 1U));
    if (pick >= current) {
        pick++;
    }
    s_theme = (maze_ui_theme_t)pick;
    game_ui_port_log_i(TAG, "迷宫自动换风格 theme=%s",
                       maze_ui_theme_name(s_theme));
}

/**
 * @brief 确认设置页当前选项: 开始, 切模式或返回选择页.
 *
 * @return 无.
 */
static void maze_ui_activate_menu(void)
{
    if (s_maze_menu_index == 1U) {
        maze_ui_cycle_mode();
    } else if (s_maze_menu_index == 2U) {
        maze_ui_cycle_theme();
    } else if (s_maze_menu_index == 3U) {
        maze_ui_goto_select();
    } else {
        maze_ui_start_game();
    }
}

/**
 * @brief 确认暂停弹窗当前选项: 继续, 再来一次或返回设置.
 *
 * @return 无.
 */
static void maze_ui_activate_pause(void)
{
    const maze_state_t *state = maze_game_state(&s_maze);

    game_ui_port_log_i(TAG, "迷宫暂停确认 index=%u level=%u remain_ms=%lu "
                       "player=(%u,%u) guard=(%u,%u)",
                       (unsigned)s_maze_pause_index,
                       state ? (unsigned)state->level : 0U,
                       state ? (unsigned long)state->remain_ms : 0UL,
                       state ? (unsigned)state->player.x : 0U,
                       state ? (unsigned)state->player.y : 0U,
                       state ? (unsigned)state->guard.x : 0U,
                       state ? (unsigned)state->guard.y : 0U);
    if (s_maze_pause_index == 1U) {
        maze_ui_start_game();
        return;
    }
    if (s_maze_pause_index == 2U) {
        if (maze_game_get_status(&s_maze) == MAZE_STATUS_PAUSED) {
            (void)maze_game_set_input(&s_maze, MAZE_INPUT_PAUSE);
        }
        maze_ui_goto_menu();
        return;
    }
    if (maze_game_get_status(&s_maze) == MAZE_STATUS_PAUSED) {
        (void)maze_game_set_input(&s_maze, MAZE_INPUT_PAUSE);
    }
    game_ui_request_render();
    game_ui_port_log_i(TAG, "迷宫暂停后继续");
}

/**
 * @brief 触摸点击"开始游戏", 立即建对局并切屏.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
/**
 * @brief 触摸点击"开始游戏", 立即建对局并切屏.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void maze_ui_start_clicked(lv_event_t *event)
{
    (void)event;
    s_maze_menu_index = 0U;
    maze_ui_start_game();
    maze_ui_render_game(NULL);
    (void)game_ui_consume_render();
}

/**
 * @brief 触摸点击"模式"按钮, 切换模式并刷新文案.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
/**
 * @brief 触摸点击"模式"按钮, 切换模式并刷新文案.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void maze_ui_mode_clicked(lv_event_t *event)
{
    (void)event;
    s_maze_menu_index = 1U;
    maze_ui_cycle_mode();
    maze_ui_refresh_menu_focus();
    if (s_maze_mode_label) {
        lv_label_set_text_fmt(s_maze_mode_label, "模式：%s",
                              maze_ui_mode_name(maze_game_get_mode(&s_maze)));
    }
}

/**
 * @brief 触摸点击"风格"按钮, 循环切换主题并刷新文案.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void maze_ui_theme_clicked(lv_event_t *event)
{
    (void)event;
    s_maze_menu_index = 2U;
    maze_ui_cycle_theme();
    maze_ui_refresh_menu_focus();
    if (s_maze_theme_label) {
        lv_label_set_text_fmt(s_maze_theme_label, "风格：%s",
                              maze_ui_theme_name(s_theme));
    }
}

/**
 * @brief 触摸点击设置页"返回", 交给外壳回选择页.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
/**
 * @brief 触摸点击设置页"返回", 交给外壳回选择页.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void maze_ui_menu_back_clicked(lv_event_t *event)
{
    (void)event;
    s_maze_menu_index = 3U;
    maze_ui_goto_select();
}

/**
 * @brief 触摸点击暂停弹窗"继续".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
/**
 * @brief 触摸点击暂停弹窗"继续".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void maze_ui_pause_resume_clicked(lv_event_t *event)
{
    (void)event;
    s_maze_pause_index = 0U;
    maze_ui_activate_pause();
    if (s_page == MAZE_UI_PAGE_GAME) {
        maze_ui_render_game(NULL);
    }
    (void)game_ui_consume_render();
}

/**
 * @brief 触摸点击暂停弹窗"再来一次".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
/**
 * @brief 触摸点击暂停弹窗"再来一次".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void maze_ui_pause_retry_clicked(lv_event_t *event)
{
    (void)event;
    s_maze_pause_index = 1U;
    maze_ui_activate_pause();
    maze_ui_render_game(NULL);
    (void)game_ui_consume_render();
}

/**
 * @brief 触摸点击暂停弹窗"返回", 回到迷宫设置页.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
/**
 * @brief 触摸点击暂停弹窗"返回", 回到迷宫设置页.
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void maze_ui_pause_back_clicked(lv_event_t *event)
{
    (void)event;
    s_maze_pause_index = 2U;
    maze_ui_activate_pause();
    if (s_page == MAZE_UI_PAGE_MENU) {
        maze_ui_render_menu(NULL);
    }
    (void)game_ui_consume_render();
}

/**
 * @brief 懒创建迷宫设置页对象, 只应在 LVGL 线程调用.
 *
 * @return 无.
 */
/**
 * @brief 懒创建迷宫设置页对象, 只应在 LVGL 线程调用.
 *
 * @return 无.
 */
static void maze_ui_create_menu(void)
{
    lv_obj_t *title;

    s_maze_menu_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_maze_menu_screen);
    lv_obj_set_style_pad_all(s_maze_menu_screen, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_maze_menu_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_maze_menu_screen, 6, LV_PART_MAIN);

    title = game_ui_make_label(s_maze_menu_screen, "迷宫", GAME_UI_COLOR_ACCENT,
                               game_ui_font_title());
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_maze_menu_buttons[0] = game_ui_make_button(s_maze_menu_screen, "开始游戏",
                                                 NULL);
    lv_obj_set_width(s_maze_menu_buttons[0], lv_pct(100));
    lv_obj_set_height(s_maze_menu_buttons[0], 44);
    lv_obj_add_event_cb(s_maze_menu_buttons[0], maze_ui_start_clicked,
                        LV_EVENT_CLICKED, NULL);

    s_maze_menu_buttons[1] = game_ui_make_button(s_maze_menu_screen, "模式：简单",
                                                 &s_maze_mode_label);
    lv_obj_set_width(s_maze_menu_buttons[1], lv_pct(100));
    lv_obj_set_height(s_maze_menu_buttons[1], 44);
    lv_obj_add_event_cb(s_maze_menu_buttons[1], maze_ui_mode_clicked,
                        LV_EVENT_CLICKED, NULL);

    s_maze_menu_buttons[2] = game_ui_make_button(s_maze_menu_screen, "风格：沙漠",
                                                 &s_maze_theme_label);
    lv_obj_set_width(s_maze_menu_buttons[2], lv_pct(100));
    lv_obj_set_height(s_maze_menu_buttons[2], 42);
    lv_obj_add_event_cb(s_maze_menu_buttons[2], maze_ui_theme_clicked,
                        LV_EVENT_CLICKED, NULL);

    s_maze_menu_buttons[3] = game_ui_make_button(s_maze_menu_screen, "返回",
                                                 NULL);
    lv_obj_set_width(s_maze_menu_buttons[3], lv_pct(100));
    lv_obj_set_height(s_maze_menu_buttons[3], 42);
    lv_obj_add_event_cb(s_maze_menu_buttons[3], maze_ui_menu_back_clicked,
                        LV_EVENT_CLICKED, NULL);

    s_maze_menu_hint = game_ui_make_label(s_maze_menu_screen, "确定 : 开始游戏",
                                          GAME_UI_COLOR_ACCENT,
                                          game_ui_font_body());
    lv_obj_set_width(s_maze_menu_hint, lv_pct(100));
    lv_obj_set_style_text_align(s_maze_menu_hint, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    s_maze_menu_index = 0U;
    maze_ui_refresh_menu_focus();
}

/**
 * @brief 在棋盘 DRAW_MAIN 里画墙, 入口, 出口和守卫视线, 避免每格一个 lv_obj.
 *
 * @param event LVGL 绘制事件, 非 DRAW_MAIN 时直接返回.
 * @return 无.
 */
static void maze_ui_board_draw(lv_event_t *event)
{
    lv_obj_t *obj;
    lv_layer_t *layer;
    lv_area_t coords;
    lv_draw_rect_dsc_t wall_dsc;
    lv_draw_rect_dsc_t mark_dsc;
    const maze_state_t *state;
    uint8_t x;
    uint8_t y;

    if (!event || lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
        return;
    }
    obj = lv_event_get_target(event);
    layer = lv_event_get_layer(event);
    if (!obj || !layer) {
        return;
    }
    lv_obj_get_coords(obj, &coords);
    state = maze_game_state(&s_maze);
    if (!state) {
        return;
    }

    const maze_ui_palette_t *pal = maze_ui_palette();

    lv_draw_rect_dsc_init(&wall_dsc);
    wall_dsc.bg_opa = LV_OPA_COVER;
    wall_dsc.radius = 0;
    wall_dsc.border_width = 0;

    lv_draw_rect_dsc_init(&mark_dsc);
    mark_dsc.bg_opa = LV_OPA_COVER;
    mark_dsc.radius = 0;
    mark_dsc.border_width = 0;

    for (y = 0U; y < MAZE_HEIGHT; ++y) {
        for (x = 0U; x < MAZE_WIDTH; ++x) {
            lv_area_t cell;
            maze_point_t point = {x, y};

            cell.x1 = coords.x1 + ((int32_t)x * (int32_t)GAME_UI_MAZE_CELL_PX);
            cell.y1 = coords.y1 + ((int32_t)y * (int32_t)GAME_UI_MAZE_CELL_PX);
            cell.x2 = cell.x1 + (int32_t)GAME_UI_MAZE_CELL_PX - 1;
            cell.y2 = cell.y1 + (int32_t)GAME_UI_MAZE_CELL_PX - 1;
            if (maze_game_cell(&s_maze, x, y) == MAZE_CELL_WALL) {
                lv_area_t edge = cell;

                wall_dsc.bg_color = lv_color_hex(
                    (((x + y) & 1U) != 0U) ? pal->wall_alt : pal->wall);
                lv_draw_rect(layer, &wall_dsc, &cell);
                wall_dsc.bg_color = lv_color_hex(pal->wall_edge);
                edge.y1 = cell.y2;
                lv_draw_rect(layer, &wall_dsc, &edge);
                edge = cell;
                edge.x1 = cell.x2;
                lv_draw_rect(layer, &wall_dsc, &edge);
                continue;
            }
            mark_dsc.bg_color = lv_color_hex(pal->path);
            lv_draw_rect(layer, &mark_dsc, &cell);
            if (maze_game_same_point(point, state->entrance)) {
                maze_ui_draw_entrance(layer, &cell, pal->entrance);
            } else if (maze_game_same_point(point, state->exit_cell)) {
                maze_ui_draw_exit(layer, &cell, pal->exit_cell);
            } else if (maze_game_is_sight_cell(&s_maze, x, y)) {
                mark_dsc.bg_color = lv_color_hex(pal->sight);
                lv_draw_rect(layer, &mark_dsc, &cell);
            }
            continue;
        }
    }

    {
        lv_area_t actor;

        actor.x1 = coords.x1 +
                   ((int32_t)state->player.x * (int32_t)GAME_UI_MAZE_CELL_PX);
        actor.y1 = coords.y1 +
                   ((int32_t)state->player.y * (int32_t)GAME_UI_MAZE_CELL_PX);
        actor.x2 = actor.x1 + (int32_t)GAME_UI_MAZE_CELL_PX - 1;
        actor.y2 = actor.y1 + (int32_t)GAME_UI_MAZE_CELL_PX - 1;
        maze_ui_draw_player(layer, &coords, &actor, pal->player);
        if (state->guard_active) {
            actor.x1 = coords.x1 +
                       ((int32_t)state->guard.x * (int32_t)GAME_UI_MAZE_CELL_PX);
            actor.y1 = coords.y1 +
                       ((int32_t)state->guard.y * (int32_t)GAME_UI_MAZE_CELL_PX);
            actor.x2 = actor.x1 + (int32_t)GAME_UI_MAZE_CELL_PX - 1;
            actor.y2 = actor.y1 + (int32_t)GAME_UI_MAZE_CELL_PX - 1;
            maze_ui_draw_guard(layer, &coords, &actor, pal->guard);
        }
    }
}

/**
 * @brief 懒创建迷宫对局页, 含状态栏, 棋盘和暂停弹窗.
 *
 * @return 无.
 */
static void maze_ui_create_game(void)
{
    lv_obj_t *pause_title;

    s_maze_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_maze_screen);

    s_maze_status = lv_obj_create(s_maze_screen);
    lv_obj_set_size(s_maze_status, GAME_UI_SCREEN_W, GAME_UI_STATUS_BAR_PX);
    lv_obj_set_pos(s_maze_status, 0, 0);
    lv_obj_set_style_bg_color(s_maze_status, lv_color_hex(GAME_UI_COLOR_PANEL),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_maze_status, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_maze_status, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_maze_status, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_maze_status, 8, LV_PART_MAIN);

    s_maze_info_label = game_ui_make_label(s_maze_status, "第1关",
                                           GAME_UI_COLOR_TEXT,
                                           game_ui_font_body());
    lv_obj_align(s_maze_info_label, LV_ALIGN_LEFT_MID, 0, 0);
    s_maze_pause_hint = game_ui_make_label(s_maze_status,
                                           GAME_UI_KEY5_TEXT " 暂停",
                                           GAME_UI_COLOR_TEXT,
                                           game_ui_font_body());
    lv_obj_align(s_maze_pause_hint, LV_ALIGN_RIGHT_MID, 0, 0);

    s_maze_board = lv_obj_create(s_maze_screen);
    lv_obj_set_size(s_maze_board, GAME_UI_MAZE_BOARD_W, GAME_UI_MAZE_BOARD_H);
    lv_obj_set_pos(s_maze_board, GAME_UI_MAZE_BOARD_X, GAME_UI_STATUS_BAR_PX);
    lv_obj_set_style_bg_color(s_maze_board, lv_color_hex(GAME_UI_COLOR_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_border_width(s_maze_board, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_maze_board, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_maze_board, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_maze_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_maze_board, maze_ui_board_draw, LV_EVENT_DRAW_MAIN,
                        NULL);

    s_maze_pause_overlay = lv_obj_create(s_maze_screen);
    lv_obj_set_pos(s_maze_pause_overlay, 100, 48);
    lv_obj_set_size(s_maze_pause_overlay, 280, 220);
    lv_obj_set_scrollbar_mode(s_maze_pause_overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_maze_pause_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_maze_pause_overlay, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_maze_pause_overlay, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_maze_pause_overlay,
                              lv_color_hex(GAME_UI_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_maze_pause_overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_maze_pause_overlay, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_maze_pause_overlay,
                                  lv_color_hex(GAME_UI_COLOR_ACCENT),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(s_maze_pause_overlay, 6, LV_PART_MAIN);

    pause_title = game_ui_make_label(s_maze_pause_overlay, "已暂停",
                                     GAME_UI_COLOR_ACCENT, game_ui_font_body());
    lv_obj_set_width(pause_title, lv_pct(100));
    lv_obj_set_style_text_align(pause_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_maze_pause_buttons[0] = game_ui_make_button(s_maze_pause_overlay, "继续",
                                                  NULL);
    lv_obj_set_width(s_maze_pause_buttons[0], lv_pct(100));
    lv_obj_set_height(s_maze_pause_buttons[0], 40);
    lv_obj_add_event_cb(s_maze_pause_buttons[0], maze_ui_pause_resume_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_maze_pause_buttons[1] = game_ui_make_button(s_maze_pause_overlay,
                                                  "再来一次", NULL);
    lv_obj_set_width(s_maze_pause_buttons[1], lv_pct(100));
    lv_obj_set_height(s_maze_pause_buttons[1], 40);
    lv_obj_add_event_cb(s_maze_pause_buttons[1], maze_ui_pause_retry_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_maze_pause_buttons[2] = game_ui_make_button(s_maze_pause_overlay, "返回",
                                                  NULL);
    lv_obj_set_width(s_maze_pause_buttons[2], lv_pct(100));
    lv_obj_set_height(s_maze_pause_buttons[2], 40);
    lv_obj_add_event_cb(s_maze_pause_buttons[2], maze_ui_pause_back_clicked,
                        LV_EVENT_CLICKED, NULL);
    s_maze_pause_index = 0U;
    maze_ui_refresh_pause_focus();
    lv_obj_add_flag(s_maze_pause_overlay, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief 刷新并显示迷宫设置页.
 *
 * @param user_data 未使用, 仅为对齐 game_ui_port_call 签名.
 * @return 无.
 */
static void maze_ui_render_menu(void *user_data)
{
    (void)user_data;
    if (!s_maze_menu_screen) {
        maze_ui_create_menu();
    }
    /* 标签值缓存: 仅在值变化时重写, 避免重复 set_text_fmt 触发无效化. */
    {
        int mode = (int)maze_game_get_mode(&s_maze);

        if (s_maze_mode_label && s_maze_menu_shown_mode != mode) {
            lv_label_set_text_fmt(s_maze_mode_label, "模式：%s",
                                  maze_ui_mode_name((maze_mode_t)mode));
            s_maze_menu_shown_mode = mode;
        }
    }
    if (s_maze_theme_label && s_maze_menu_shown_theme != (int)s_theme) {
        lv_label_set_text_fmt(s_maze_theme_label, "风格：%s",
                              maze_ui_theme_name(s_theme));
        s_maze_menu_shown_theme = (int)s_theme;
    }
    maze_ui_refresh_menu_focus();
    lv_screen_load(s_maze_menu_screen);
}

/**
 * @brief 按逻辑状态刷新对局页: 关卡信息, 角色位置和暂停弹窗.
 *
 * @param user_data 未使用, 仅为对齐 game_ui_port_call 签名.
 * @return 无.
 */
static void maze_ui_render_game(void *user_data)
{
    const maze_state_t *state = maze_game_state(&s_maze);
    bool paused;

    (void)user_data;
    if (!s_maze_screen) {
        maze_ui_create_game();
    }
    if (!state) {
        return;
    }
    /* 调色板缓存: 主题不变时不重复写 style, 避免整屏无效化. */
    {
        const maze_ui_palette_t *pal = maze_ui_palette();

        if (s_maze_pal_applied != (const void *)pal) {
            lv_obj_set_style_bg_color(s_maze_screen,
                                      lv_color_hex(pal->status_bg),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_maze_status,
                                      lv_color_hex(pal->status_bg),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_maze_board, lv_color_hex(pal->frame),
                                      LV_PART_MAIN);
            if (s_maze_info_label) {
                lv_obj_set_style_text_color(s_maze_info_label,
                                            lv_color_hex(pal->status_fg),
                                            LV_PART_MAIN);
            }
            if (s_maze_pause_hint) {
                lv_obj_set_style_text_color(s_maze_pause_hint,
                                            lv_color_hex(pal->status_fg),
                                            LV_PART_MAIN);
            }
            s_maze_pal_applied = (const void *)pal;
        }
    }
    /* 状态栏缓存: 关卡/秒数/模式/风格任一变化才重写标签. */
    {
        unsigned level = (unsigned)state->level;
        uint32_t sec = (state->mode == MAZE_MODE_TIMED)
                           ? (state->remain_ms + 999U) / 1000U
                           : 0U;

        if (level != s_maze_info_shown_level ||
            sec != s_maze_info_shown_sec ||
            (int)state->mode != s_maze_info_shown_mode ||
            (int)s_theme != s_maze_info_shown_theme) {
            if (state->mode == MAZE_MODE_TIMED) {
                lv_label_set_text_fmt(s_maze_info_label, "第%u关  %us  %s",
                                      level, (unsigned)sec,
                                      maze_ui_theme_name(s_theme));
            } else {
                lv_label_set_text_fmt(s_maze_info_label, "第%u关  %s  %s",
                                      level, maze_ui_mode_name(state->mode),
                                      maze_ui_theme_name(s_theme));
            }
            s_maze_info_shown_level = level;
            s_maze_info_shown_sec = sec;
            s_maze_info_shown_mode = (int)state->mode;
            s_maze_info_shown_theme = (int)s_theme;
        }
    }
    paused = (maze_game_get_status(&s_maze) == MAZE_STATUS_PAUSED);
    if (paused) {
        maze_ui_refresh_pause_focus();
        lv_obj_clear_flag(s_maze_pause_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_maze_pause_overlay, LV_OBJ_FLAG_HIDDEN);
        /* 暂停时角色和迷宫静止, 无需重绘棋盘; 弹窗区域由 LVGL 自行合成. */
        lv_obj_invalidate(s_maze_board);
    }
    lv_screen_load(s_maze_screen);
}

/**
 * @brief 初始化迷宫逻辑和页状态, 不创建 LVGL 对象.
 *
 * @param on_back 返回游戏选择页的回调, 可为空.
 * @return 无.
 */
/**
 * @brief 初始化迷宫逻辑和页状态, 不创建 LVGL 对象.
 *
 * @param on_back 返回游戏选择页的回调, 可为空.
 * @return 无.
 */
void maze_ui_init(game_ui_back_cb_t on_back)
{
    s_on_back = on_back;
    maze_game_init(&s_maze);
    s_theme = MAZE_UI_THEME_DESERT;
    s_shot_seed = 0U;
    s_exit_anim_ms = 0U;
    s_exit_refresh_acc = 0U;
    s_player_dir = MAZE_DIR_RIGHT;
    s_step_this_tick = false;
    s_stepped_input = MAZE_INPUT_NONE;
    s_page = MAZE_UI_PAGE_MENU;
    s_maze_menu_index = 0U;
    s_maze_pause_index = 0U;
}

/**
 * @brief 进入迷宫设置页并请求外壳重绘.
 *
 * @return 无.
 */
/**
 * @brief 进入迷宫设置页并请求外壳重绘.
 *
 * @return 无.
 */
void maze_ui_enter_menu(void)
{
    s_maze_menu_index = 0U;
    s_page = MAZE_UI_PAGE_MENU;
    game_ui_request_render();
    game_ui_port_log_i(TAG, "进入迷宫设置");
}

/**
 * @brief 按当前内部页渲染设置或对局. 必须在 LVGL 线程调用.
 *
 * @param user_data 未使用, 透传给具体渲染函数.
 * @return 无.
 */
/**
 * @brief 按当前内部页渲染设置或对局. 必须在 LVGL 线程调用.
 *
 * @param user_data 未使用, 透传给具体渲染函数.
 * @return 无.
 */
void maze_ui_render(void *user_data)
{
    if (s_page == MAZE_UI_PAGE_GAME) {
        maze_ui_render_game(user_data);
        return;
    }
    maze_ui_render_menu(user_data);
}

/**
 * @brief 处理实体键: 菜单导航, 暂停选项或对局移动.
 *
 * @param key 实体键编号, 与 GAME_UI_KEY_* 对应.
 * @param type 菜单和暂停弹窗只响应 PRESS. 对局里方向键仍接受连发.
 * @return 无.
 */
void maze_ui_handle_key(uint8_t key, ad_keys_event_type_t type)
{
    if (s_page == MAZE_UI_PAGE_MENU) {
        if (type != AD_KEYS_EVENT_PRESS) {
            return;
        }
        if (key == GAME_UI_KEY_UP || key == GAME_UI_KEY_LEFT) {
            game_ui_move_index(&s_maze_menu_index, GAME_UI_MAZE_MENU_ITEM_COUNT,
                               -1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_DOWN || key == GAME_UI_KEY_RIGHT) {
            game_ui_move_index(&s_maze_menu_index, GAME_UI_MAZE_MENU_ITEM_COUNT,
                               1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_PAUSE) {
            maze_ui_activate_menu();
        }
        return;
    }

    if (maze_game_get_status(&s_maze) == MAZE_STATUS_PAUSED) {
        if (type != AD_KEYS_EVENT_PRESS) {
            return;
        }
        if (key == GAME_UI_KEY_UP || key == GAME_UI_KEY_LEFT) {
            game_ui_move_index(&s_maze_pause_index,
                               GAME_UI_MAZE_PAUSE_ITEM_COUNT, -1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_DOWN || key == GAME_UI_KEY_RIGHT) {
            game_ui_move_index(&s_maze_pause_index,
                               GAME_UI_MAZE_PAUSE_ITEM_COUNT, 1);
            game_ui_request_render();
        } else if (key == GAME_UI_KEY_PAUSE) {
            maze_ui_activate_pause();
        }
        return;
    }

    if (key == GAME_UI_KEY_PAUSE) {
        if (type != AD_KEYS_EVENT_PRESS) {
            return;
        }
        s_maze_pause_index = 0U;
        game_ui_port_log_i(TAG, "迷宫内暂停");
    }
    {
        maze_input_t input = MAZE_INPUT_NONE;
        maze_event_t event;
        const maze_state_t *before = maze_game_state(&s_maze);
        maze_point_t before_player = {UINT8_MAX, UINT8_MAX};

        if (before) {
            before_player = before->player;
        }
        switch (key) {
            case GAME_UI_KEY_UP:
                input = MAZE_INPUT_UP;
                break;
            case GAME_UI_KEY_DOWN:
                input = MAZE_INPUT_DOWN;
                break;
            case GAME_UI_KEY_LEFT:
                input = MAZE_INPUT_LEFT;
                break;
            case GAME_UI_KEY_RIGHT:
                input = MAZE_INPUT_RIGHT;
                break;
            case GAME_UI_KEY_PAUSE:
                input = MAZE_INPUT_PAUSE;
                break;
            default:
                break;
        }
        if ((input != MAZE_INPUT_NONE) && (input != MAZE_INPUT_PAUSE) &&
            s_step_this_tick && (input == s_stepped_input)) {
            return;
        }
        event = maze_game_set_input(&s_maze, input);
        if ((input != MAZE_INPUT_NONE) && (input != MAZE_INPUT_PAUSE)) {
            const maze_state_t *moved = maze_game_state(&s_maze);

            if (moved && !maze_game_same_point(before_player, moved->player)) {
                s_step_this_tick = true;
                s_stepped_input = input;
            }
        }
        if (input == MAZE_INPUT_UP) {
            s_player_dir = MAZE_DIR_UP;
        } else if (input == MAZE_INPUT_DOWN) {
            s_player_dir = MAZE_DIR_DOWN;
        } else if (input == MAZE_INPUT_LEFT) {
            s_player_dir = MAZE_DIR_LEFT;
        } else if (input == MAZE_INPUT_RIGHT) {
            s_player_dir = MAZE_DIR_RIGHT;
        }
        if (event != MAZE_EVENT_NONE || input == MAZE_INPUT_PAUSE) {
            game_ui_request_render();
        }
        if (event == MAZE_EVENT_BLOCKED) {
            uint32_t now = game_ui_port_tick_ms();

            if (!s_block_log_valid || (now - s_last_block_log_ms) >= 500U) {
                s_last_block_log_ms = now;
                s_block_log_valid = true;
                game_ui_port_log_i(TAG, "迷宫移动受阻 input=%s player=(%u,%u) "
                                   "level=%u",
                                   maze_ui_input_name(input),
                                   before ? (unsigned)before->player.x : 0U,
                                   before ? (unsigned)before->player.y : 0U,
                                   before ? (unsigned)before->level : 0U);
            }
        } else if (event == MAZE_EVENT_LEVEL_CLEAR) {
            s_player_dir = MAZE_DIR_RIGHT;
            maze_ui_pick_other_theme();
            {
                const maze_state_t *next = maze_game_state(&s_maze);

                game_ui_port_log_i(
                    TAG,
                    "迷宫过关 level=%u player=(%u,%u) next_exit=(%u,%u) "
                    "theme=%s",
                    next ? (unsigned)next->level : 0U,
                    next ? (unsigned)next->player.x : 0U,
                    next ? (unsigned)next->player.y : 0U,
                    next ? (unsigned)next->exit_cell.x : 0U,
                    next ? (unsigned)next->exit_cell.y : 0U,
                    maze_ui_theme_name(s_theme));
            }
        } else if (event == MAZE_EVENT_CAUGHT) {
            s_player_dir = MAZE_DIR_RIGHT;
            {
                const maze_state_t *reset = maze_game_state(&s_maze);

                game_ui_port_log_i(TAG, "迷宫被发现 caught=%lu player=(%u,%u) "
                                   "guard=(%u,%u)",
                                   reset ? (unsigned long)reset->caught_count : 0UL,
                                   reset ? (unsigned)reset->player.x : 0U,
                                   reset ? (unsigned)reset->player.y : 0U,
                                   reset ? (unsigned)reset->guard.x : 0U,
                                   reset ? (unsigned)reset->guard.y : 0U);
            }
        }
    }
}

/**
 * @brief 推进迷宫对局时间, 超时或角色变化时请求重绘.
 *
 * @param elapsed_ms 距上次调用的毫秒数, 0 表示只消化输入.
 * @return 无.
 */
/**
 * @brief 推进迷宫对局时间, 超时或角色变化时请求重绘.
 *
 * @param elapsed_ms 距上次调用的毫秒数, 0 表示只消化输入.
 * @return 无.
 */
void maze_ui_advance(uint32_t elapsed_ms)
{
    const maze_state_t *before;
    maze_point_t before_player = {UINT8_MAX, UINT8_MAX};
    maze_point_t before_guard = {UINT8_MAX, UINT8_MAX};
    uint32_t before_sec = 0U;
    uint16_t before_level = 0U;
    maze_event_t event;
    const maze_state_t *after;

    s_step_this_tick = false;
    s_stepped_input = MAZE_INPUT_NONE;
    if (s_page != MAZE_UI_PAGE_GAME) {
        return;
    }
    before = maze_game_state(&s_maze);
    if (before) {
        before_player = before->player;
        before_guard = before->guard;
        before_sec = (before->remain_ms + 999U) / 1000U;
        before_level = before->level;
    }
    event = maze_game_advance(&s_maze, elapsed_ms);
    after = maze_game_state(&s_maze);
    if (event != MAZE_EVENT_NONE) {
        game_ui_request_render();
        if (event == MAZE_EVENT_TIMEOUT) {
            game_ui_port_log_i(TAG, "迷宫超时 level=%u timeout_count=%lu "
                               "remain_ms=%lu player=(%u,%u)",
                               after ? (unsigned)after->level : 0U,
                               after ? (unsigned long)after->timeout_count : 0UL,
                               after ? (unsigned long)after->remain_ms : 0UL,
                               after ? (unsigned)after->player.x : 0U,
                               after ? (unsigned)after->player.y : 0U);
        } else if (event == MAZE_EVENT_CAUGHT) {
            game_ui_port_log_i(TAG, "迷宫被发现 caught=%lu level=%u "
                               "player=(%u,%u) guard=(%u,%u)",
                               after ? (unsigned long)after->caught_count : 0UL,
                               after ? (unsigned)after->level : 0U,
                               after ? (unsigned)after->player.x : 0U,
                               after ? (unsigned)after->player.y : 0U,
                               after ? (unsigned)after->guard.x : 0U,
                               after ? (unsigned)after->guard.y : 0U);
        }
    } else if (after &&
               (!maze_game_same_point(before_player, after->player) ||
                !maze_game_same_point(before_guard, after->guard) ||
                before_level != after->level ||
                before_sec != ((after->remain_ms + 999U) / 1000U))) {
        game_ui_request_render();
    }
    if ((elapsed_ms > 0U) &&
        (maze_game_get_status(&s_maze) == MAZE_STATUS_RUNNING)) {
        s_exit_anim_ms += elapsed_ms;
        s_exit_refresh_acc += elapsed_ms;
        if (s_exit_refresh_acc >= MAZE_UI_EXIT_REFRESH_MS) {
            s_exit_refresh_acc = 0U;
            game_ui_request_render();
        }
    }
}

/**
 * @brief 取当前迷宫页诊断名.
 *
 * @return "maze_menu", "maze" 或 "maze_paused". 静态字符串.
 */
/**
 * @brief 取当前迷宫页诊断名.
 *
 * @return "maze_menu", "maze" 或 "maze_paused". 静态字符串.
 */
const char *maze_ui_page_name(void)
{
    if (s_page == MAZE_UI_PAGE_GAME &&
        maze_game_get_status(&s_maze) == MAZE_STATUS_PAUSED) {
        return "maze_paused";
    }
    return (s_page == MAZE_UI_PAGE_GAME) ? "maze" : "maze_menu";
}

/**
 * @brief 取迷宫逻辑只读快照, 供诊断和模拟器使用.
 *
 * @return 逻辑状态指针, 游戏未初始化时也可能非空.
 */
/**
 * @brief 取迷宫逻辑只读快照, 供诊断和模拟器使用.
 *
 * @return 逻辑状态指针, 游戏未初始化时也可能非空.
 */
const maze_state_t *maze_ui_state(void)
{
    return maze_game_state(&s_maze);
}

/**
 * @brief 设置迷宫墙/路配色风格. 非法值回落到沙漠.
 *
 * @param theme 沙漠/雪地/森林/太空/海洋.
 * @return 无.
 */
void maze_ui_set_theme(maze_ui_theme_t theme)
{
    if ((unsigned)theme >= MAZE_UI_THEME_COUNT) {
        theme = MAZE_UI_THEME_DESERT;
    }
    s_theme = theme;
    game_ui_request_render();
    game_ui_port_log_i(TAG, "设置迷宫风格 theme=%s",
                       maze_ui_theme_name(s_theme));
}

/**
 * @brief 取当前迷宫风格.
 *
 * @return 当前风格枚举.
 */
maze_ui_theme_t maze_ui_get_theme(void)
{
    return s_theme;
}

/**
 * @brief 固定下一局迷宫随机种子. 0 表示改回用 tick.
 *
 * @param seed 非 0 时开局使用该种子, 便于对照截图.
 * @return 无.
 */
void maze_ui_set_shot_seed(uint32_t seed)
{
    s_shot_seed = seed;
    game_ui_port_log_i(TAG, "设置迷宫种子 seed=%lu",
                       (unsigned long)s_shot_seed);
}
