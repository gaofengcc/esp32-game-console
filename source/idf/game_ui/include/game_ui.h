#pragma once

#include "game_ui_port.h"
#include "maze_logic.h"
#include "snake_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化游戏选择页, 以及贪吃蛇/迷宫的设置页和对局任务.
 *
 * @return ESP_OK 成功; 重复初始化也返回 ESP_OK.
 */
esp_err_t game_ui_init(void);

/**
 * @brief 当前页面名, 供诊断接口使用.
 *
 * @return 静态页名, 例如 select/menu/game/maze.
 */
const char *game_ui_get_page_name(void);

/**
 * @brief 迷宫状态快照, 未开始时 level 为 0.
 *
 * @return 只读迷宫状态指针.
 */
const maze_state_t *game_ui_get_maze_state(void);

/**
 * @brief 设备任务和 PC 仿真器共用的时间推进入口.
 *
 * @param elapsed_ms 距上次调用的毫秒数.
 * @return 无.
 */
void game_ui_update(uint32_t elapsed_ms);

/**
 * @brief 取贪吃蛇状态, 供仿真和冒烟测试读取.
 *
 * @return 只读贪吃蛇状态指针.
 */
const snake_state_t *game_ui_get_state(void);

/**
 * @brief 测试辅助: 把食物放到指定格子.
 *
 * @param food 目标坐标.
 * @return 放置成功为 true.
 */
bool game_ui_force_food(snake_point_t food);

/**
 * @brief 测试辅助: 切到贪吃蛇对局并摆出自撞形状.
 *
 * @return 无.
 */
void game_ui_force_self_collision(void);

/**
 * @brief 按蛇当前方向把触摸坐标映射成转弯输入.
 *
 * @param x 屏幕 X, 负值按 0 处理.
 * @param y 屏幕 Y, 负值按 0 处理.
 * @return 左右走映射上下, 上下走映射左右.
 */
snake_input_t game_ui_map_touch(int32_t x, int32_t y);

/**
 * @brief 打开或关闭贪吃蛇触摸转向.
 *
 * @param enabled true 允许点棋盘转向.
 * @return 无.
 */
void game_ui_set_touch_control(bool enabled);

/**
 * @brief 查询贪吃蛇触摸转向是否开启.
 *
 * @return 开启为 true.
 */
bool game_ui_get_touch_control(void);

/**
 * @brief 取蛇身扭动相位, 供仿真观测.
 *
 * @return 当前周期内的毫秒相位.
 */
uint32_t game_ui_get_wiggle_phase_ms(void);

/**
 * @brief 取某一节最近一次算出的扭动偏移.
 *
 * @param segment_index 蛇节下标, 0 是头.
 * @return 像素偏移; 越界返回 0.
 */
int16_t game_ui_get_wiggle_offset(uint16_t segment_index);

/**
 * @brief 取上一帧实际改过的蛇节对象数.
 *
 * @return 更新过的 LVGL 对象个数.
 */
uint16_t game_ui_get_wiggle_updated_objects(void);

#ifdef __cplusplus
}
#endif
