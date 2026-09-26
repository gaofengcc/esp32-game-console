#ifndef KLOTSKI_UI_H
#define KLOTSKI_UI_H

/*
 * 华容道选关/对局页, 卡通风格, 面向小学生.
 * 不依赖贪吃蛇或迷宫实现, 只通过 game_ui_common / game_ui_port 与外壳协作.
 */

#include <stdint.h>

#include "ad_keys.h"
#include "game_ui_common.h"
#include "klotski_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化华容道逻辑和页状态, 加载各关最佳成绩, 不创建 LVGL 对象.
 *
 * @param on_back 返回游戏选择页的回调, 可为空.
 * @return 无.
 */
void klotski_ui_init(game_ui_back_cb_t on_back);

/**
 * @brief 进入选关页并请求外壳重绘.
 *
 * @return 无.
 */
void klotski_ui_enter_menu(void);

/**
 * @brief 按当前内部页渲染选关或对局. 必须在 LVGL 线程调用.
 *
 * @param user_data 未使用, 仅为对齐 game_ui_port_call 签名.
 * @return 无.
 */
void klotski_ui_render(void *user_data);

/**
 * @brief 处理实体键: 选关导航, 对局光标/棋子移动, 弹窗选项.
 *
 * @param key 实体键编号, 与 GAME_UI_KEY_* 对应.
 * @param type 按下或连发. 弹窗和 K5 只响应 PRESS, 方向键接受连发.
 * @return 无.
 */
void klotski_ui_handle_key(uint8_t key, ad_keys_event_type_t type);

/**
 * @brief 推进光标闪烁和胜利弹窗延时.
 *
 * @param elapsed_ms 距上次调用的毫秒数, 0 表示只消化输入.
 * @return 无.
 */
void klotski_ui_advance(uint32_t elapsed_ms);

/**
 * @brief 取当前华容道页诊断名.
 *
 * @return "klotski_select", "klotski", "klotski_paused" 或 "klotski_win".
 */
const char *klotski_ui_page_name(void);

/**
 * @brief 取华容道逻辑只读快照.
 *
 * @return 逻辑状态指针.
 */
const klotski_state_t *klotski_ui_state(void);

/**
 * @brief 直接进入指定关卡, 供模拟器场景和测试使用.
 *
 * @param level 关卡下标, 越界时忽略.
 * @return 无.
 */
void klotski_ui_start_level(uint8_t level);

/**
 * @brief 测试辅助: 把曹操直接放到出口并触发胜利流程.
 *
 * @return 无.
 */
void klotski_ui_force_win(void);

#ifdef __cplusplus
}
#endif

#endif /* KLOTSKI_UI_H */
