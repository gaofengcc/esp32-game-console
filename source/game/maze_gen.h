#ifndef MAZE_GEN_H
#define MAZE_GEN_H

/*
 * 迷宫生成模块.
 * 只负责按关卡雕通路和选出口, 不碰计时, 守卫, 输入状态机.
 */

#include "maze_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAZE_GEN_TRY_MAX 24U
#define MAZE_GEN_DIFFICULTY_COUNT 10U
#define MAZE_GEN_REASONABLE_DEPTH 3U

typedef enum {
    MAZE_GEN_ALGO_BINARY = 0,
    MAZE_GEN_ALGO_SIDEWINDER,
    MAZE_GEN_ALGO_GROW,
    MAZE_GEN_ALGO_KRUSKAL,
} maze_gen_algo_t;

typedef struct {
    uint8_t width;
    uint8_t height;
    uint8_t *cells;
    maze_point_t entrance;
    maze_point_t exit_cell;
} maze_gen_map_t;

typedef struct {
    uint16_t junctions;
    uint16_t tree_depth;
    uint16_t branches;
    uint16_t path_len;
} maze_gen_metrics_t;

typedef struct {
    uint8_t rooms_x;
    uint8_t rooms_y;
    maze_gen_algo_t algo;
    uint8_t newest_pct;
    uint8_t straight_pct;
    uint8_t min_path_dist;
    uint8_t target_path;
    uint8_t min_junctions;
    const char *name;
} maze_gen_diff_t;

/**
 * @brief 关卡号映射到 1-10 档. 每 3 关一档, 超过 30 关锁在第 10 档.
 *
 * @param level 关卡号, 0 按 1 处理.
 * @return 难度档 1-10.
 */
uint8_t maze_gen_difficulty_of_level(uint16_t level);

/**
 * @brief 量当前地图的正解分叉, 死胡同树和路径长度.
 *
 * @param map 已雕好的地图, 不可为空.
 * @param out 输出指标, 不可为空.
 * @return 入口能走到出口为 true.
 *
 * @note 不可重入. 仅任务上下文调用.
 */
bool maze_gen_measure(const maze_gen_map_t *map, maze_gen_metrics_t *out);

/**
 * @brief 按关卡雕满屏迷宫: 映射到 10 档, 按目标步数选出口.
 *
 * @param map 输出地图, cells 由调用方提供, 不可为空.
 * @param level 关卡号, 从 1 起, 0 按 1 处理, 超过 30 按 30.
 * @param rng 线性同余种子, 不可为空, 与逻辑层同一公式.
 * @return 参数合法且雕出连通迷宫为 true.
 *
 * @note 不改计时和守卫. 不可重入, 不可在 ISR 调用.
 */
bool maze_gen_create(maze_gen_map_t *map, uint16_t level, uint32_t *rng);

/**
 * @brief 取 1-10 档难度规格. 非法值回落到第 1 档.
 *
 * @param difficulty 难度档, 从 1 起.
 * @return 静态表项指针, 不会为空.
 */
const maze_gen_diff_t *maze_gen_diff_spec(uint8_t difficulty);

/**
 * @brief 按难度档雕满屏迷宫: 只改弯绕和算法, 不缩小地盘.
 *
 * @param map 输出地图, cells 由调用方提供, 不可为空.
 * @param difficulty 1-10.
 * @param rng 线性同余种子, 不可为空.
 * @return 雕出连通迷宫为 true.
 *
 * @note 不改计时和守卫. 不可重入, 不可在 ISR 调用.
 */
bool maze_gen_create_difficulty(maze_gen_map_t *map, uint8_t difficulty,
                                uint32_t *rng);

#ifdef __cplusplus
}
#endif

#endif /* MAZE_GEN_H */
