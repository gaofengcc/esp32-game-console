#pragma once

#include "game_ui_port.h"
#include "snake_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化贪吃蛇菜单、游戏页和 UI 刷新任务。 */
esp_err_t game_ui_init(void);

/* 设备任务和 PC 仿真器共用的时间推进入口。 */
void game_ui_update(uint32_t elapsed_ms);

/* 仿真/冒烟测试读取和构造场景时使用的最小辅助接口。 */
const snake_state_t *game_ui_get_state(void);
bool game_ui_force_food(snake_point_t food);
void game_ui_force_self_collision(void);

#ifdef __cplusplus
}
#endif
