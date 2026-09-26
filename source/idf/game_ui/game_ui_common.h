#ifndef GAME_UI_COMMON_H
#define GAME_UI_COMMON_H

/*
 * 游戏 UI 公共控件, 不含任何具体游戏状态.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ad_keys.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAME_UI_TICK_MS 20U
#define GAME_UI_CELL_PX 16U
#define GAME_UI_STATUS_BAR_PX 32U
#define GAME_UI_SCREEN_W 480
#define GAME_UI_SCREEN_H 320
#define GAME_UI_KEY5_TEXT "K5"

#ifndef GAME_UI_KEY_UP
#define GAME_UI_KEY_UP 2U
#endif
#ifndef GAME_UI_KEY_DOWN
#define GAME_UI_KEY_DOWN 3U
#endif
#ifndef GAME_UI_KEY_LEFT
#define GAME_UI_KEY_LEFT 1U
#endif
#ifndef GAME_UI_KEY_RIGHT
#define GAME_UI_KEY_RIGHT 4U
#endif
#ifndef GAME_UI_KEY_PAUSE
#define GAME_UI_KEY_PAUSE 5U
#endif

#define GAME_UI_COLOR_BG 0x0F1720
#define GAME_UI_COLOR_PANEL 0x17232B
#define GAME_UI_COLOR_TEXT 0xE8F1F2
#define GAME_UI_COLOR_ACCENT 0x4DD0E1
#define GAME_UI_COLOR_FOOD 0xEF5350
#define GAME_UI_COLOR_GRID 0x141E26
#define GAME_UI_COLOR_BUTTON 0x263238
#define GAME_UI_COLOR_BUTTON_PRESSED 0x34545E
#define GAME_UI_COLOR_BUTTON_IDLE 0x1B2830
#define GAME_UI_COLOR_SNAKE_HEAD 0xAEEA00
#define GAME_UI_COLOR_SNAKE_BODY 0x2E8B57

typedef void (*game_ui_back_cb_t)(void);

/**
 * @brief 准备正文/标题字体及 16px 回退, 并清掉待重绘标记.
 *
 * @return 无.
 */
void game_ui_common_init(void);

/**
 * @brief 取带 CJK 回退的正文字体.
 *
 * @return 静态字体指针, 调用方不要释放.
 */
const lv_font_t *game_ui_font_body(void);

/**
 * @brief 取带 CJK 回退的标题字体.
 *
 * @return 静态字体指针, 调用方不要释放.
 */
const lv_font_t *game_ui_font_title(void);

/**
 * @brief 把屏幕铺成统一深色底, 去掉默认内边距.
 *
 * @param screen LVGL 屏幕对象, 为空则忽略.
 * @return 无.
 */
void game_ui_set_screen_style(lv_obj_t *screen);

/**
 * @brief 创建固定颜色和字体的标签.
 *
 * @param parent 父对象.
 * @param text 初始文案, 为空时写成空串.
 * @param color RGB888 颜色.
 * @param font 字体, 通常用 game_ui_font_body/title.
 * @return 新建标签. parent 非法时行为由 LVGL 决定.
 */
lv_obj_t *game_ui_make_label(lv_obj_t *parent, const char *text,
                             uint32_t color, const lv_font_t *font);

/**
 * @brief 创建统一风格按钮, 可选带回内部标签指针.
 *
 * @param parent 父对象.
 * @param text 按钮文案.
 * @param label_out 不为空时写回内部 label, 便于后续改字.
 * @return 新建按钮对象.
 */
lv_obj_t *game_ui_make_button(lv_obj_t *parent, const char *text,
                              lv_obj_t **label_out);

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
                            int32_t width, int32_t height, uint32_t color);

/**
 * @brief 设置按钮选中/未选中边框和底色.
 *
 * @param button 目标按钮, 为空则忽略.
 * @param focused true 表示当前选中项.
 * @return 无.
 */
void game_ui_set_button_focus(lv_obj_t *button, bool focused);

/**
 * @brief 在环形菜单上移动选中下标.
 *
 * @param index 当前下标, 为空则忽略.
 * @param count 选项个数, 0 则忽略.
 * @param delta 步进, 一般为 -1 或 1.
 * @return 无.
 */
void game_ui_move_index(uint8_t *index, uint8_t count, int8_t delta);

/**
 * @brief 标记下一帧需要重绘当前活动页.
 *
 * @return 无.
 */
void game_ui_request_render(void);

/**
 * @brief 读取并清掉待重绘标记.
 *
 * @return 调用前若已请求重绘则为 true.
 */
bool game_ui_consume_render(void);

#ifdef __cplusplus
}
#endif

#endif /* GAME_UI_COMMON_H */
