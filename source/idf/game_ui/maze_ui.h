#ifndef MAZE_UI_H
#define MAZE_UI_H

/*
 * 迷宫设置/对局页, 不依赖贪吃蛇实现.
 */

#include <stdint.h>

#include "ad_keys.h"
#include "game_ui_common.h"
#include "maze_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAZE_UI_THEME_COUNT 5U

typedef enum {
    MAZE_UI_THEME_DESERT = 0,
    MAZE_UI_THEME_SNOW,
    MAZE_UI_THEME_FOREST,
    MAZE_UI_THEME_SPACE,
    MAZE_UI_THEME_OCEAN,
} maze_ui_theme_t;

/**
 * @brief 初始化迷宫逻辑和页状态, 不创建 LVGL 对象.
 *
 * @param on_back 返回游戏选择页的回调, 可为空.
 * @return 无.
 */
void maze_ui_init(game_ui_back_cb_t on_back);

/**
 * @brief 进入迷宫设置页并请求外壳重绘.
 *
 * @return 无.
 */
void maze_ui_enter_menu(void);

/**
 * @brief 按当前内部页渲染设置或对局. 必须在 LVGL 线程调用.
 *
 * @param user_data 未使用, 透传给具体渲染函数.
 * @return 无.
 */
void maze_ui_render(void *user_data);

/**
 * @brief 处理实体键: 菜单导航, 暂停选项或对局移动.
 *
 * @param key 实体键编号, 与 GAME_UI_KEY_* 对应.
 * @param type 按下或连发. 确认键只响应 PRESS.
 * @return 无.
 */
void maze_ui_handle_key(uint8_t key, ad_keys_event_type_t type);

/**
 * @brief 推进迷宫对局时间, 超时或角色变化时请求重绘.
 *
 * @param elapsed_ms 距上次调用的毫秒数, 0 表示只消化输入.
 * @return 无.
 */
void maze_ui_advance(uint32_t elapsed_ms);

/**
 * @brief 取当前迷宫页诊断名.
 *
 * @return "maze_menu", "maze" 或 "maze_paused".
 */
const char *maze_ui_page_name(void);

/**
 * @brief 取迷宫逻辑只读快照.
 *
 * @return 逻辑状态指针.
 */
const maze_state_t *maze_ui_state(void);

/**
 * @brief 设置迷宫墙/路配色风格. 非法值回落到沙漠.
 *
 * @param theme 沙漠/雪地/森林/太空/海洋.
 * @return 无.
 */
void maze_ui_set_theme(maze_ui_theme_t theme);

/**
 * @brief 取当前迷宫风格.
 *
 * @return 当前风格枚举.
 */
maze_ui_theme_t maze_ui_get_theme(void);

/**
 * @brief 固定下一局迷宫随机种子. 0 表示改回用 tick.
 *
 * @param seed 非 0 时开局使用该种子, 便于对照截图.
 * @return 无.
 */
void maze_ui_set_shot_seed(uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* MAZE_UI_H */
