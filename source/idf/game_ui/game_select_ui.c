#include "game_select_ui.h"

#include "assets/select_icons.h"
#include "game_ui_common.h"
#include "game_ui_port.h"
#include "lvgl.h"

/* 首页固定展示三种游戏，数组和焦点下标都以该数量为边界。 */
#define GAME_SELECT_ITEM_COUNT 3U
/* SIZE+HALO: 默认小，选中大并带柔光。 */
#define GAME_SELECT_TILE_IDLE_PX 88
#define GAME_SELECT_ICON_IDLE_PX 72
#define GAME_SELECT_TILE_FOCUS_PX 128
#define GAME_SELECT_ICON_FOCUS_PX 108
#define GAME_SELECT_HALO_PAD_PX 18
#define GAME_SELECT_SLOT_FOCUS_PX \
    (GAME_SELECT_TILE_FOCUS_PX + (GAME_SELECT_HALO_PAD_PX * 2))

static const char *TAG = "game_select";
/* 名称和图标按 game_select_id_t 的顺序排列，避免在渲染路径分支。 */
static const char *const s_select_names[GAME_SELECT_ITEM_COUNT] = {
    "贪吃蛇",
    "迷宫",
    "华容道",
};
static const lv_image_dsc_t *const s_select_icons[GAME_SELECT_ITEM_COUNT] = {
    &select_icon_snake,
    &select_icon_maze,
    &select_icon_klotski,
};

/* 选择回调由 game_ui 外壳注册；以下对象仅由 LVGL 线程访问。 */
static game_select_choose_cb_t s_on_choose;
static lv_obj_t *s_select_screen;
static lv_obj_t *s_select_slots[GAME_SELECT_ITEM_COUNT];
static lv_obj_t *s_select_halos[GAME_SELECT_ITEM_COUNT];
static lv_obj_t *s_select_tiles[GAME_SELECT_ITEM_COUNT];
static lv_obj_t *s_select_images[GAME_SELECT_ITEM_COUNT];
static lv_obj_t *s_select_name;
static uint8_t s_select_index;

/**
 * @brief 按当前下标刷新尺寸, 柔光和描边, 并更新底部游戏名.
 *
 * @return 无.
 */
static void game_select_refresh_focus(void)
{
    uint8_t i;

    for (i = 0U; i < GAME_SELECT_ITEM_COUNT; ++i) {
        bool focused = (i == s_select_index);
        lv_obj_t *slot = s_select_slots[i];
        lv_obj_t *halo = s_select_halos[i];
        lv_obj_t *tile = s_select_tiles[i];
        lv_obj_t *image = s_select_images[i];
        int32_t tile_px =
            focused ? GAME_SELECT_TILE_FOCUS_PX : GAME_SELECT_TILE_IDLE_PX;
        int32_t icon_px =
            focused ? GAME_SELECT_ICON_FOCUS_PX : GAME_SELECT_ICON_IDLE_PX;
        int32_t slot_px =
            focused ? GAME_SELECT_SLOT_FOCUS_PX : GAME_SELECT_TILE_IDLE_PX;

        if (!slot || !tile) {
            continue;
        }
        lv_obj_set_size(slot, slot_px, slot_px);
        lv_obj_set_size(tile, tile_px, tile_px);
        lv_obj_center(tile);
        if (image) {
            lv_obj_set_size(image, icon_px, icon_px);
            lv_obj_center(image);
        }
        lv_obj_set_style_border_width(tile, focused ? 4 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(
            tile,
            lv_color_hex(focused ? GAME_UI_COLOR_ACCENT
                                 : GAME_UI_COLOR_BUTTON_IDLE),
            LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            tile,
            lv_color_hex(focused ? GAME_UI_COLOR_PANEL : GAME_UI_COLOR_BG),
            LV_PART_MAIN);
        if (halo) {
            if (focused) {
                lv_obj_set_size(halo,
                                GAME_SELECT_TILE_FOCUS_PX +
                                    GAME_SELECT_HALO_PAD_PX,
                                GAME_SELECT_TILE_FOCUS_PX +
                                    GAME_SELECT_HALO_PAD_PX);
                lv_obj_center(halo);
                lv_obj_clear_flag(halo, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_background(halo);
            } else {
                lv_obj_add_flag(halo, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (s_select_name && s_select_index < GAME_SELECT_ITEM_COUNT) {
        lv_label_set_text(s_select_name, s_select_names[s_select_index]);
    }
}

/**
 * @brief 确认当前选中项, 回调外壳切到对应游戏设置页.
 *
 * @return 无.
 */
static void game_select_activate(void)
{
    game_select_id_t id = GAME_SELECT_SNAKE;

    if (s_select_index == 1U) {
        id = GAME_SELECT_MAZE;
    } else if (s_select_index == 2U) {
        id = GAME_SELECT_KLOTSKI;
    }
    game_ui_port_log_i(TAG, "选择游戏 id=%u name=%s index=%u",
                       (unsigned)id, s_select_names[s_select_index],
                       (unsigned)s_select_index);
    if (s_on_choose) {
        s_on_choose(id);
    }
}

/**
 * @brief 触摸点击"贪吃蛇".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void game_select_snake_clicked(lv_event_t *event)
{
    (void)event;
    s_select_index = 0U;
    game_select_activate();
}

/**
 * @brief 触摸点击"迷宫".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void game_select_maze_clicked(lv_event_t *event)
{
    (void)event;
    s_select_index = 1U;
    game_select_activate();
}

/**
 * @brief 触摸点击"华容道".
 *
 * @param event LVGL 点击事件, 本函数不使用内容.
 * @return 无.
 */
static void game_select_klotski_clicked(lv_event_t *event)
{
    (void)event;
    s_select_index = 2U;
    game_select_activate();
}

/**
 * @brief 创建一个带柔光槽位的方形图标.
 *
 * @param parent 图标行容器.
 * @param icon 静态 RGB565 贴图, 为空则只留空框.
 * @param clicked 点击回调.
 * @param index 槽位下标, 用于写入静态数组.
 * @return 槽位对象; 创建失败返回 NULL.
 */
static lv_obj_t *game_select_make_slot(lv_obj_t *parent, const lv_image_dsc_t *icon,
                                       lv_event_cb_t clicked, uint8_t index)
{
    lv_obj_t *slot;
    lv_obj_t *halo;
    lv_obj_t *tile;
    lv_obj_t *image;

    if (!parent || index >= GAME_SELECT_ITEM_COUNT) {
        return NULL;
    }

    slot = lv_obj_create(parent);
    if (!slot) {
        game_ui_port_log_i(TAG, "创建游戏图标槽失败");
        return NULL;
    }
    lv_obj_set_size(slot, GAME_SELECT_TILE_IDLE_PX, GAME_SELECT_TILE_IDLE_PX);
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(slot, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(slot, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(slot, 0, LV_PART_MAIN);
    lv_obj_remove_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(slot, LV_OBJ_FLAG_CLICKABLE);

    /* 青色半透明柔光环, 仅选中时显示. */
    halo = lv_obj_create(slot);
    if (!halo) {
        game_ui_port_log_i(TAG, "创建柔光失败");
        return NULL;
    }
    lv_obj_set_size(halo, GAME_SELECT_TILE_FOCUS_PX + GAME_SELECT_HALO_PAD_PX,
                    GAME_SELECT_TILE_FOCUS_PX + GAME_SELECT_HALO_PAD_PX);
    lv_obj_center(halo);
    lv_obj_set_style_radius(halo, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(halo, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(halo, lv_color_hex(GAME_UI_COLOR_ACCENT),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(halo, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(halo, 22, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(halo, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(halo, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(halo, lv_color_hex(GAME_UI_COLOR_ACCENT),
                                  LV_PART_MAIN);
    lv_obj_add_flag(halo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(halo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(halo, LV_OBJ_FLAG_SCROLLABLE);

    tile = lv_obj_create(slot);
    if (!tile) {
        game_ui_port_log_i(TAG, "创建游戏图标失败");
        return NULL;
    }
    lv_obj_set_size(tile, GAME_SELECT_TILE_IDLE_PX, GAME_SELECT_TILE_IDLE_PX);
    lv_obj_center(tile);
    lv_obj_set_style_bg_color(tile, lv_color_hex(GAME_UI_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(tile, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, lv_color_hex(GAME_UI_COLOR_BUTTON_IDLE),
                                  LV_PART_MAIN);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    if (clicked) {
        lv_obj_add_event_cb(tile, clicked, LV_EVENT_CLICKED, NULL);
    }

    image = lv_image_create(tile);
    if (!image || !icon) {
        game_ui_port_log_i(TAG, "创建游戏图标图像失败");
        s_select_slots[index] = slot;
        s_select_halos[index] = halo;
        s_select_tiles[index] = tile;
        s_select_images[index] = image;
        return slot;
    }
    lv_obj_remove_style_all(image);
    lv_obj_set_size(image, GAME_SELECT_ICON_IDLE_PX, GAME_SELECT_ICON_IDLE_PX);
    lv_obj_center(image);
    lv_image_set_antialias(image, false);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_src(image, icon);

    s_select_slots[index] = slot;
    s_select_halos[index] = halo;
    s_select_tiles[index] = tile;
    s_select_images[index] = image;
    return slot;
}

/**
 * @brief 懒创建游戏选择页, 只应在 LVGL 线程调用.
 *
 * @return 无.
 */
static void game_select_create(void)
{
    lv_obj_t *row;
    static lv_event_cb_t clicked[GAME_SELECT_ITEM_COUNT] = {
        game_select_snake_clicked,
        game_select_maze_clicked,
        game_select_klotski_clicked,
    };
    uint8_t i;

    s_select_screen = lv_obj_create(NULL);
    if (!s_select_screen) {
        game_ui_port_log_i(TAG, "创建选择页失败");
        return;
    }
    game_ui_set_screen_style(s_select_screen);
    lv_obj_set_style_pad_all(s_select_screen, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_select_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_select_screen, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_select_screen, 10, LV_PART_MAIN);

    row = lv_obj_create(s_select_screen);
    if (!row) {
        game_ui_port_log_i(TAG, "创建图标行失败");
        return;
    }
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, GAME_SELECT_SLOT_FOCUS_PX);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    for (i = 0U; i < GAME_SELECT_ITEM_COUNT; ++i) {
        if (!game_select_make_slot(row, s_select_icons[i], clicked[i], i)) {
            game_ui_port_log_i(TAG, "图标槽 %u 创建失败", (unsigned)i);
        }
    }

    s_select_name = game_ui_make_label(
        s_select_screen,
        s_select_names[(s_select_index < GAME_SELECT_ITEM_COUNT) ? s_select_index
                                                                 : 0U],
        GAME_UI_COLOR_TEXT, game_ui_font_title());
    lv_obj_set_width(s_select_name, lv_pct(100));
    lv_obj_set_style_text_align(s_select_name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    game_select_refresh_focus();
}

/**
 * @brief 保存选中回调并复位下标, 不创建 LVGL 对象.
 *
 * @param on_choose 选中游戏后的回调, 可为空.
 * @return 无.
 */
void game_select_ui_init(game_select_choose_cb_t on_choose)
{
    s_on_choose = on_choose;
    s_select_index = 0U;
}

/**
 * @brief 进入选择页并请求重绘, 保留上次选中的游戏.
 *
 * @return 无.
 */
void game_select_ui_enter(void)
{
    game_ui_request_render();
}

/**
 * @brief 显示游戏选择页. 必须在 LVGL 线程调用.
 *
 * @param user_data 未使用, 仅为对齐 game_ui_port_call 签名.
 * @return 无.
 */
void game_select_ui_render(void *user_data)
{
    (void)user_data;
    if (!s_select_screen) {
        game_select_create();
    }
    game_select_refresh_focus();
    lv_screen_load(s_select_screen);
}

/**
 * @brief 处理选择页方向键和确认键.
 *
 * @param key 实体键编号, 与 GAME_UI_KEY_* 对应.
 * @param type 只响应 PRESS. 选择页不吃连发, 避免按住时连跳.
 * @return 无.
 */
void game_select_ui_handle_key(uint8_t key, ad_keys_event_type_t type)
{
    if (type != AD_KEYS_EVENT_PRESS) {
        return;
    }
    if (key == GAME_UI_KEY_UP || key == GAME_UI_KEY_LEFT) {
        game_ui_move_index(&s_select_index, GAME_SELECT_ITEM_COUNT, -1);
        game_ui_request_render();
        game_ui_port_log_i(TAG, "选择页上一项 index=%u",
                           (unsigned)s_select_index);
    } else if (key == GAME_UI_KEY_DOWN || key == GAME_UI_KEY_RIGHT) {
        game_ui_move_index(&s_select_index, GAME_SELECT_ITEM_COUNT, 1);
        game_ui_request_render();
        game_ui_port_log_i(TAG, "选择页下一项 index=%u",
                           (unsigned)s_select_index);
    } else if (key == GAME_UI_KEY_PAUSE) {
        game_select_activate();
    }
}

/**
 * @brief 取选择页诊断名.
 *
 * @return 固定字符串 "select".
 */
const char *game_select_ui_page_name(void)
{
    return "select";
}
