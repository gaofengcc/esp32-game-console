#ifndef GAME_SELECT_UI_H
#define GAME_SELECT_UI_H

/*
 * 首页游戏选择, 不持有任何具体游戏状态.
 */

#include "ad_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GAME_SELECT_NONE = 0,
    GAME_SELECT_SNAKE,
    GAME_SELECT_MAZE,
    GAME_SELECT_KLOTSKI,
} game_select_id_t;

typedef void (*game_select_choose_cb_t)(game_select_id_t id);

/**
 * @brief 保存选中回调并复位下标, 不创建 LVGL 对象.
 *
 * @param on_choose 选中游戏后的回调, 可为空.
 * @return 无.
 */
void game_select_ui_init(game_select_choose_cb_t on_choose);

/**
 * @brief 进入选择页, 复位选中项并请求重绘.
 *
 * @return 无.
 */
void game_select_ui_enter(void);

/**
 * @brief 显示游戏选择页. 必须在 LVGL 线程调用.
 *
 * @param user_data 未使用, 仅为对齐 game_ui_port_call 签名.
 * @return 无.
 */
void game_select_ui_render(void *user_data);

/**
 * @brief 处理选择页方向键和确认键.
 *
 * @param key 实体键编号, 与 GAME_UI_KEY_* 对应.
 * @param type 按下或连发. 确认键只响应 PRESS.
 * @return 无.
 */
void game_select_ui_handle_key(uint8_t key, ad_keys_event_type_t type);

/**
 * @brief 取选择页诊断名.
 *
 * @return 固定字符串 "select".
 */
const char *game_select_ui_page_name(void);

#ifdef __cplusplus
}
#endif

#endif /* GAME_SELECT_UI_H */
