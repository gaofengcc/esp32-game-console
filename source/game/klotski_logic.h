#ifndef KLOTSKI_LOGIC_H
#define KLOTSKI_LOGIC_H

/*
 * 华容道纯 C 逻辑层.
 * 不依赖 ESP-IDF, FreeRTOS 或 LVGL, 设备和 PC 模拟器共用同一份状态机.
 *
 * 棋盘固定 4 列 x 5 行, 棋子固定 10 个:
 *   0 曹操 2x2, 1 关羽 2x1 (横), 2-5 四竖将 1x2, 6-9 四兵 1x1.
 * 胜利条件: 曹操 anchor (最小格号) 到达 (1,3), 即底部中间出口.
 * 关卡表由 tools/klotski_solver 生成 (klotski_levels.h), min_steps 为
 * 单格滑动口径的实测最少步数.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KLOTSKI_COLS 4U
#define KLOTSKI_ROWS 5U
#define KLOTSKI_CELL_COUNT (KLOTSKI_COLS * KLOTSKI_ROWS)
#define KLOTSKI_PIECE_COUNT 10U
#define KLOTSKI_LEVEL_COUNT 24U

#define KLOTSKI_PIECE_CAO 0U
#define KLOTSKI_PIECE_GUAN 1U
#define KLOTSKI_PIECE_GENERAL_FIRST 2U
#define KLOTSKI_PIECE_SOLDIER_FIRST 6U

/* 曹操 anchor 的胜利位置: 底部中间 */
#define KLOTSKI_WIN_X 1U
#define KLOTSKI_WIN_Y 3U

typedef enum {
    KLOTSKI_TIER_EASY = 0,
    KLOTSKI_TIER_NORMAL,
    KLOTSKI_TIER_MASTER,
} klotski_tier_t;

typedef enum {
    KLOTSKI_DIR_UP = 0,
    KLOTSKI_DIR_DOWN,
    KLOTSKI_DIR_LEFT,
    KLOTSKI_DIR_RIGHT,
} klotski_dir_t;

typedef enum {
    KLOTSKI_EVENT_NONE = 0,
    KLOTSKI_EVENT_MOVED,
    KLOTSKI_EVENT_BLOCKED,
    KLOTSKI_EVENT_WIN,
} klotski_event_t;

/* 关卡初始布局: 全部存 anchor 格号 (y * 4 + x), 竖将和兵各自升序 */
typedef struct {
    /* 以左上角 anchor 格号保存布局，尺寸由棋子编号决定。 */
    uint8_t cao;
    uint8_t guan;
    uint8_t generals[4];
    uint8_t soldiers[4];
} klotski_layout_t;

typedef struct {
    /* 关卡表中的展示名、难度和求解器实测最少步数。 */
    const char *name;
    uint8_t tier;
    uint16_t min_steps;
    klotski_layout_t layout;
} klotski_level_def_t;

typedef struct {
    /* 棋子左上角坐标与逻辑占用尺寸。 */
    uint8_t x;
    uint8_t y;
    uint8_t w;
    uint8_t h;
} klotski_piece_t;

typedef struct {
    /* 当前棋子数组、已走单格步数和胜利标志。 */
    klotski_piece_t pieces[KLOTSKI_PIECE_COUNT];
    uint16_t steps;
    uint8_t level;
    bool won;
} klotski_state_t;

typedef struct {
    klotski_state_t state;
} klotski_game_t;

/**
 * @brief 清零实例, 不加载任何关卡.
 *
 * @param game 实例, 不可为空.
 * @return 无.
 */
void klotski_game_init(klotski_game_t *game);

/**
 * @brief 加载指定关卡, 步数清零, 棋子回到初始布局.
 *
 * @param game 实例, 不可为空.
 * @param level 关卡下标, 必须小于 KLOTSKI_LEVEL_COUNT.
 * @return 加载成功为 true; 下标越界返回 false 且实例不变.
 */
bool klotski_game_load_level(klotski_game_t *game, uint8_t level);

/**
 * @brief 把指定棋子朝一个方向滑动一格.
 *
 * @param game 实例, 不可为空.
 * @param piece 棋子下标, 必须小于 KLOTSKI_PIECE_COUNT.
 * @param dir 方向.
 * @return MOVED 移动成功; BLOCKED 被边界或其他棋子挡住;
 *         WIN 移动后曹操到达出口; 参数非法或已胜利返回 NONE.
 */
klotski_event_t klotski_game_move(klotski_game_t *game, uint8_t piece,
                                  klotski_dir_t dir);

/**
 * @brief 查询某格被哪个棋子占据.
 *
 * @param game 实例, 不可为空.
 * @param x 列, 必须小于 KLOTSKI_COLS.
 * @param y 行, 必须小于 KLOTSKI_ROWS.
 * @return 棋子下标; 空格或坐标越界返回 -1.
 */
int klotski_game_piece_at(const klotski_game_t *game, uint8_t x, uint8_t y);

/**
 * @brief 取只读状态快照.
 *
 * @param game 实例, 不可为空.
 * @return 状态指针.
 */
const klotski_state_t *klotski_game_state(const klotski_game_t *game);

/**
 * @brief 取关卡定义.
 *
 * @param level 关卡下标.
 * @return 定义指针; 下标越界返回 NULL.
 */
const klotski_level_def_t *klotski_level_def(uint8_t level);

/**
 * @brief 取棋子显示名.
 *
 * @param piece 棋子下标.
 * @return 静态字符串; 下标越界返回空串.
 */
const char *klotski_piece_name(uint8_t piece);

/**
 * @brief 判断格子是否属于底部中间出口两格.
 *
 * @param x 列.
 * @param y 行.
 * @return 属于出口为 true.
 */
bool klotski_is_exit_cell(uint8_t x, uint8_t y);

#ifdef __cplusplus
}
#endif

#endif /* KLOTSKI_LOGIC_H */
