#ifndef MAZE_LOGIC_H
#define MAZE_LOGIC_H

/*
 * 迷宫纯 C 逻辑层.
 * 不依赖 ESP-IDF, FreeRTOS 或 LVGL, 设备和 PC 共用同一份状态机.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAZE_WIDTH
#define MAZE_WIDTH 29U
#endif
#ifndef MAZE_HEIGHT
#define MAZE_HEIGHT 17U
#endif
#define MAZE_CELL_COUNT (MAZE_WIDTH * MAZE_HEIGHT)
#define MAZE_LEVEL_MAX 30U
#define MAZE_LEVELS_PER_DIFF 3U
#define MAZE_TIMED_LIMIT_MS 120000U
#define MAZE_GUARD_STEP_MS 480U
#define MAZE_GUARD_SIGHT 2U
#define MAZE_ENTRANCE_SAFE_DIST 2U

typedef enum {
    MAZE_CELL_WALL = 0,
    MAZE_CELL_PATH = 1,
} maze_cell_kind_t;

typedef enum {
    MAZE_MODE_SIMPLE = 0,
    MAZE_MODE_TIMED,
    MAZE_MODE_CHALLENGE,
} maze_mode_t;

typedef enum {
    MAZE_DIR_UP = 0,
    MAZE_DIR_DOWN,
    MAZE_DIR_LEFT,
    MAZE_DIR_RIGHT,
} maze_dir_t;

typedef enum {
    MAZE_STATUS_READY = 0,
    MAZE_STATUS_RUNNING,
    MAZE_STATUS_PAUSED,
} maze_status_t;

typedef enum {
    MAZE_INPUT_NONE = 0,
    MAZE_INPUT_UP,
    MAZE_INPUT_DOWN,
    MAZE_INPUT_LEFT,
    MAZE_INPUT_RIGHT,
    MAZE_INPUT_PAUSE,
} maze_input_t;

typedef enum {
    MAZE_EVENT_NONE = 0,
    MAZE_EVENT_MOVED,
    MAZE_EVENT_BLOCKED,
    MAZE_EVENT_CAUGHT,
    MAZE_EVENT_LEVEL_CLEAR,
    MAZE_EVENT_TIMEOUT,
} maze_event_t;

typedef struct {
    uint8_t x;
    uint8_t y;
} maze_point_t;

typedef struct {
    uint8_t width;
    uint8_t height;
    uint8_t cells[MAZE_CELL_COUNT];
    maze_point_t entrance;
    maze_point_t exit_cell;
    maze_point_t player;
    maze_point_t guard;
    maze_point_t guard_spawn;
    maze_dir_t guard_dir;
    bool guard_active;
    maze_mode_t mode;
    uint16_t level;
    uint32_t remain_ms;
    uint32_t caught_count;
    uint32_t timeout_count;
    bool paused;
} maze_state_t;

typedef struct {
    maze_state_t state;
    uint32_t random_state;
    uint32_t guard_elapsed_ms;
} maze_game_t;

/**
 * @brief 清零并进入 READY, 默认简单模式.
 * @param game 迷宫实例, 不可为空.
 */
void maze_game_init(maze_game_t *game);

/**
 * @brief 固定随机种子, 便于单测复现同一座迷宫.
 */
void maze_game_seed(maze_game_t *game, uint32_t seed);

/**
 * @brief 设置模式, 对局中调用会在下一关或重开时生效.
 */
void maze_game_set_mode(maze_game_t *game, maze_mode_t mode);

maze_mode_t maze_game_get_mode(const maze_game_t *game);

/**
 * @brief 从第 1 关生成新迷宫并开始对局. 最多 30 关, 每 3 关升一档难度.
 */
void maze_game_start(maze_game_t *game);

/**
 * @brief 本关重来: 保留当前迷宫, 玩家回入口, 计时重置, 守卫回出口.
 */
void maze_game_retry_level(maze_game_t *game);

/**
 * @brief 处理方向或暂停. 方向键只在 RUNNING 时走一格.
 * @return 本次输入产生的事件.
 */
maze_event_t maze_game_set_input(maze_game_t *game, maze_input_t input);

/**
 * @brief 推进守卫走动和计时倒计时.
 * @return 本次推进里最后一次有效事件.
 */
maze_event_t maze_game_advance(maze_game_t *game, uint32_t elapsed_ms);

const maze_state_t *maze_game_state(const maze_game_t *game);
maze_status_t maze_game_get_status(const maze_game_t *game);
maze_cell_kind_t maze_game_cell(const maze_game_t *game, uint8_t x, uint8_t y);
bool maze_game_same_point(maze_point_t a, maze_point_t b);

/**
 * @brief 判断格子是否在守卫朝前 2 格视线内, 墙和入口安全区会挡住.
 */
bool maze_game_is_sight_cell(const maze_game_t *game, uint8_t x, uint8_t y);

#ifdef __cplusplus
}
#endif

#endif /* MAZE_LOGIC_H */
