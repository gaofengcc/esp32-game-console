#include "game_ui_common.h"

#include "game_ui_port.h"

extern const lv_font_t lv_font_cjk_16;

static lv_font_t s_body_font_with_fallback;
static lv_font_t s_title_font_with_fallback;
static bool s_render_pending;

/**
 * @brief 准备正文/标题字体及 16px 回退, 并清掉待重绘标记.
 *
 * @return 无.
 */
void game_ui_common_init(void)
{
    s_body_font_with_fallback = *game_ui_port_font_body();
    s_body_font_with_fallback.fallback = &lv_font_cjk_16;
    s_title_font_with_fallback = *game_ui_port_font_title();
    s_title_font_with_fallback.fallback = &lv_font_cjk_16;
    s_render_pending = false;
}

/**
 * @brief 取带 CJK 回退的正文字体.
 *
 * @return 静态字体指针, 调用方不要释放.
 */
const lv_font_t *game_ui_font_body(void)
{
    return &s_body_font_with_fallback;
}

/**
 * @brief 取带 CJK 回退的标题字体.
 *
 * @return 静态字体指针, 调用方不要释放.
 */
const lv_font_t *game_ui_font_title(void)
{
    return &s_title_font_with_fallback;
}

/**
 * @brief 把屏幕铺成统一深色底, 去掉默认内边距.
 *
 * @param screen LVGL 屏幕对象, 为空则忽略.
 * @return 无.
 */
void game_ui_set_screen_style(lv_obj_t *screen)
{
    if (!screen) {
        return;
    }
    lv_obj_set_style_bg_color(screen, lv_color_hex(GAME_UI_COLOR_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
}

/**
 * @brief 创建固定颜色和字体的标签.
 *
 * @param parent 父对象.
 * @param text 初始文案, 为空时写成空串.
 * @param color RGB888 颜色.
 * @param font 字体, 通常用 game_ui_font_body/title.
 * @return 新建标签.
 */
lv_obj_t *game_ui_make_label(lv_obj_t *parent, const char *text,
                             uint32_t color, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

/**
 * @brief 创建统一风格按钮, 可选带回内部标签指针.
 *
 * @param parent 父对象.
 * @param text 按钮文案.
 * @param label_out 不为空时写回内部 label, 便于后续改字.
 * @return 新建按钮对象.
 */
lv_obj_t *game_ui_make_button(lv_obj_t *parent, const char *text,
                              lv_obj_t **label_out)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label;

    lv_obj_set_style_bg_color(button, lv_color_hex(GAME_UI_COLOR_BUTTON),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(GAME_UI_COLOR_ACCENT),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(GAME_UI_COLOR_BUTTON_PRESSED),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(GAME_UI_COLOR_TEXT),
                                  LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(button, 2, LV_PART_MAIN | LV_STATE_PRESSED);
    label = game_ui_make_label(button, text, GAME_UI_COLOR_TEXT,
                               game_ui_font_body());
    lv_obj_center(label);
    if (label_out) {
        *label_out = label;
    }
    return button;
}

/**
 * @brief 创建无边框色块, 用于棋盘边线和迷宫标记.
 *
 * @param parent 父对象.
 * @param x 相对父对象的 X.
 * @param y 相对父对象的 Y.
 * @param width 宽度, 像素.
 * @param height 高度, 像素.
 * @param color RGB888 填充色.
 * @return 新建色块对象.
 */
lv_obj_t *game_ui_make_rect(lv_obj_t *parent, int32_t x, int32_t y,
                            int32_t width, int32_t height, uint32_t color)
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

/**
 * @brief 设置按钮选中/未选中边框和底色.
 *
 * @param button 目标按钮, 为空则忽略.
 * @param focused true 表示当前选中项.
 * @return 无.
 */
void game_ui_set_button_focus(lv_obj_t *button, bool focused)
{
    if (!button) {
        return;
    }
    lv_obj_set_style_border_width(button, focused ? 3 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(
        button,
        lv_color_hex(focused ? GAME_UI_COLOR_ACCENT : GAME_UI_COLOR_BUTTON_IDLE),
        LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        button,
        lv_color_hex(focused ? GAME_UI_COLOR_BUTTON_PRESSED
                             : GAME_UI_COLOR_BUTTON),
        LV_PART_MAIN);
}

/**
 * @brief 在环形菜单上移动选中下标.
 *
 * @param index 当前下标, 为空则忽略.
 * @param count 选项个数, 0 则忽略.
 * @param delta 步进, 一般为 -1 或 1.
 * @return 无.
 */
void game_ui_move_index(uint8_t *index, uint8_t count, int8_t delta)
{
    int32_t next;

    if (!index || count == 0U) {
        return;
    }
    next = (int32_t)(*index) + (int32_t)delta;
    if (next < 0) {
        next = (int32_t)count - 1;
    } else if (next >= (int32_t)count) {
        next = 0;
    }
    *index = (uint8_t)next;
}

/**
 * @brief 标记下一帧需要重绘当前活动页.
 *
 * @return 无.
 */
void game_ui_request_render(void)
{
    s_render_pending = true;
}

/**
 * @brief 读取并清掉待重绘标记.
 *
 * @return 调用前若已请求重绘则为 true.
 */
bool game_ui_consume_render(void)
{
    bool pending = s_render_pending;

    s_render_pending = false;
    return pending;
}
