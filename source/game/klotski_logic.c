#include "klotski_logic.h"

#include <string.h>

/* 关卡表由 tools/klotski_solver 生成, 请勿手改 */
#include "klotski_levels.h"

/* 占用表中的空格哨兵，和合法棋子编号区分。 */
#define KLOTSKI_EMPTY 0xFFU

static const char *const s_piece_names[KLOTSKI_PIECE_COUNT] = {
    "曹操", "关羽", "张飞", "赵云", "马超", "黄忠", "兵", "兵", "兵", "兵",
};

/**
 * @brief 按棋子下标取尺寸.
 *
 * @param piece 棋子下标.
 * @param w 输出宽, 不可为空.
 * @param h 输出高, 不可为空.
 * @return 无. 下标越界时按 1x1 处理.
 */
static void klotski_piece_dim(uint8_t piece, uint8_t *w, uint8_t *h)
{
    if (piece == KLOTSKI_PIECE_CAO) {
        *w = 2U;
        *h = 2U;
    } else if (piece == KLOTSKI_PIECE_GUAN) {
        *w = 2U;
        *h = 1U;
    } else if (piece < KLOTSKI_PIECE_SOLDIER_FIRST) {
        *w = 1U;
        *h = 2U;
    } else {
        *w = 1U;
        *h = 1U;
    }
}

void klotski_game_init(klotski_game_t *game)
{
    if (!game) {
        return;
    }
    memset(game, 0, sizeof(*game));
}

bool klotski_game_load_level(klotski_game_t *game, uint8_t level)
{
    const klotski_level_def_t *def;
    uint8_t i;

    if (!game) {
        return false;
    }
    def = klotski_level_def(level);
    if (!def) {
        return false;
    }
    memset(game, 0, sizeof(*game));
    game->state.level = level;

    game->state.pieces[KLOTSKI_PIECE_CAO].x =
        (uint8_t)(def->layout.cao % KLOTSKI_COLS);
    game->state.pieces[KLOTSKI_PIECE_CAO].y =
        (uint8_t)(def->layout.cao / KLOTSKI_COLS);
    game->state.pieces[KLOTSKI_PIECE_GUAN].x =
        (uint8_t)(def->layout.guan % KLOTSKI_COLS);
    game->state.pieces[KLOTSKI_PIECE_GUAN].y =
        (uint8_t)(def->layout.guan / KLOTSKI_COLS);
    for (i = 0U; i < 4U; ++i) {
        uint8_t anchor = def->layout.generals[i];

        game->state.pieces[KLOTSKI_PIECE_GENERAL_FIRST + i].x =
            (uint8_t)(anchor % KLOTSKI_COLS);
        game->state.pieces[KLOTSKI_PIECE_GENERAL_FIRST + i].y =
            (uint8_t)(anchor / KLOTSKI_COLS);
        anchor = def->layout.soldiers[i];
        game->state.pieces[KLOTSKI_PIECE_SOLDIER_FIRST + i].x =
            (uint8_t)(anchor % KLOTSKI_COLS);
        game->state.pieces[KLOTSKI_PIECE_SOLDIER_FIRST + i].y =
            (uint8_t)(anchor / KLOTSKI_COLS);
    }
    for (i = 0U; i < KLOTSKI_PIECE_COUNT; ++i) {
        klotski_piece_dim(i, &game->state.pieces[i].w,
                          &game->state.pieces[i].h);
    }
    return true;
}

/**
 * @brief 建占用表, 每格为棋子下标或 KLOTSKI_EMPTY.
 *
 * @param game 实例, 不可为空.
 * @param cells 输出数组, 至少 KLOTSKI_CELL_COUNT 格, 不可为空.
 * @return 无.
 */
static void klotski_build_occupancy(const klotski_game_t *game, uint8_t *cells)
{
    /* 每次尝试移动前重建小棋盘占用表，逻辑清晰且开销固定。 */
    uint8_t p;
    uint8_t dx;
    uint8_t dy;

    memset(cells, KLOTSKI_EMPTY, KLOTSKI_CELL_COUNT);
    for (p = 0U; p < KLOTSKI_PIECE_COUNT; ++p) {
        const klotski_piece_t *piece = &game->state.pieces[p];

        for (dy = 0U; dy < piece->h; ++dy) {
            for (dx = 0U; dx < piece->w; ++dx) {
                cells[(piece->y + dy) * KLOTSKI_COLS + (piece->x + dx)] = p;
            }
        }
    }
}

klotski_event_t klotski_game_move(klotski_game_t *game, uint8_t piece,
                                  klotski_dir_t dir)
{
    klotski_piece_t *target;
    uint8_t cells[KLOTSKI_CELL_COUNT];
    int8_t dx = 0;
    int8_t dy = 0;
    int16_t nx;
    int16_t ny;
    uint8_t i;
    uint8_t j;

    if (!game || piece >= KLOTSKI_PIECE_COUNT || game->state.won) {
        return KLOTSKI_EVENT_NONE;
    }
    switch (dir) {
        case KLOTSKI_DIR_UP:
            dy = -1;
            break;
        case KLOTSKI_DIR_DOWN:
            dy = 1;
            break;
        case KLOTSKI_DIR_LEFT:
            dx = -1;
            break;
        case KLOTSKI_DIR_RIGHT:
            dx = 1;
            break;
        default:
            return KLOTSKI_EVENT_NONE;
    }
    target = &game->state.pieces[piece];
    nx = (int16_t)target->x + dx;
    ny = (int16_t)target->y + dy;
    /* 先检查目标棋子整体仍在棋盘内，再检查目标矩形是否撞到别人。 */
    if (nx < 0 || ny < 0 ||
        (uint16_t)nx + target->w > KLOTSKI_COLS ||
        (uint16_t)ny + target->h > KLOTSKI_ROWS) {
        return KLOTSKI_EVENT_BLOCKED;
    }
    klotski_build_occupancy(game, cells);
    for (j = 0U; j < target->h; ++j) {
        for (i = 0U; i < target->w; ++i) {
            uint8_t occupant =
                cells[((uint8_t)ny + j) * KLOTSKI_COLS + ((uint8_t)nx + i)];

            if (occupant != KLOTSKI_EMPTY && occupant != piece) {
                return KLOTSKI_EVENT_BLOCKED;
            }
        }
    }
    /* 移动按单格计步；只有曹操到达出口 anchor 才产生 WIN。 */
    target->x = (uint8_t)nx;
    target->y = (uint8_t)ny;
    game->state.steps++;
    if (piece == KLOTSKI_PIECE_CAO && target->x == KLOTSKI_WIN_X &&
        target->y == KLOTSKI_WIN_Y) {
        game->state.won = true;
        return KLOTSKI_EVENT_WIN;
    }
    return KLOTSKI_EVENT_MOVED;
}

int klotski_game_piece_at(const klotski_game_t *game, uint8_t x, uint8_t y)
{
    uint8_t p;

    if (!game || x >= KLOTSKI_COLS || y >= KLOTSKI_ROWS) {
        return -1;
    }
    for (p = 0U; p < KLOTSKI_PIECE_COUNT; ++p) {
        const klotski_piece_t *piece = &game->state.pieces[p];

        if (x >= piece->x && x < (uint8_t)(piece->x + piece->w) &&
            y >= piece->y && y < (uint8_t)(piece->y + piece->h)) {
            return (int)p;
        }
    }
    return -1;
}

const klotski_state_t *klotski_game_state(const klotski_game_t *game)
{
    return game ? &game->state : NULL;
}

const klotski_level_def_t *klotski_level_def(uint8_t level)
{
    if (level >= KLOTSKI_LEVEL_COUNT) {
        return NULL;
    }
    return &s_klotski_levels[level];
}

const char *klotski_piece_name(uint8_t piece)
{
    if (piece >= KLOTSKI_PIECE_COUNT) {
        return "";
    }
    return s_piece_names[piece];
}

bool klotski_is_exit_cell(uint8_t x, uint8_t y)
{
    return y == (KLOTSKI_ROWS - 1U) && (x == 1U || x == 2U);
}
