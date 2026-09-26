#include "game_select_ui.h"

#include "game_ui_common.h"
#include "game_ui_port.h"
#include "lvgl.h"

#define GAME_SELECT_ITEM_COUNT 2U

static const char *TAG = "game_select";
static game_select_choose_cb_t s_on_choose;
static lv_obj_t *s_select_screen;
static lv_obj_t *s_select_buttons[GAME_SELECT_ITEM_COUNT];
static uint8_t s_select_index;

/**
 * @brief 按当前下标刷新选择页两个按钮的选中态.
 *
 * @return 无.
 */
static void game_select_refresh_focus(void)
{
    uint8_t i;

    for (i = 0U; i < GAME_SELECT_ITEM_COUNT; ++i) {
        game_ui_set_button_focus(s_select_buttons[i], i == s_select_index);
    }
}

/**
 * @brief 确认当前选中项, 回调外壳切到对应游戏设置页.
 *
 * @return 无.
 */
static void game_select_activate(void)
{
    game_select_id_t id = (s_select_index == 1U) ? GAME_SELECT_MAZE
                                                 : GAME_SELECT_SNAKE;

    game_ui_port_log_i(TAG, "选择游戏 id=%u", (unsigned)id);
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
 * @brief 懒创建游戏选择页, 只应在 LVGL 线程调用.
 *
 * @return 无.
 */
static void game_select_create(void)
{
    lv_obj_t *title;
    lv_obj_t *hint;

    s_select_screen = lv_obj_create(NULL);
    game_ui_set_screen_style(s_select_screen);
    lv_obj_set_style_pad_all(s_select_screen, 18, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_select_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_select_screen, 10, LV_PART_MAIN);

    title = game_ui_make_label(s_select_screen, "选择游戏", GAME_UI_COLOR_ACCENT,
                               game_ui_font_title());
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s_select_buttons[0] = game_ui_make_button(s_select_screen, "贪吃蛇", NULL);
    lv_obj_set_width(s_select_buttons[0], lv_pct(100));
    lv_obj_set_height(s_select_buttons[0], 56);
    lv_obj_add_event_cb(s_select_buttons[0], game_select_snake_clicked,
                        LV_EVENT_CLICKED, NULL);

    s_select_buttons[1] = game_ui_make_button(s_select_screen, "迷宫", NULL);
    lv_obj_set_width(s_select_buttons[1], lv_pct(100));
    lv_obj_set_height(s_select_buttons[1], 56);
    lv_obj_add_event_cb(s_select_buttons[1], game_select_maze_clicked,
                        LV_EVENT_CLICKED, NULL);

    hint = game_ui_make_label(s_select_screen, "方向键选中, K5 进入",
                              GAME_UI_COLOR_ACCENT, game_ui_font_body());
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_select_index = 0U;
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
 * @brief 进入选择页, 复位选中项并请求重绘.
 *
 * @return 无.
 */
void game_select_ui_enter(void)
{
    s_select_index = 0U;
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
