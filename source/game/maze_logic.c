#include "maze_logic.h"
#include "maze_gen.h"

#include <string.h>

/**
 * @brief 线性同余随机数, 与贪吃蛇逻辑层同一套.
 */
static uint32_t maze_random_next(maze_game_t *game)
{
    game->random_state = game->random_state * 1664525U + 1013904223U;
    return game->random_state;
}

static bool maze_valid_point(uint8_t x, uint8_t y)
{
    /* 公开查询接口统一用固定编译期边界，避免状态被外部写坏后越界。 */
    return (x < MAZE_WIDTH) && (y < MAZE_HEIGHT);
}

static uint16_t maze_index(uint8_t x, uint8_t y)
{
    return (uint16_t)((uint16_t)y * MAZE_WIDTH + x);
}

static bool maze_is_path(const maze_game_t *game, int16_t x, int16_t y)
{
    if ((x < 0) || (y < 0) || (x >= (int16_t)MAZE_WIDTH) ||
        (y >= (int16_t)MAZE_HEIGHT)) {
        return false;
    }
    return game->state.cells[maze_index((uint8_t)x, (uint8_t)y)] ==
           MAZE_CELL_PATH;
}

static void maze_dir_delta(maze_dir_t dir, int8_t *dx, int8_t *dy)
{
    *dx = 0;
    *dy = 0;
    switch (dir) {
        case MAZE_DIR_UP:
            *dy = -1;
            break;
        case MAZE_DIR_DOWN:
            *dy = 1;
            break;
        case MAZE_DIR_LEFT:
            *dx = -1;
            break;
        case MAZE_DIR_RIGHT:
        default:
            *dx = 1;
            break;
    }
}

/**
 * @brief 取相反朝向, 用于守卫巡逻时禁止立刻掉头.
 *
 * @param dir 当前朝向.
 * @return 相反方向.
 *
 * @note 任务上下文: 逻辑层, 不可在 ISR 调用. 纯函数可重入.
 */
static maze_dir_t maze_dir_opposite(maze_dir_t dir)
{
    switch (dir) {
        case MAZE_DIR_UP:
            return MAZE_DIR_DOWN;
        case MAZE_DIR_DOWN:
            return MAZE_DIR_UP;
        case MAZE_DIR_LEFT:
            return MAZE_DIR_RIGHT;
        case MAZE_DIR_RIGHT:
        default:
            return MAZE_DIR_LEFT;
    }
}

/**
 * @brief 曼哈顿距离, 用于入口安全区判定.
 *
 * @param a 格子 A.
 * @param b 格子 B.
 * @return |ax-bx| + |ay-by|.
 *
 * @note 任务上下文: 逻辑层, 纯函数可重入.
 */
static uint16_t maze_manhattan(maze_point_t a, maze_point_t b)
{
    uint16_t dx = (a.x > b.x) ? (uint16_t)(a.x - b.x) : (uint16_t)(b.x - a.x);
    uint16_t dy = (a.y > b.y) ? (uint16_t)(a.y - b.y) : (uint16_t)(b.y - a.y);

    return (uint16_t)(dx + dy);
}

/**
 * @brief 判断格子是否落在入口安全区内.
 *
 * @param game 迷宫实例, 调用方保证非空.
 * @param p 待判断格子.
 * @return 距入口曼哈顿距离 <= MAZE_ENTRANCE_SAFE_DIST 为 true.
 *
 * @note 任务上下文: 逻辑层, 不可在 ISR 调用. 只读.
 */
static bool maze_in_entrance_safe(const maze_game_t *game, maze_point_t p)
{
    return maze_manhattan(p, game->state.entrance) <=
           (uint16_t)MAZE_ENTRANCE_SAFE_DIST;
}

static maze_dir_t maze_input_dir(maze_input_t input)
{
    switch (input) {
        case MAZE_INPUT_UP:
            return MAZE_DIR_UP;
        case MAZE_INPUT_DOWN:
            return MAZE_DIR_DOWN;
        case MAZE_INPUT_LEFT:
            return MAZE_DIR_LEFT;
        case MAZE_INPUT_RIGHT:
        default:
            return MAZE_DIR_RIGHT;
    }
}

/**
 * @brief 按当前关卡调用生成模块雕满屏迷宫, 不改计时和守卫规则.
 *
 * @param game 迷宫实例, 调用方保证非空.
 * @return 无.
 */
static void maze_generate(maze_game_t *game)
{
    maze_gen_map_t map;

    game->state.width = (uint8_t)MAZE_WIDTH;
    game->state.height = (uint8_t)MAZE_HEIGHT;
    map.width = game->state.width;
    map.height = game->state.height;
    map.cells = game->state.cells;
    map.entrance.x = 1U;
    map.entrance.y = 1U;
    map.exit_cell.x = (uint8_t)(MAZE_WIDTH - 2U);
    map.exit_cell.y = (uint8_t)(MAZE_HEIGHT - 2U);
    if (!maze_gen_create(&map, game->state.level, &game->random_state)) {
        game->state.entrance.x = 1U;
        game->state.entrance.y = 1U;
        game->state.exit_cell.x = (uint8_t)(MAZE_WIDTH - 2U);
        game->state.exit_cell.y = (uint8_t)(MAZE_HEIGHT - 2U);
        game->state.cells[maze_index(1U, 1U)] = (uint8_t)MAZE_CELL_PATH;
        game->state.cells[maze_index(game->state.exit_cell.x,
                                     game->state.exit_cell.y)] =
            (uint8_t)MAZE_CELL_PATH;
        return;
    }
    game->state.entrance = map.entrance;
    game->state.exit_cell = map.exit_cell;
}

/**
 * @brief 守卫出生点固定为出口. 入口留给玩家, 两人不会刷到同一格.
 *
 * @param game 迷宫实例, 调用方保证非空.
 * @return 无.
 */
static void maze_place_guard(maze_game_t *game)
{
    game->state.guard_spawn = game->state.exit_cell;
    game->state.guard_dir = MAZE_DIR_LEFT;
}

/**
 * @brief 守卫朝前看最多 2 格, 撞墙或入口安全区即停.
 *
 * @param game 迷宫实例, 调用方保证非空.
 * @return 看见玩家为 true.
 *
 * @note 任务上下文: 逻辑层, 不可在 ISR 调用. 只读.
 */
static bool maze_guard_sees_player(const maze_game_t *game)
{
    int8_t dx;
    int8_t dy;
    uint8_t i;
    int16_t x;
    int16_t y;
    maze_point_t cell;

    if (!game->state.guard_active) {
        return false;
    }
    if (maze_in_entrance_safe(game, game->state.player)) {
        return false;
    }
    maze_dir_delta(game->state.guard_dir, &dx, &dy);
    x = (int16_t)game->state.guard.x;
    y = (int16_t)game->state.guard.y;
    for (i = 0U; i < MAZE_GUARD_SIGHT; ++i) {
        x = (int16_t)(x + dx);
        y = (int16_t)(y + dy);
        if (!maze_is_path(game, x, y)) {
            return false;
        }
        cell.x = (uint8_t)x;
        cell.y = (uint8_t)y;
        if (maze_in_entrance_safe(game, cell)) {
            return false;
        }
        if ((x == (int16_t)game->state.player.x) &&
            (y == (int16_t)game->state.player.y)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 玩家回入口, 守卫回出口出生点, 计时按模式重置.
 *
 * @param game 迷宫实例, 调用方保证非空.
 * @return 无.
 */
static void maze_reset_actors(maze_game_t *game)
{
    /* 重置只回收角色和计时，不重新雕地图；重试因此保持本关布局。 */
    game->state.player = game->state.entrance;
    game->state.guard_active = (game->state.mode == MAZE_MODE_CHALLENGE);
    if (game->state.guard_active) {
        game->state.guard = game->state.guard_spawn;
        game->state.guard_dir = MAZE_DIR_LEFT;
    } else {
        game->state.guard.x = 0U;
        game->state.guard.y = 0U;
        game->state.guard_dir = MAZE_DIR_LEFT;
    }
    game->state.remain_ms = (game->state.mode == MAZE_MODE_TIMED)
                                ? MAZE_TIMED_LIMIT_MS
                                : 0U;
    game->state.paused = false;
    game->guard_elapsed_ms = 0U;
}

/**
 * @brief 被守卫看见或重叠则双方重置: 玩家回入口, 守卫回出口.
 *
 * @param game 迷宫实例, 调用方保证非空.
 * @return MAZE_EVENT_CAUGHT 或 MAZE_EVENT_NONE.
 *
 * @note 玩家已经站在出口格时不抓, 把过关判定留给 set_input.
 *       入口安全区内也不抓, 给玩家出生和回撤空间.
 */
static maze_event_t maze_apply_catch(maze_game_t *game)
{
    if (!game->state.guard_active) {
        return MAZE_EVENT_NONE;
    }
    if (maze_game_same_point(game->state.player, game->state.exit_cell)) {
        return MAZE_EVENT_NONE;
    }
    if (maze_in_entrance_safe(game, game->state.player)) {
        return MAZE_EVENT_NONE;
    }
    if (!maze_guard_sees_player(game) &&
        !maze_game_same_point(game->state.player, game->state.guard)) {
        return MAZE_EVENT_NONE;
    }
    game->state.caught_count++;
    maze_reset_actors(game);
    return MAZE_EVENT_CAUGHT;
}

static void maze_begin_level(maze_game_t *game, bool new_maze)
{
    if (new_maze) {
        maze_generate(game);
        if (game->state.mode == MAZE_MODE_CHALLENGE) {
            maze_place_guard(game);
        } else {
            game->state.guard_spawn.x = 0U;
            game->state.guard_spawn.y = 0U;
        }
    }
    maze_reset_actors(game);
}

static maze_event_t maze_next_level(maze_game_t *game)
{
    if (game->state.level < MAZE_LEVEL_MAX) {
        game->state.level++;
    }
    maze_begin_level(game, true);
    return MAZE_EVENT_LEVEL_CLEAR;
}

/**
 * @brief 守卫沿通路巡逻一步, 并更新朝向.
 *
 * @param game 迷宫实例, 调用方保证非空.
 * @return MAZE_EVENT_CAUGHT 或 MAZE_EVENT_NONE.
 *
 * @note 任务上下文: 逻辑层, 不可在 ISR 调用. 不可重入.
 *       优先级: 不掉头且不踩出口 > 不掉头 > 死路才掉头.
 *       入口安全区格子直接剔除, 守卫不会走进玩家出生点.
 */
static maze_event_t maze_guard_step(maze_game_t *game)
{
    maze_point_t options[4];
    maze_dir_t dirs[4];
    uint8_t count = 0U;
    maze_point_t keep[4];
    maze_dir_t keep_dirs[4];
    uint8_t keep_count = 0U;
    maze_point_t prefer[4];
    maze_dir_t prefer_dirs[4];
    uint8_t prefer_count = 0U;
    const int8_t dx[4] = {0, 0, -1, 1};
    const int8_t dy[4] = {-1, 1, 0, 0};
    const maze_dir_t dir_map[4] = {
        MAZE_DIR_UP, MAZE_DIR_DOWN, MAZE_DIR_LEFT, MAZE_DIR_RIGHT
    };
    maze_dir_t back;
    const maze_point_t *pick_pts;
    const maze_dir_t *pick_dirs;
    uint8_t pick_count;
    uint8_t i;
    uint8_t pick;

    if (!game->state.guard_active) {
        return MAZE_EVENT_NONE;
    }

    back = maze_dir_opposite(game->state.guard_dir);
    for (i = 0U; i < 4U; ++i) {
        int16_t nx = (int16_t)game->state.guard.x + dx[i];
        int16_t ny = (int16_t)game->state.guard.y + dy[i];
        maze_point_t next;

        if (!maze_is_path(game, nx, ny)) {
            continue;
        }
        next.x = (uint8_t)nx;
        next.y = (uint8_t)ny;
        if (maze_in_entrance_safe(game, next)) {
            continue;
        }
        options[count] = next;
        dirs[count] = dir_map[i];
        ++count;
        if (dir_map[i] == back) {
            continue;
        }
        keep[keep_count] = next;
        keep_dirs[keep_count] = dir_map[i];
        ++keep_count;
        if (maze_game_same_point(next, game->state.exit_cell)) {
            continue;
        }
        prefer[prefer_count] = next;
        prefer_dirs[prefer_count] = dir_map[i];
        ++prefer_count;
    }
    /* 没有可走邻居时仍执行一次抓捕判定，处理守卫与玩家重叠的死路。 */
    if (count == 0U) {
        return maze_apply_catch(game);
    }
    if (prefer_count > 0U) {
        pick_pts = prefer;
        pick_dirs = prefer_dirs;
        pick_count = prefer_count;
    } else if (keep_count > 0U) {
        pick_pts = keep;
        pick_dirs = keep_dirs;
        pick_count = keep_count;
    } else {
        pick_pts = options;
        pick_dirs = dirs;
        pick_count = count;
    }
    /* 优先不掉头且不踩出口，其次允许踩出口，最后才允许原地掉头。 */
    pick = (uint8_t)(maze_random_next(game) % pick_count);
    game->state.guard = pick_pts[pick];
    game->state.guard_dir = pick_dirs[pick];
    return maze_apply_catch(game);
}

bool maze_game_same_point(maze_point_t a, maze_point_t b)
{
    return (a.x == b.x) && (a.y == b.y);
}

void maze_game_init(maze_game_t *game)
{
    if (!game) {
        return;
    }
    memset(game, 0, sizeof(*game));
    game->state.width = (uint8_t)MAZE_WIDTH;
    game->state.height = (uint8_t)MAZE_HEIGHT;
    game->state.mode = MAZE_MODE_SIMPLE;
    game->random_state = 1U;
}

void maze_game_seed(maze_game_t *game, uint32_t seed)
{
    if (!game) {
        return;
    }
    game->random_state = (seed == 0U) ? 1U : seed;
}

void maze_game_set_mode(maze_game_t *game, maze_mode_t mode)
{
    if (!game) {
        return;
    }
    if ((mode != MAZE_MODE_SIMPLE) && (mode != MAZE_MODE_TIMED) &&
        (mode != MAZE_MODE_CHALLENGE)) {
        mode = MAZE_MODE_SIMPLE;
    }
    game->state.mode = mode;
}

maze_mode_t maze_game_get_mode(const maze_game_t *game)
{
    if (!game) {
        return MAZE_MODE_SIMPLE;
    }
    return game->state.mode;
}

void maze_game_start(maze_game_t *game)
{
    if (!game) {
        return;
    }
    game->state.level = 1U;
    game->state.caught_count = 0U;
    game->state.timeout_count = 0U;
    maze_begin_level(game, true);
}

void maze_game_retry_level(maze_game_t *game)
{
    if (!game) {
        return;
    }
    maze_begin_level(game, false);
}

maze_event_t maze_game_set_input(maze_game_t *game, maze_input_t input)
{
    int8_t dx;
    int8_t dy;
    int16_t nx;
    int16_t ny;
    maze_event_t event;

    if (!game || (input == MAZE_INPUT_NONE)) {
        return MAZE_EVENT_NONE;
    }
    if (input == MAZE_INPUT_PAUSE) {
        if (game->state.level == 0U) {
            return MAZE_EVENT_NONE;
        }
        game->state.paused = !game->state.paused;
        return MAZE_EVENT_NONE;
    }
    if ((game->state.level == 0U) || game->state.paused) {
        return MAZE_EVENT_NONE;
    }

    maze_dir_delta(maze_input_dir(input), &dx, &dy);
    nx = (int16_t)game->state.player.x + dx;
    ny = (int16_t)game->state.player.y + dy;
    /* 玩家移动是单格原子操作；撞墙返回 BLOCKED，角色不会被部分更新。 */
    if (!maze_is_path(game, nx, ny)) {
        return MAZE_EVENT_BLOCKED;
    }
    game->state.player.x = (uint8_t)nx;
    game->state.player.y = (uint8_t)ny;
    if (maze_game_same_point(game->state.player, game->state.exit_cell)) {
        return maze_next_level(game);
    }
    event = maze_apply_catch(game);
    return (event == MAZE_EVENT_NONE) ? MAZE_EVENT_MOVED : event;
}

maze_event_t maze_game_advance(maze_game_t *game, uint32_t elapsed_ms)
{
    maze_event_t event = MAZE_EVENT_NONE;
    maze_event_t step_event;

    if (!game || (elapsed_ms == 0U) || (game->state.level == 0U) ||
        game->state.paused) {
        return MAZE_EVENT_NONE;
    }

    /* 先扣除倒计时，再推进守卫；超时优先级高于本次守卫移动。 */
    if (game->state.mode == MAZE_MODE_TIMED) {
        if (elapsed_ms >= game->state.remain_ms) {
            game->state.timeout_count++;
            maze_game_retry_level(game);
            return MAZE_EVENT_TIMEOUT;
        }
        game->state.remain_ms -= elapsed_ms;
    }

    if (!game->state.guard_active) {
        return MAZE_EVENT_NONE;
    }
    game->guard_elapsed_ms += elapsed_ms;
    while (game->guard_elapsed_ms >= MAZE_GUARD_STEP_MS) {
        game->guard_elapsed_ms -= MAZE_GUARD_STEP_MS;
        step_event = maze_guard_step(game);
        if (step_event != MAZE_EVENT_NONE) {
            event = step_event;
        }
    }
    return event;
}

const maze_state_t *maze_game_state(const maze_game_t *game)
{
    return game ? &game->state : NULL;
}

maze_status_t maze_game_get_status(const maze_game_t *game)
{
    if (!game || (game->state.level == 0U)) {
        return MAZE_STATUS_READY;
    }
    if (game->state.paused) {
        return MAZE_STATUS_PAUSED;
    }
    return MAZE_STATUS_RUNNING;
}

maze_cell_kind_t maze_game_cell(const maze_game_t *game, uint8_t x, uint8_t y)
{
    if (!game || !maze_valid_point(x, y)) {
        return MAZE_CELL_WALL;
    }
    return (maze_cell_kind_t)game->state.cells[maze_index(x, y)];
}

bool maze_game_is_sight_cell(const maze_game_t *game, uint8_t x, uint8_t y)
{
    int8_t dx;
    int8_t dy;
    uint8_t i;
    int16_t sx;
    int16_t sy;

    if (!game || !game->state.guard_active || !maze_valid_point(x, y)) {
        return false;
    }
    maze_dir_delta(game->state.guard_dir, &dx, &dy);
    sx = (int16_t)game->state.guard.x;
    sy = (int16_t)game->state.guard.y;
    for (i = 0U; i < MAZE_GUARD_SIGHT; ++i) {
        sx = (int16_t)(sx + dx);
        sy = (int16_t)(sy + dy);
        if (!maze_is_path(game, sx, sy)) {
            return false;
        }
        {
            maze_point_t cell;

            cell.x = (uint8_t)sx;
            cell.y = (uint8_t)sy;
            if (maze_in_entrance_safe(game, cell)) {
                return false;
            }
        }
        if ((sx == (int16_t)x) && (sy == (int16_t)y)) {
            return true;
        }
    }
    return false;
}
