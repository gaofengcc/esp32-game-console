#ifndef SNAKE_LOGIC_H
#define SNAKE_LOGIC_H

/*
 * 贪吃蛇纯 C 逻辑层。
 * 本文件不依赖 ESP-IDF、FreeRTOS 或 LVGL，设备和 PC 共用同一份状态机。
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SNAKE_BOARD_WIDTH
#define SNAKE_BOARD_WIDTH 30U
#endif
#ifndef SNAKE_BOARD_HEIGHT
#define SNAKE_BOARD_HEIGHT 18U
#endif
#define SNAKE_MAX_SEGMENTS (SNAKE_BOARD_WIDTH * SNAKE_BOARD_HEIGHT)
#define SNAKE_MAX_CELLS SNAKE_MAX_SEGMENTS

#define SNAKE_SPEED_SLOW_MS 260U
#define SNAKE_SPEED_MEDIUM_MS 180U
#define SNAKE_SPEED_FAST_MS 120U
#define SNAKE_MIN_SPEED_MS 100U
/* 每吃 1 个果子缩短的步进间隔, 数值越小越快. */
#ifndef SNAKE_SPEED_PER_FOOD_MS
#define SNAKE_SPEED_PER_FOOD_MS 8U
#endif

typedef struct {
    uint8_t x;
    uint8_t y;
} snake_point_t;

typedef enum {
    SNAKE_INPUT_NONE = 0,
    SNAKE_INPUT_UP,
    SNAKE_INPUT_DOWN,
    SNAKE_INPUT_LEFT,
    SNAKE_INPUT_RIGHT,
    SNAKE_INPUT_PAUSE,
} snake_input_t;

typedef enum {
    SNAKE_DIRECTION_UP = 0,
    SNAKE_DIRECTION_DOWN,
    SNAKE_DIRECTION_LEFT,
    SNAKE_DIRECTION_RIGHT,
} snake_direction_t;

typedef enum {
    SNAKE_STATUS_READY = 0,
    SNAKE_STATUS_RUNNING,
    SNAKE_STATUS_PAUSED,
    SNAKE_STATUS_GAME_OVER,
} snake_status_t;

typedef enum {
    SNAKE_SPEED_LEVEL_SLOW = 0,
    SNAKE_SPEED_LEVEL_MEDIUM,
    SNAKE_SPEED_LEVEL_FAST,
} snake_speed_level_t;

/* 游戏结束原因仅用于 UI 提示，不改变原有规则。 */
typedef enum {
    SNAKE_GAME_OVER_NONE = 0,
    SNAKE_GAME_OVER_SELF_COLLISION,
    SNAKE_GAME_OVER_WALL,
    SNAKE_GAME_OVER_BOARD_FULL,
} snake_game_over_reason_t;

typedef int (*snake_score_load_fn)(void *ctx, int *score);
typedef int (*snake_score_save_fn)(void *ctx, int score);

typedef struct {
    uint8_t width;
    uint8_t height;
    uint16_t initial_speed_ms;
    bool wrap_walls;
    snake_score_load_fn load_best;
    snake_score_save_fn save_best;
    void *storage_ctx;
} snake_config_t;

typedef struct {
    uint8_t width;
    uint8_t height;
    snake_point_t segments[SNAKE_MAX_SEGMENTS];
    uint16_t length;
    snake_point_t food;
    int score;
    int best_score;
    uint16_t speed_ms;
    bool wrap_walls;
    bool game_over;
    bool paused;
    uint32_t foods_eaten;
    snake_game_over_reason_t game_over_reason;
} snake_state_t;

typedef struct {
    snake_state_t state;
    snake_config_t config;
    snake_direction_t direction;
    snake_direction_t pending_direction;
    uint32_t elapsed_ms;
    uint32_t random_state;
    bool food_valid;
} snake_game_t;

/* 生成默认配置：30x18、慢速 260ms、默认穿墙。 */
void snake_config_default(snake_config_t *config);

/* 初始化并读取最高分；初始化后状态为 READY，调用 reset 开始游戏。 */
void snake_game_init(snake_game_t *game, const snake_config_t *config);

/* 重置并开始一局新游戏。 */
void snake_game_reset(snake_game_t *game);

/* 设置输入；反向输入忽略，暂停键在运行/暂停间切换。 */
void snake_game_set_input(snake_game_t *game, snake_input_t input);

/* 按 elapsed_ms 推进时间，内部按 speed_ms 执行若干格移动。 */
void snake_game_advance(snake_game_t *game, uint32_t elapsed_ms);

/* 立即执行一步；成功移动返回 true。 */
bool snake_game_step(snake_game_t *game);

/* 取得只读状态快照。 */
const snake_state_t *snake_game_state(const snake_game_t *game);
bool snake_game_is_over(const snake_game_t *game);

/* 测试/回放辅助：固定随机种子或直接指定食物坐标。 */
void snake_game_seed(snake_game_t *game, uint32_t seed);
bool snake_game_force_food(snake_game_t *game, snake_point_t food);

/* 额外查询/配置接口，便于设备 UI 使用。 */
snake_status_t snake_game_get_status(const snake_game_t *game);
snake_direction_t snake_game_get_direction(const snake_game_t *game);
snake_game_over_reason_t snake_game_get_over_reason(const snake_game_t *game);
uint16_t snake_game_get_speed_ms(const snake_game_t *game);
void snake_game_set_wrap(snake_game_t *game, bool enabled);
void snake_game_set_speed_level(snake_game_t *game, snake_speed_level_t level);
void snake_game_reload_high_score(snake_game_t *game);

/* 兼容设备 UI 的便捷包装。 */
void snake_game_start(snake_game_t *game);
void snake_game_handle_input(snake_game_t *game, snake_input_t input);
uint32_t snake_game_update(snake_game_t *game, uint32_t elapsed_ms);
uint32_t snake_game_get_score(const snake_game_t *game);
uint32_t snake_game_get_high_score(const snake_game_t *game);
const snake_point_t *snake_game_get_segments(const snake_game_t *game,
                                             uint16_t *length);
snake_point_t snake_game_get_food(const snake_game_t *game);

#ifdef __cplusplus
}
#endif

#endif /* SNAKE_LOGIC_H */
