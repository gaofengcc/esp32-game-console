#include "maze_gen.h"

#include <string.h>

#define MAZE_GEN_ROOM_MAX 112U
#define MAZE_GEN_INF 0xFFFFU

/* 10 档: 满屏迷宫, 不用 Binary/Sidewinder 那种假纹理. */
static const maze_gen_diff_t s_diffs[MAZE_GEN_DIFFICULTY_COUNT] = {
    {14U, 8U, MAZE_GEN_ALGO_GROW, 80U, 40U, 12U, 18U, 2U, "EasyA"},
    {14U, 8U, MAZE_GEN_ALGO_GROW, 75U, 30U, 16U, 28U, 3U, "EasyB"},
    {14U, 8U, MAZE_GEN_ALGO_GROW, 68U, 20U, 20U, 38U, 4U, "EasyC"},
    {14U, 8U, MAZE_GEN_ALGO_GROW, 58U, 10U, 24U, 48U, 5U, "MidA"},
    {14U, 8U, MAZE_GEN_ALGO_GROW, 48U, 0U, 28U, 58U, 6U, "MidB"},
    {14U, 8U, MAZE_GEN_ALGO_GROW, 38U, 0U, 32U, 70U, 7U, "MidC"},
    {14U, 8U, MAZE_GEN_ALGO_GROW, 26U, 0U, 36U, 84U, 8U, "HardA"},
    {14U, 8U, MAZE_GEN_ALGO_GROW, 14U, 0U, 40U, 100U, 9U, "HardB"},
    {14U, 8U, MAZE_GEN_ALGO_KRUSKAL, 0U, 0U, 44U, 116U, 10U, "Kruskal"},
    {14U, 8U, MAZE_GEN_ALGO_GROW, 0U, 0U, 48U, 0U, 11U, "Prim"},
};

/* 生成期工作区: 约 5KB BSS, 避免占用 game_ui 任务栈；不可重入。 */
static uint16_t s_dist[MAZE_CELL_COUNT];
static int16_t s_parent[MAZE_CELL_COUNT];
static uint16_t s_queue[MAZE_CELL_COUNT];
static uint16_t s_tree_dist[MAZE_CELL_COUNT];
static uint8_t s_on_path[MAZE_CELL_COUNT];
static uint8_t s_best_cells[MAZE_CELL_COUNT];

/**
 * @brief 线性同余随机数, 与 maze_logic / snake_logic 同一套.
 *
 * @param rng 种子指针, 调用方保证非空.
 * @return 下一步随机值.
 */
static uint32_t maze_gen_rand(uint32_t *rng)
{
    *rng = (*rng) * 1664525U + 1013904223U;
    return *rng;
}

/**
 * @brief 把坐标折成线性下标.
 *
 * @param width 列数.
 * @param x 列.
 * @param y 行.
 * @return y * width + x.
 */
static uint16_t maze_gen_index(uint8_t width, uint8_t x, uint8_t y)
{
    return (uint16_t)((uint16_t)y * (uint16_t)width + (uint16_t)x);
}

/**
 * @brief 判断格子是否在地图内.
 *
 * @param map 地图.
 * @param x 列, 可为负.
 * @param y 行, 可为负.
 * @return 在界内为 true.
 */
static bool maze_gen_inside(const maze_gen_map_t *map, int16_t x, int16_t y)
{
    if (!map) {
        return false;
    }
    return (x >= 0) && (y >= 0) && (x < (int16_t)map->width) &&
           (y < (int16_t)map->height);
}

/**
 * @brief 判断格子是否为通路.
 *
 * @param map 地图, 调用方保证非空.
 * @param x 列.
 * @param y 行.
 * @return 界内且为通路.
 */
static bool maze_gen_is_path(const maze_gen_map_t *map, int16_t x, int16_t y)
{
    uint16_t index;

    if (!maze_gen_inside(map, x, y) || !map->cells) {
        return false;
    }
    index = maze_gen_index(map->width, (uint8_t)x, (uint8_t)y);
    return map->cells[index] == (uint8_t)MAZE_CELL_PATH;
}

/**
 * @brief 统计通路邻居数, 即格子度数.
 *
 * @param map 地图, 调用方保证非空.
 * @param x 列.
 * @param y 行.
 * @return 0 到 4.
 */
static uint8_t maze_gen_degree(const maze_gen_map_t *map, uint8_t x, uint8_t y)
{
    static const int8_t dx[4] = {0, 0, -1, 1};
    static const int8_t dy[4] = {-1, 1, 0, 0};
    uint8_t i;
    uint8_t degree = 0U;

    for (i = 0U; i < 4U; ++i) {
        if (maze_gen_is_path(map, (int16_t)x + dx[i], (int16_t)y + dy[i])) {
            degree++;
        }
    }
    return degree;
}

/**
 * @brief 从入口 BFS, 填距离和父指针.
 *
 * @param map 地图, 调用方保证非空且 cells 合法.
 * @return 访问到的通路格数.
 */
static uint16_t maze_gen_bfs(const maze_gen_map_t *map)
{
    /* s_dist/s_parent 是当前入口的一次 BFS 结果，后续候选出口复用。 */
    static const int8_t dx[4] = {0, 0, -1, 1};
    static const int8_t dy[4] = {-1, 1, 0, 0};
    uint16_t i;
    uint16_t count = (uint16_t)map->width * (uint16_t)map->height;
    uint16_t head = 0U;
    uint16_t tail = 0U;
    uint16_t start;

    if (count > MAZE_CELL_COUNT) {
        count = MAZE_CELL_COUNT;
    }
    for (i = 0U; i < count; ++i) {
        s_dist[i] = MAZE_GEN_INF;
        s_parent[i] = -1;
    }
    if (!maze_gen_is_path(map, (int16_t)map->entrance.x,
                          (int16_t)map->entrance.y)) {
        return 0U;
    }
    start = maze_gen_index(map->width, map->entrance.x, map->entrance.y);
    s_dist[start] = 0U;
    s_queue[tail++] = start;
    while (head < tail) {
        uint16_t cur = s_queue[head++];
        uint8_t cx = (uint8_t)(cur % map->width);
        uint8_t cy = (uint8_t)(cur / map->width);

        for (i = 0U; i < 4U; ++i) {
            int16_t nx = (int16_t)cx + dx[i];
            int16_t ny = (int16_t)cy + dy[i];
            uint16_t nidx;

            if (!maze_gen_is_path(map, nx, ny)) {
                continue;
            }
            nidx = maze_gen_index(map->width, (uint8_t)nx, (uint8_t)ny);
            if (s_dist[nidx] != MAZE_GEN_INF) {
                continue;
            }
            s_dist[nidx] = (uint16_t)(s_dist[cur] + 1U);
            s_parent[nidx] = (int16_t)cur;
            if (tail < MAZE_CELL_COUNT) {
                s_queue[tail++] = nidx;
            }
        }
    }
    return tail;
}

/**
 * @brief 按父指针把入口到出口标成正解.
 *
 * @param map 地图, 调用方保证非空.
 * @param exit_idx 出口下标.
 * @param count 格子总数, 不超过 MAZE_CELL_COUNT.
 * @return 标出的正解格数, 出口不可达时为 0.
 */
static uint16_t maze_gen_mark_path(const maze_gen_map_t *map, uint16_t exit_idx,
                                   uint16_t count)
{
    uint16_t i;
    uint16_t marked = 0U;
    int16_t cur;

    for (i = 0U; i < count; ++i) {
        s_on_path[i] = 0U;
    }
    if ((exit_idx >= count) || (s_dist[exit_idx] == MAZE_GEN_INF)) {
        return 0U;
    }
    cur = (int16_t)exit_idx;
    while (cur >= 0) {
        s_on_path[cur] = 1U;
        marked++;
        if (cur == (int16_t)maze_gen_index(map->width, map->entrance.x,
                                           map->entrance.y)) {
            break;
        }
        cur = s_parent[cur];
    }
    return marked;
}

/**
 * @brief 从正解旁的一格量死胡同树深度.
 *
 * @param map 地图, 调用方保证非空.
 * @param start_x 树根列, 必须不在正解上.
 * @param start_y 树根行.
 * @return 树的最大深度, 根为 1.
 */
static uint16_t maze_gen_tree_depth(const maze_gen_map_t *map, uint8_t start_x,
                                    uint8_t start_y)
{
    static const int8_t dx[4] = {0, 0, -1, 1};
    static const int8_t dy[4] = {-1, 1, 0, 0};
    uint16_t count = (uint16_t)map->width * (uint16_t)map->height;
    uint16_t start;
    uint16_t head = 0U;
    uint16_t tail = 0U;
    uint16_t i;
    uint16_t max_depth = 0U;

    if (count > MAZE_CELL_COUNT) {
        count = MAZE_CELL_COUNT;
    }
    start = maze_gen_index(map->width, start_x, start_y);
    if ((start >= count) || (s_on_path[start] != 0U) ||
        !maze_gen_is_path(map, (int16_t)start_x, (int16_t)start_y)) {
        return 0U;
    }
    for (i = 0U; i < count; ++i) {
        s_tree_dist[i] = MAZE_GEN_INF;
    }
    s_tree_dist[start] = 1U;
    s_queue[tail++] = start;
    while (head < tail) {
        uint16_t cur = s_queue[head++];
        uint8_t cx = (uint8_t)(cur % map->width);
        uint8_t cy = (uint8_t)(cur / map->width);

        if (s_tree_dist[cur] > max_depth) {
            max_depth = s_tree_dist[cur];
        }
        for (i = 0U; i < 4U; ++i) {
            int16_t nx = (int16_t)cx + dx[i];
            int16_t ny = (int16_t)cy + dy[i];
            uint16_t nidx;

            if (!maze_gen_is_path(map, nx, ny)) {
                continue;
            }
            nidx = maze_gen_index(map->width, (uint8_t)nx, (uint8_t)ny);
            if ((nidx >= count) || (s_on_path[nidx] != 0U) ||
                (s_tree_dist[nidx] != MAZE_GEN_INF)) {
                continue;
            }
            s_tree_dist[nidx] = (uint16_t)(s_tree_dist[cur] + 1U);
            if (tail < MAZE_CELL_COUNT) {
                s_queue[tail++] = nidx;
            }
        }
    }
    return max_depth;
}

/**
 * @brief 在已做完 BFS 的前提下, 按指定出口收指标.
 *
 * @param map 地图, 调用方保证非空.
 * @param exit_cell 候选出口.
 * @param out 输出, 调用方保证非空.
 * @return 出口可达为 true.
 */
static bool maze_gen_measure_exit(const maze_gen_map_t *map,
                                  maze_point_t exit_cell,
                                  maze_gen_metrics_t *out)
{
    static const int8_t dx[4] = {0, 0, -1, 1};
    static const int8_t dy[4] = {-1, 1, 0, 0};
    uint16_t count = (uint16_t)map->width * (uint16_t)map->height;
    uint16_t exit_idx;
    uint16_t i;
    uint16_t marked;

    /* 先标出入口到出口的正解，再统计正解上的分叉及旁支深度。 */
    memset(out, 0, sizeof(*out));
    if (count > MAZE_CELL_COUNT) {
        count = MAZE_CELL_COUNT;
    }
    if (!maze_gen_is_path(map, (int16_t)exit_cell.x, (int16_t)exit_cell.y)) {
        return false;
    }
    exit_idx = maze_gen_index(map->width, exit_cell.x, exit_cell.y);
    if ((exit_idx >= count) || (s_dist[exit_idx] == MAZE_GEN_INF)) {
        return false;
    }
    out->path_len = s_dist[exit_idx];
    marked = maze_gen_mark_path(map, exit_idx, count);
    if (marked == 0U) {
        return false;
    }
    for (i = 0U; i < count; ++i) {
        uint8_t x;
        uint8_t y;
        uint8_t degree;
        uint8_t n;

        if (s_on_path[i] == 0U) {
            continue;
        }
        x = (uint8_t)(i % map->width);
        y = (uint8_t)(i / map->width);
        degree = maze_gen_degree(map, x, y);
        if (degree < 3U) {
            continue;
        }
        out->junctions++;
        for (n = 0U; n < 4U; ++n) {
            int16_t nx = (int16_t)x + dx[n];
            int16_t ny = (int16_t)y + dy[n];
            uint16_t depth;

            if (!maze_gen_is_path(map, nx, ny)) {
                continue;
            }
            if (s_on_path[maze_gen_index(map->width, (uint8_t)nx,
                                         (uint8_t)ny)] != 0U) {
                continue;
            }
            depth = maze_gen_tree_depth(map, (uint8_t)nx, (uint8_t)ny);
            if (depth > out->tree_depth) {
                out->tree_depth = depth;
            }
            if (depth >= MAZE_GEN_REASONABLE_DEPTH) {
                out->branches++;
            }
        }
    }
    return true;
}

/**
 * @brief 把指标收成可选出口的分数. 步数只作弱约束.
 *
 * @param metrics 已量好的指标.
 * @param exit_degree 出口度数, 1 表示藏在死胡同.
 * @return 越大越难.
 */
static uint32_t maze_gen_score(const maze_gen_metrics_t *metrics,
                               uint8_t exit_degree, uint8_t min_path)
{
    uint32_t score;

    if (!metrics || (metrics->path_len < (uint16_t)min_path)) {
        return 0U;
    }
    /* 支路条数优先, 不再奖励最大树深, 避免长走廊虚高 */
    score = ((uint32_t)metrics->junctions * 10U) +
            ((uint32_t)metrics->branches * 14U);
    if (exit_degree == 1U) {
        score += 8U;
    }
    return score;
}

/**
 * @brief 打通两个房间之间的墙和目标房间.
 *
 * @param map 地图, 调用方保证非空.
 * @param x0 起点列.
 * @param y0 起点行.
 * @param x1 终点列.
 * @param y1 终点行.
 * @return 无.
 */
static void maze_gen_carve_edge(maze_gen_map_t *map, uint8_t x0, uint8_t y0,
                                uint8_t x1, uint8_t y1)
{
    uint8_t wx = (uint8_t)((x0 + x1) / 2U);
    uint8_t wy = (uint8_t)((y0 + y1) / 2U);

    map->cells[maze_gen_index(map->width, wx, wy)] = (uint8_t)MAZE_CELL_PATH;
    map->cells[maze_gen_index(map->width, x1, y1)] = (uint8_t)MAZE_CELL_PATH;
}

/**
 * @brief Growing Tree 在指定房间矩形内雕刻.
 *
 * @param map 地图, 调用方保证非空.
 * @param ox 左上房间列.
 * @param oy 左上房间行.
 * @param rooms_x 横向房间数.
 * @param rooms_y 纵向房间数.
 * @param newest_pct 取最新房间的百分比, 0-100.
 * @param straight_pct 优先沿上次方向直走的百分比, 0-100.
 * @param rng 随机种子, 调用方保证非空.
 * @return 无.
 */
static void maze_gen_carve_grow(maze_gen_map_t *map, uint8_t ox, uint8_t oy,
                                uint8_t rooms_x, uint8_t rooms_y,
                                uint8_t newest_pct, uint8_t straight_pct,
                                uint32_t *rng)
{
    maze_point_t rooms[MAZE_GEN_ROOM_MAX];
    uint8_t last_dir[MAZE_GEN_ROOM_MAX];
    uint16_t count = 0U;
    const int8_t step_x[4] = {0, 0, -2, 2};
    const int8_t step_y[4] = {-2, 2, 0, 0};
    int16_t x_max = (int16_t)ox + (int16_t)((rooms_x - 1U) * 2U);
    int16_t y_max = (int16_t)oy + (int16_t)((rooms_y - 1U) * 2U);

    map->entrance.x = ox;
    map->entrance.y = oy;
    map->cells[maze_gen_index(map->width, ox, oy)] = (uint8_t)MAZE_CELL_PATH;
    rooms[0] = map->entrance;
    last_dir[0] = 3U;
    count = 1U;

    while (count > 0U) {
        uint16_t idx;
        maze_point_t cur;
        uint8_t order[4] = {0U, 1U, 2U, 3U};
        uint8_t i;
        bool carved = false;

        if ((newest_pct >= 100U) ||
            ((maze_gen_rand(rng) % 100U) < (uint32_t)newest_pct)) {
            idx = (uint16_t)(count - 1U);
        } else {
            idx = (uint16_t)(maze_gen_rand(rng) % count);
        }
        cur = rooms[idx];

        for (i = 3U; i > 0U; --i) {
            uint8_t swap = (uint8_t)(maze_gen_rand(rng) % ((uint32_t)i + 1U));
            uint8_t tmp = order[i];

            order[i] = order[swap];
            order[swap] = tmp;
        }
        if ((straight_pct > 0U) &&
            ((maze_gen_rand(rng) % 100U) < (uint32_t)straight_pct)) {
            uint8_t prefer = last_dir[idx];

            for (i = 0U; i < 4U; ++i) {
                if (order[i] == prefer) {
                    order[i] = order[0];
                    order[0] = prefer;
                    break;
                }
            }
        }
        for (i = 0U; i < 4U; ++i) {
            int16_t nx = (int16_t)cur.x + step_x[order[i]];
            int16_t ny = (int16_t)cur.y + step_y[order[i]];

            if ((nx < (int16_t)ox) || (ny < (int16_t)oy) || (nx > x_max) ||
                (ny > y_max)) {
                continue;
            }
            if (map->cells[maze_gen_index(map->width, (uint8_t)nx,
                                          (uint8_t)ny)] !=
                (uint8_t)MAZE_CELL_WALL) {
                continue;
            }
            maze_gen_carve_edge(map, cur.x, cur.y, (uint8_t)nx, (uint8_t)ny);
            if (count < MAZE_GEN_ROOM_MAX) {
                rooms[count].x = (uint8_t)nx;
                rooms[count].y = (uint8_t)ny;
                last_dir[count] = order[i];
                count++;
            }
            carved = true;
            break;
        }
        if (!carved) {
            rooms[idx] = rooms[count - 1U];
            last_dir[idx] = last_dir[count - 1U];
            count--;
        }
    }
}

/**
 * @brief Binary Tree: 每格只向西或向北打通, 规律强, 弯绕少.
 *
 * @param map 地图, 调用方保证非空.
 * @param ox 左上房间列.
 * @param oy 左上房间行.
 * @param rooms_x 横向房间数.
 * @param rooms_y 纵向房间数.
 * @param rng 随机种子, 调用方保证非空.
 * @return 无.
 */
static void maze_gen_carve_binary(maze_gen_map_t *map, uint8_t ox, uint8_t oy,
                                  uint8_t rooms_x, uint8_t rooms_y,
                                  uint32_t *rng)
{
    uint8_t ry;
    uint8_t rx;

    map->entrance.x = ox;
    map->entrance.y = oy;
    for (ry = 0U; ry < rooms_y; ++ry) {
        for (rx = 0U; rx < rooms_x; ++rx) {
            uint8_t x = (uint8_t)(ox + (rx * 2U));
            uint8_t y = (uint8_t)(oy + (ry * 2U));
            bool west = (rx > 0U);
            bool north = (ry > 0U);

            map->cells[maze_gen_index(map->width, x, y)] =
                (uint8_t)MAZE_CELL_PATH;
            if ((!west) && (!north)) {
                continue;
            }
            if (west && north) {
                if ((maze_gen_rand(rng) % 2U) == 0U) {
                    north = false;
                } else {
                    west = false;
                }
            }
            if (west) {
                maze_gen_carve_edge(map, x, y, (uint8_t)(x - 2U), y);
            } else {
                maze_gen_carve_edge(map, x, y, x, (uint8_t)(y - 2U));
            }
        }
    }
}

/**
 * @brief Sidewinder: 每行成段, 段内向东, 随机向北开一口.
 *
 * @param map 地图, 调用方保证非空.
 * @param ox 左上房间列.
 * @param oy 左上房间行.
 * @param rooms_x 横向房间数.
 * @param rooms_y 纵向房间数.
 * @param rng 随机种子, 调用方保证非空.
 * @return 无.
 */
static void maze_gen_carve_sidewinder(maze_gen_map_t *map, uint8_t ox,
                                      uint8_t oy, uint8_t rooms_x,
                                      uint8_t rooms_y, uint32_t *rng)
{
    uint8_t ry;

    map->entrance.x = ox;
    map->entrance.y = oy;
    for (ry = 0U; ry < rooms_y; ++ry) {
        uint8_t run_x[MAZE_GEN_ROOM_MAX];
        uint8_t run_len = 0U;
        uint8_t rx;
        uint8_t y = (uint8_t)(oy + (ry * 2U));

        for (rx = 0U; rx < rooms_x; ++rx) {
            uint8_t x = (uint8_t)(ox + (rx * 2U));
            bool close_run;

            map->cells[maze_gen_index(map->width, x, y)] =
                (uint8_t)MAZE_CELL_PATH;
            run_x[run_len] = x;
            run_len++;
            close_run = (rx == (rooms_x - 1U));
            if ((!close_run) && (ry > 0U)) {
                close_run = ((maze_gen_rand(rng) % 2U) == 0U);
            }
            if (close_run) {
                if (ry > 0U) {
                    uint8_t pick = (uint8_t)(maze_gen_rand(rng) % run_len);
                    uint8_t px = run_x[pick];

                    maze_gen_carve_edge(map, px, y, px, (uint8_t)(y - 2U));
                }
                run_len = 0U;
            } else {
                maze_gen_carve_edge(map, x, y, (uint8_t)(x + 2U), y);
            }
        }
    }
}

/**
 * @brief 并查集找根.
 *
 * @param parent 父表.
 * @param i 节点.
 * @return 根下标.
 */
static uint16_t maze_gen_uf_find(uint16_t *parent, uint16_t i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

/**
 * @brief Kruskal 随机最小生成树, 分叉更密.
 *
 * @param map 地图, 调用方保证非空.
 * @param ox 左上房间列.
 * @param oy 左上房间行.
 * @param rooms_x 横向房间数.
 * @param rooms_y 纵向房间数.
 * @param rng 随机种子, 调用方保证非空.
 * @return 无.
 */
static void maze_gen_carve_kruskal(maze_gen_map_t *map, uint8_t ox, uint8_t oy,
                                   uint8_t rooms_x, uint8_t rooms_y,
                                   uint32_t *rng)
{
    uint16_t parent[MAZE_GEN_ROOM_MAX];
    uint8_t ex0[MAZE_GEN_ROOM_MAX * 2U];
    uint8_t ey0[MAZE_GEN_ROOM_MAX * 2U];
    uint8_t ex1[MAZE_GEN_ROOM_MAX * 2U];
    uint8_t ey1[MAZE_GEN_ROOM_MAX * 2U];
    uint16_t edge_n = 0U;
    uint16_t i;
    uint8_t ry;
    uint8_t rx;
    uint16_t room_n = (uint16_t)rooms_x * (uint16_t)rooms_y;

    map->entrance.x = ox;
    map->entrance.y = oy;
    for (i = 0U; i < room_n; ++i) {
        parent[i] = i;
    }
    for (ry = 0U; ry < rooms_y; ++ry) {
        for (rx = 0U; rx < rooms_x; ++rx) {
            uint8_t x = (uint8_t)(ox + (rx * 2U));
            uint8_t y = (uint8_t)(oy + (ry * 2U));

            map->cells[maze_gen_index(map->width, x, y)] =
                (uint8_t)MAZE_CELL_PATH;
            if (rx + 1U < rooms_x) {
                ex0[edge_n] = x;
                ey0[edge_n] = y;
                ex1[edge_n] = (uint8_t)(x + 2U);
                ey1[edge_n] = y;
                edge_n++;
            }
            if (ry + 1U < rooms_y) {
                ex0[edge_n] = x;
                ey0[edge_n] = y;
                ex1[edge_n] = x;
                ey1[edge_n] = (uint8_t)(y + 2U);
                edge_n++;
            }
        }
    }
    for (i = edge_n; i > 1U; --i) {
        uint16_t j = (uint16_t)(maze_gen_rand(rng) % i);
        uint16_t last = (uint16_t)(i - 1U);
        uint8_t tx = ex0[last];
        uint8_t ty = ey0[last];
        uint8_t ux = ex1[last];
        uint8_t uy = ey1[last];

        ex0[last] = ex0[j];
        ey0[last] = ey0[j];
        ex1[last] = ex1[j];
        ey1[last] = ey1[j];
        ex0[j] = tx;
        ey0[j] = ty;
        ex1[j] = ux;
        ey1[j] = uy;
    }
    for (i = 0U; i < edge_n; ++i) {
        uint16_t a = (uint16_t)(((ex0[i] - ox) / 2U) +
                                (((ey0[i] - oy) / 2U) * rooms_x));
        uint16_t b = (uint16_t)(((ex1[i] - ox) / 2U) +
                                (((ey1[i] - oy) / 2U) * rooms_x));
        uint16_t ra = maze_gen_uf_find(parent, a);
        uint16_t rb = maze_gen_uf_find(parent, b);

        if (ra == rb) {
            continue;
        }
        parent[ra] = rb;
        maze_gen_carve_edge(map, ex0[i], ey0[i], ex1[i], ey1[i]);
    }
}

/**
 * @brief 按算法在居中区域内雕通路.
 *
 * @param map 地图, 调用方保证非空.
 * @param spec 难度规格.
 * @param rng 随机种子, 调用方保证非空.
 * @return 无.
 */
static void maze_gen_carve_diff(maze_gen_map_t *map, const maze_gen_diff_t *spec,
                                uint32_t *rng)
{
    uint16_t cell_bytes = (uint16_t)map->width * (uint16_t)map->height;
    uint8_t ox;
    uint8_t oy;

    if (cell_bytes > MAZE_CELL_COUNT) {
        cell_bytes = MAZE_CELL_COUNT;
    }
    memset(map->cells, MAZE_CELL_WALL, (size_t)cell_bytes);
    ox = 1U;
    oy = 1U;
    if (spec->algo == MAZE_GEN_ALGO_BINARY) {
        maze_gen_carve_binary(map, ox, oy, 14U, 8U, rng);
    } else if (spec->algo == MAZE_GEN_ALGO_SIDEWINDER) {
        maze_gen_carve_sidewinder(map, ox, oy, 14U, 8U, rng);
    } else if (spec->algo == MAZE_GEN_ALGO_KRUSKAL) {
        maze_gen_carve_kruskal(map, ox, oy, 14U, 8U, rng);
    } else {
        maze_gen_carve_grow(map, ox, oy, 14U, 8U, spec->newest_pct,
                            spec->straight_pct, rng);
    }
}

/**
 * @brief 在已雕好的通路上挑本关最难的出口.
 *
 * @param map 地图, 调用方保证非空.
 * @param spec 关卡门槛.
 * @param metrics 输出选中出口的指标, 可为 NULL.
 * @return 选出可达出口为 true.
 */
static bool maze_gen_pick_exit(maze_gen_map_t *map, uint8_t min_path_dist,
                               maze_gen_metrics_t *metrics)
{
    uint16_t count = (uint16_t)map->width * (uint16_t)map->height;
    uint16_t i;
    uint32_t best_score = 0U;
    maze_point_t best_exit;
    maze_gen_metrics_t best_metrics;
    bool found = false;

    if (count > MAZE_CELL_COUNT) {
        count = MAZE_CELL_COUNT;
    }
    if (maze_gen_bfs(map) == 0U) {
        return false;
    }
    best_exit = map->entrance;
    memset(&best_metrics, 0, sizeof(best_metrics));

    for (i = 0U; i < count; ++i) {
        maze_point_t cand;
        maze_gen_metrics_t local;
        uint32_t score;

        if (map->cells[i] != (uint8_t)MAZE_CELL_PATH) {
            continue;
        }
        if (s_dist[i] == MAZE_GEN_INF) {
            continue;
        }
        cand.x = (uint8_t)(i % map->width);
        cand.y = (uint8_t)(i / map->width);
        if ((cand.x == map->entrance.x) && (cand.y == map->entrance.y)) {
            continue;
        }
        if (maze_gen_degree(map, cand.x, cand.y) != 1U) {
            continue;
        }
        if (s_dist[i] < min_path_dist) {
            continue;
        }
        if (!maze_gen_measure_exit(map, cand, &local)) {
            continue;
        }
        score = maze_gen_score(&local, maze_gen_degree(map, cand.x, cand.y),
                               min_path_dist);
        if (!found || (score > best_score)) {
            best_score = score;
            best_exit = cand;
            best_metrics = local;
            found = true;
        }
    }
    if (!found) {
        for (i = 0U; i < count; ++i) {
            maze_point_t cand;
            maze_gen_metrics_t local;
            uint32_t score;

            if ((map->cells[i] != (uint8_t)MAZE_CELL_PATH) ||
                (s_dist[i] == MAZE_GEN_INF) || (s_dist[i] < 4U)) {
                continue;
            }
            cand.x = (uint8_t)(i % map->width);
            cand.y = (uint8_t)(i / map->width);
            if ((cand.x == map->entrance.x) && (cand.y == map->entrance.y)) {
                continue;
            }
            if (maze_gen_degree(map, cand.x, cand.y) != 1U) {
                continue;
            }
            if (!maze_gen_measure_exit(map, cand, &local)) {
                continue;
            }
            score = maze_gen_score(&local,
                                   maze_gen_degree(map, cand.x, cand.y), 4U);
            if (!found || (score > best_score)) {
                best_score = score;
                best_exit = cand;
                best_metrics = local;
                found = true;
            }
        }
    }
    if (!found) {
        uint16_t far = 0U;

        for (i = 0U; i < count; ++i) {
            if ((map->cells[i] != (uint8_t)MAZE_CELL_PATH) ||
                (s_dist[i] == MAZE_GEN_INF)) {
                continue;
            }
            if (s_dist[i] >= far) {
                far = s_dist[i];
                best_exit.x = (uint8_t)(i % map->width);
                best_exit.y = (uint8_t)(i / map->width);
                found = true;
            }
        }
        if (!found) {
            return false;
        }
        (void)maze_gen_measure_exit(map, best_exit, &best_metrics);
    }
    map->exit_cell = best_exit;
    map->cells[maze_gen_index(map->width, best_exit.x, best_exit.y)] =
        (uint8_t)MAZE_CELL_PATH;
    if (metrics) {
        *metrics = best_metrics;
    }
    return maze_gen_is_path(map, (int16_t)map->entrance.x,
                            (int16_t)map->entrance.y) &&
           maze_gen_is_path(map, (int16_t)map->exit_cell.x,
                            (int16_t)map->exit_cell.y);
}

/**
 * @brief 按目标步数挑出口. target 为 0 时改选最难出口.
 *
 * @param map 地图, 调用方保证非空.
 * @param min_path_dist 出口至少这么远.
 * @param target_path 希望的正解步数, 0 表示越难越好.
 * @param metrics 输出指标, 可为 NULL.
 * @return 选出可达出口为 true.
 */
static bool maze_gen_pick_exit_target(maze_gen_map_t *map,
                                      uint8_t min_path_dist,
                                      uint8_t target_path,
                                      maze_gen_metrics_t *metrics)
{
    uint16_t count = (uint16_t)map->width * (uint16_t)map->height;
    uint16_t i;
    uint32_t best_cost = 0xFFFFFFFFU;
    maze_point_t best_exit;
    maze_gen_metrics_t best_metrics;
    bool found = false;

    if (target_path == 0U) {
        return maze_gen_pick_exit(map, min_path_dist, metrics);
    }
    if (count > MAZE_CELL_COUNT) {
        count = MAZE_CELL_COUNT;
    }
    if (maze_gen_bfs(map) == 0U) {
        return false;
    }
    best_exit = map->entrance;
    memset(&best_metrics, 0, sizeof(best_metrics));
    for (i = 0U; i < count; ++i) {
        maze_point_t cand;
        maze_gen_metrics_t local;
        uint16_t dist_err;
        uint32_t cost;

        if ((map->cells[i] != (uint8_t)MAZE_CELL_PATH) ||
            (s_dist[i] == MAZE_GEN_INF) || (s_dist[i] < min_path_dist)) {
            continue;
        }
        cand.x = (uint8_t)(i % map->width);
        cand.y = (uint8_t)(i / map->width);
        if ((cand.x == map->entrance.x) && (cand.y == map->entrance.y)) {
            continue;
        }
        if (maze_gen_degree(map, cand.x, cand.y) != 1U) {
            continue;
        }
        if (!maze_gen_measure_exit(map, cand, &local)) {
            continue;
        }
        dist_err = (local.path_len > (uint16_t)target_path)
                       ? (uint16_t)(local.path_len - target_path)
                       : (uint16_t)(target_path - local.path_len);
        cost = ((uint32_t)dist_err * 8U) + (uint32_t)local.junctions;
        if (!found || (cost < best_cost)) {
            best_cost = cost;
            best_exit = cand;
            best_metrics = local;
            found = true;
        }
    }
    if (!found) {
        return maze_gen_pick_exit(map, 8U, metrics);
    }
    map->exit_cell = best_exit;
    map->cells[maze_gen_index(map->width, best_exit.x, best_exit.y)] =
        (uint8_t)MAZE_CELL_PATH;
    if (metrics) {
        *metrics = best_metrics;
    }
    return true;
}

uint8_t maze_gen_difficulty_of_level(uint16_t level)
{
    uint16_t capped;

    if (level < 1U) {
        level = 1U;
    }
    if (level > MAZE_LEVEL_MAX) {
        level = MAZE_LEVEL_MAX;
    }
    capped = (uint16_t)((level - 1U) / MAZE_LEVELS_PER_DIFF);
    if (capped >= MAZE_GEN_DIFFICULTY_COUNT) {
        capped = (uint16_t)(MAZE_GEN_DIFFICULTY_COUNT - 1U);
    }
    return (uint8_t)(capped + 1U);
}

bool maze_gen_measure(const maze_gen_map_t *map, maze_gen_metrics_t *out)
{
    if (!map || !out || !map->cells || (map->width == 0U) ||
        (map->height == 0U)) {
        return false;
    }
    if (((uint16_t)map->width * (uint16_t)map->height) > MAZE_CELL_COUNT) {
        return false;
    }
    if (maze_gen_bfs(map) == 0U) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return maze_gen_measure_exit(map, map->exit_cell, out);
}

bool maze_gen_create(maze_gen_map_t *map, uint16_t level, uint32_t *rng)
{
    return maze_gen_create_difficulty(map, maze_gen_difficulty_of_level(level),
                                      rng);
}

const maze_gen_diff_t *maze_gen_diff_spec(uint8_t difficulty)
{
    if ((difficulty < 1U) || (difficulty > MAZE_GEN_DIFFICULTY_COUNT)) {
        difficulty = 1U;
    }
    return &s_diffs[difficulty - 1U];
}

bool maze_gen_create_difficulty(maze_gen_map_t *map, uint8_t difficulty,
                                uint32_t *rng)
{
    const maze_gen_diff_t *spec;
    maze_gen_metrics_t metrics;
    maze_gen_metrics_t best_metrics;
    maze_point_t best_entrance;
    maze_point_t best_exit;
    uint16_t cell_bytes;
    uint8_t try_i;
    bool have_best = false;

    if (!map || !map->cells || !rng || (map->width < 5U) ||
        (map->height < 5U)) {
        return false;
    }
    cell_bytes = (uint16_t)map->width * (uint16_t)map->height;
    if (cell_bytes > MAZE_CELL_COUNT) {
        return false;
    }
    spec = maze_gen_diff_spec(difficulty);
    memset(&best_metrics, 0, sizeof(best_metrics));
    best_entrance.x = 1U;
    best_entrance.y = 1U;
    best_exit.x = 1U;
    best_exit.y = 1U;
    for (try_i = 0U; try_i < 8U; ++try_i) {
        uint16_t path_err;
        uint16_t best_err;

        maze_gen_carve_diff(map, spec, rng);
        if (!maze_gen_pick_exit_target(map, spec->min_path_dist,
                                       spec->target_path, &metrics)) {
            continue;
        }
        if (spec->target_path == 0U) {
            if (!have_best || (metrics.junctions > best_metrics.junctions)) {
                memcpy(s_best_cells, map->cells, (size_t)cell_bytes);
                best_entrance = map->entrance;
                best_exit = map->exit_cell;
                best_metrics = metrics;
                have_best = true;
            }
            if (metrics.junctions >= spec->min_junctions) {
                return true;
            }
            continue;
        }
        path_err = (metrics.path_len > spec->target_path)
                       ? (uint16_t)(metrics.path_len - spec->target_path)
                       : (uint16_t)(spec->target_path - metrics.path_len);
        best_err = (best_metrics.path_len > spec->target_path)
                       ? (uint16_t)(best_metrics.path_len - spec->target_path)
                       : (uint16_t)(spec->target_path - best_metrics.path_len);
        if (!have_best || (path_err < best_err)) {
            memcpy(s_best_cells, map->cells, (size_t)cell_bytes);
            best_entrance = map->entrance;
            best_exit = map->exit_cell;
            best_metrics = metrics;
            have_best = true;
        }
        if (path_err <= 6U) {
            return true;
        }
    }
    if (!have_best) {
        return false;
    }
    memcpy(map->cells, s_best_cells, (size_t)cell_bytes);
    map->entrance = best_entrance;
    map->exit_cell = best_exit;
    return true;
}
