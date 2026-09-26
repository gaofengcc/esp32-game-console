#ifndef SNAKE_UI_H
#define SNAKE_UI_H

/*
 * 贪吃蛇设置/对局/结束页, 不依赖迷宫或选择页实现.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ad_keys.h"
#include "game_ui_common.h"
#include "snake_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化贪吃蛇逻辑和页状态, 不创建 LVGL 对象.
 *
 * @param on_back 返回游戏选择页的回调, 可为空.
 * @return 无.
 */
void snake_ui_init(game_ui_back_cb_t on_back);

/**
 * @brief 进入贪吃蛇设置页并请求外壳重绘.
 *
 * @return 无.
 */
void snake_ui_enter_menu(void);

/**
 * @brief 按当前内部页渲染设置, 对局或结束页. 必须在 LVGL 线程调用.
 *
 * @param user_data 未使用, 透传给具体渲染函数.
 * @return 无.
 */
void snake_ui_render(void *user_data);

/**
 * @brief 处理实体键: 菜单, 结束页, 暂停或对局转向.
 *
 * @param key 实体键编号, 与 GAME_UI_KEY_* 对应.
 * @param type 按下或连发. 确认键只响应 PRESS.
 * @return 无.
 */
void snake_ui_handle_key(uint8_t key, ad_keys_event_type_t type);

/**
 * @brief 推进贪吃蛇对局和扭动相位.
 *
 * @param elapsed_ms 距上次调用的毫秒数.
 * @return 无.
 */
void snake_ui_advance(uint32_t elapsed_ms);

/**
 * @brief 查询蛇身扭动是否需要刷新位置.
 *
 * @return 有脏标记为 true.
 */
bool snake_ui_wiggle_dirty(void);

/**
 * @brief 清掉扭动脏标记.
 *
 * @return 无.
 */
void snake_ui_clear_wiggle_dirty(void);

/**
 * @brief 取当前贪吃蛇页诊断名.
 *
 * @return "menu", "game", "paused" 或 "end".
 */
const char *snake_ui_page_name(void);

/**
 * @brief 取贪吃蛇逻辑只读快照.
 *
 * @return 逻辑状态指针.
 */
const snake_state_t *snake_ui_state(void);

/**
 * @brief 测试辅助: 把食物放到指定格子.
 *
 * @param food 目标坐标.
 * @return 放置成功为 true.
 */
bool snake_ui_force_food(snake_point_t food);

/**
 * @brief 测试辅助: 切到对局页并摆出自撞形状.
 *
 * @return 无.
 */
void snake_ui_force_self_collision(void);

/**
 * @brief 按蛇当前方向把触摸坐标映射成转弯输入.
 *
 * @param x 屏幕 X, 负值按 0 处理.
 * @param y 屏幕 Y, 负值按 0 处理.
 * @return 左右走映射上下, 上下走映射左右.
 */
snake_input_t snake_ui_map_touch(int32_t x, int32_t y);

/**
 * @brief 打开或关闭触摸转向.
 *
 * @param enabled true 允许点棋盘转向.
 * @return 无.
 */
void snake_ui_set_touch_control(bool enabled);

/**
 * @brief 查询触摸转向是否开启.
 *
 * @return 开启为 true.
 */
bool snake_ui_get_touch_control(void);

/**
 * @brief 取蛇身扭动相位.
 *
 * @return 当前周期内的毫秒相位.
 */
uint32_t snake_ui_get_wiggle_phase_ms(void);

/**
 * @brief 取某一节最近一次算出的扭动偏移.
 *
 * @param segment_index 蛇节下标, 0 是头.
 * @return 像素偏移; 越界返回 0.
 */
int16_t snake_ui_get_wiggle_offset(uint16_t segment_index);

/**
 * @brief 取上一帧实际改过的蛇节对象数.
 *
 * @return 更新过的 LVGL 对象个数.
 */
uint16_t snake_ui_get_wiggle_updated_objects(void);

#ifdef __cplusplus
}
#endif

#endif /* SNAKE_UI_H */
