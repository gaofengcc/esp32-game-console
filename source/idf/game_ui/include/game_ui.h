#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化贪吃蛇菜单、游戏页和 UI 刷新任务。 */
esp_err_t game_ui_init(void);

#ifdef __cplusplus
}
#endif
