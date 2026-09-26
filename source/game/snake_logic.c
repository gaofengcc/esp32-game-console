#include "snake_logic.h"

#include <string.h>

static uint16_t snake_base_speed(const snake_game_t *game)
{
    return game->config.initial_speed_ms ? game->config.initial_speed_ms
                                         : SNAKE_SPEED_SLOW_MS;
}

static void snake_refresh_speed(snake_game_t *game)
{
    uint32_t reduction = (game->state.foods_eaten / 5U) * 10U;
    uint32_t speed = snake_base_speed(game);
    if (speed > reduction) {
        speed -= reduction;
    } else {
        speed = SNAKE_MIN_SPEED_MS;
    }
    if (speed < SNAKE_MIN_SPEED_MS) {
        speed = SNAKE_MIN_SPEED_MS;
    }
    game->state.speed_ms = (uint16_t)speed;
}

static bool snake_same(snake_point_t a, snake_point_t b)
{
    return a.x == b.x && a.y == b.y;
}

static bool snake_opposite(snake_direction_t a, snake_direction_t b)
{
    return (a == SNAKE_DIRECTION_UP && b == SNAKE_DIRECTION_DOWN) ||
           (a == SNAKE_DIRECTION_DOWN && b == SNAKE_DIRECTION_UP) ||
           (a == SNAKE_DIRECTION_LEFT && b == SNAKE_DIRECTION_RIGHT) ||
           (a == SNAKE_DIRECTION_RIGHT && b == SNAKE_DIRECTION_LEFT);
}

static snake_direction_t snake_input_direction(snake_input_t input)
{
    switch (input) {
        case SNAKE_INPUT_UP:
            return SNAKE_DIRECTION_UP;
        case SNAKE_INPUT_DOWN:
            return SNAKE_DIRECTION_DOWN;
        case SNAKE_INPUT_LEFT:
            return SNAKE_DIRECTION_LEFT;
        case SNAKE_INPUT_RIGHT:
            return SNAKE_DIRECTION_RIGHT;
        default:
            return SNAKE_DIRECTION_RIGHT;
    }
}

static uint32_t snake_random_next(snake_game_t *game)
{
    game->random_state = game->random_state * 1664525U + 1013904223U;
    return game->random_state;
}

static bool snake_occupied(const snake_game_t *game, snake_point_t point)
{
    for (uint16_t i = 0; i < game->state.length; ++i) {
        if (snake_same(game->state.segments[i], point)) {
            return true;
        }
    }
    return false;
}

static void snake_spawn_food(snake_game_t *game)
{
    const uint32_t cells = (uint32_t)game->state.width * game->state.height;
    if (game->state.length >= cells || cells == 0U) {
        game->food_valid = false;
        return;
    }
    uint32_t start = snake_random_next(game) % cells;
    for (uint32_t offset = 0; offset < cells; ++offset) {
        uint32_t index = (start + offset) % cells;
        snake_point_t point = {
            .x = (uint8_t)(index % game->state.width),
            .y = (uint8_t)(index / game->state.width),
        };
        if (!snake_occupied(game, point)) {
            game->state.food = point;
            game->food_valid = true;
            return;
        }
    }
    game->food_valid = false;
}

static void snake_save_best_if_needed(snake_game_t *game)
{
    if (game->state.score <= game->state.best_score) {
        return;
    }
    game->state.best_score = game->state.score;
    if (game->config.save_best) {
        (void)game->config.save_best(game->config.storage_ctx,
                                      game->state.best_score);
    }
}

static void snake_set_game_over(snake_game_t *game,
                                snake_game_over_reason_t reason)
{
    game->state.game_over = true;
    game->state.game_over_reason = reason;
    snake_save_best_if_needed(game);
}

void snake_config_default(snake_config_t *config)
{
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->width = SNAKE_BOARD_WIDTH;
    config->height = SNAKE_BOARD_HEIGHT;
    config->initial_speed_ms = SNAKE_SPEED_SLOW_MS;
    config->wrap_walls = true;
}

void snake_game_reload_high_score(snake_game_t *game)
{
    if (!game) {
        return;
    }
    game->state.best_score = 0;
    if (game->config.load_best) {
        int best = 0;
        if (game->config.load_best(game->config.storage_ctx, &best) == 0 &&
            best > 0) {
            game->state.best_score = best;
        }
    }
}

void snake_game_init(snake_game_t *game, const snake_config_t *config)
{
    if (!game) {
        return;
    }
    memset(game, 0, sizeof(*game));
    snake_config_default(&game->config);
    if (config) {
        game->config = *config;
        if (game->config.width < 3U || game->config.width > SNAKE_BOARD_WIDTH) {
            game->config.width = SNAKE_BOARD_WIDTH;
        }
        if (game->config.height < 1U ||
            game->config.height > SNAKE_BOARD_HEIGHT) {
            game->config.height = SNAKE_BOARD_HEIGHT;
        }
        if (game->config.initial_speed_ms == 0U) {
            game->config.initial_speed_ms = SNAKE_SPEED_SLOW_MS;
        }
    }
    game->state.width = game->config.width;
    game->state.height = game->config.height;
    game->state.wrap_walls = game->config.wrap_walls;
    game->random_state = 0x12345678U;
    snake_refresh_speed(game);
    snake_game_reload_high_score(game);
    game->state.game_over = false;
    game->state.paused = false;
    game->state.game_over_reason = SNAKE_GAME_OVER_NONE;
}

void snake_game_reset(snake_game_t *game)
{
    if (!game) {
        return;
    }
    memset(game->state.segments, 0, sizeof(game->state.segments));
    game->state.length = 3;
    game->state.segments[0] =
        (snake_point_t){(uint8_t)(game->state.width / 2U),
                        (uint8_t)(game->state.height / 2U)};
    game->state.segments[1] =
        (snake_point_t){(uint8_t)(game->state.segments[0].x - 1U),
                        game->state.segments[0].y};
    game->state.segments[2] =
        (snake_point_t){(uint8_t)(game->state.segments[0].x - 2U),
                        game->state.segments[0].y};
    game->direction = SNAKE_DIRECTION_RIGHT;
    game->pending_direction = SNAKE_DIRECTION_RIGHT;
    game->state.score = 0;
    game->state.foods_eaten = 0;
    game->elapsed_ms = 0;
    game->state.game_over = false;
    game->state.paused = false;
    game->state.game_over_reason = SNAKE_GAME_OVER_NONE;
    snake_refresh_speed(game);
    snake_spawn_food(game);
}

void snake_game_set_input(snake_game_t *game, snake_input_t input)
{
    if (!game) {
        return;
    }
    if (input == SNAKE_INPUT_PAUSE) {
        if (!game->state.game_over) {
            game->state.paused = !game->state.paused;
        }
        return;
    }
    if (input == SNAKE_INPUT_NONE || game->state.game_over ||
        game->state.paused) {
        return;
    }
    snake_direction_t direction = snake_input_direction(input);
    if (!snake_opposite(direction, game->direction)) {
        game->pending_direction = direction;
    }
}

bool snake_game_step(snake_game_t *game)
{
    if (!game || game->state.game_over || game->state.paused ||
        game->state.length == 0U) {
        return false;
    }
    if (!snake_opposite(game->pending_direction, game->direction)) {
        game->direction = game->pending_direction;
    }

    snake_point_t next = game->state.segments[0];
    switch (game->direction) {
        case SNAKE_DIRECTION_UP:
            if (next.y == 0U) {
                if (!game->state.wrap_walls) {
                    snake_set_game_over(game, SNAKE_GAME_OVER_WALL);
                    return false;
                }
                next.y = (uint8_t)(game->state.height - 1U);
            } else {
                --next.y;
            }
            break;
        case SNAKE_DIRECTION_DOWN:
            if ((uint16_t)next.y + 1U >= game->state.height) {
                if (!game->state.wrap_walls) {
                    snake_set_game_over(game, SNAKE_GAME_OVER_WALL);
                    return false;
                }
                next.y = 0;
            } else {
                ++next.y;
            }
            break;
        case SNAKE_DIRECTION_LEFT:
            if (next.x == 0U) {
                if (!game->state.wrap_walls) {
                    snake_set_game_over(game, SNAKE_GAME_OVER_WALL);
                    return false;
                }
                next.x = (uint8_t)(game->state.width - 1U);
            } else {
                --next.x;
            }
            break;
        case SNAKE_DIRECTION_RIGHT:
        default:
            if ((uint16_t)next.x + 1U >= game->state.width) {
                if (!game->state.wrap_walls) {
                    snake_set_game_over(game, SNAKE_GAME_OVER_WALL);
                    return false;
                }
                next.x = 0;
            } else {
                ++next.x;
            }
            break;
    }

    bool ate_food = game->food_valid && snake_same(next, game->state.food);
    uint16_t collision_limit =
        ate_food ? game->state.length
                 : (game->state.length ? game->state.length - 1U : 0U);
    for (uint16_t i = 0; i < collision_limit; ++i) {
        if (snake_same(game->state.segments[i], next)) {
            snake_set_game_over(game, SNAKE_GAME_OVER_SELF_COLLISION);
            return false;
        }
    }

    uint16_t new_length =
        ate_food ? (uint16_t)(game->state.length + 1U) : game->state.length;
    if (new_length > SNAKE_MAX_SEGMENTS) {
        snake_set_game_over(game, SNAKE_GAME_OVER_BOARD_FULL);
        return false;
    }
    for (uint16_t i = new_length - 1U; i > 0U; --i) {
        game->state.segments[i] = game->state.segments[i - 1U];
    }
    game->state.segments[0] = next;
    game->state.length = new_length;
    if (ate_food) {
        game->state.score += 10;
        ++game->state.foods_eaten;
        snake_refresh_speed(game);
        snake_save_best_if_needed(game);
        snake_spawn_food(game);
    }
    return true;
}

void snake_game_advance(snake_game_t *game, uint32_t elapsed_ms)
{
    if (!game || game->state.game_over || game->state.paused) {
        return;
    }
    game->elapsed_ms += elapsed_ms;
    while (!game->state.game_over && !game->state.paused &&
           game->elapsed_ms >= game->state.speed_ms) {
        game->elapsed_ms -= game->state.speed_ms;
        (void)snake_game_step(game);
    }
}

uint32_t snake_game_update(snake_game_t *game, uint32_t elapsed_ms)
{
    if (!game || game->state.game_over || game->state.paused) {
        return 0;
    }
    game->elapsed_ms += elapsed_ms;
    uint32_t moved = 0;
    while (!game->state.game_over && !game->state.paused &&
           game->elapsed_ms >= game->state.speed_ms) {
        game->elapsed_ms -= game->state.speed_ms;
        if (snake_game_step(game)) {
            ++moved;
        }
    }
    return moved;
}

const snake_state_t *snake_game_state(const snake_game_t *game)
{
    return game ? &game->state : NULL;
}

bool snake_game_is_over(const snake_game_t *game)
{
    return !game || game->state.game_over;
}

void snake_game_seed(snake_game_t *game, uint32_t seed)
{
    if (game) {
        game->random_state = seed ? seed : 1U;
    }
}

bool snake_game_force_food(snake_game_t *game, snake_point_t food)
{
    if (!game || food.x >= game->state.width || food.y >= game->state.height ||
        snake_occupied(game, food)) {
        return false;
    }
    game->state.food = food;
    game->food_valid = true;
    return true;
}

snake_status_t snake_game_get_status(const snake_game_t *game)
{
    if (!game) {
        return SNAKE_STATUS_GAME_OVER;
    }
    if (game->state.game_over) {
        return SNAKE_STATUS_GAME_OVER;
    }
    if (game->state.paused) {
        return SNAKE_STATUS_PAUSED;
    }
    return game->state.length ? SNAKE_STATUS_RUNNING : SNAKE_STATUS_READY;
}

snake_direction_t snake_game_get_direction(const snake_game_t *game)
{
    return game ? game->direction : SNAKE_DIRECTION_RIGHT;
}

snake_game_over_reason_t snake_game_get_over_reason(const snake_game_t *game)
{
    return game ? game->state.game_over_reason : SNAKE_GAME_OVER_NONE;
}

uint16_t snake_game_get_speed_ms(const snake_game_t *game)
{
    return game ? game->state.speed_ms : SNAKE_SPEED_SLOW_MS;
}

void snake_game_set_wrap(snake_game_t *game, bool enabled)
{
    if (game) {
        game->state.wrap_walls = enabled;
        game->config.wrap_walls = enabled;
    }
}

void snake_game_set_speed_level(snake_game_t *game, snake_speed_level_t level)
{
    if (!game) {
        return;
    }
    uint16_t base = SNAKE_SPEED_SLOW_MS;
    if (level == SNAKE_SPEED_LEVEL_MEDIUM) {
        base = SNAKE_SPEED_MEDIUM_MS;
    } else if (level >= SNAKE_SPEED_LEVEL_FAST) {
        base = SNAKE_SPEED_FAST_MS;
    }
    game->config.initial_speed_ms = base;
    snake_refresh_speed(game);
}

void snake_game_start(snake_game_t *game)
{
    snake_game_reset(game);
}

void snake_game_handle_input(snake_game_t *game, snake_input_t input)
{
    snake_game_set_input(game, input);
}

uint32_t snake_game_get_score(const snake_game_t *game)
{
    return game && game->state.score > 0 ? (uint32_t)game->state.score : 0U;
}

uint32_t snake_game_get_high_score(const snake_game_t *game)
{
    return game && game->state.best_score > 0
               ? (uint32_t)game->state.best_score
               : 0U;
}

const snake_point_t *snake_game_get_segments(const snake_game_t *game,
                                             uint16_t *length)
{
    if (!game) {
        if (length) {
            *length = 0;
        }
        return NULL;
    }
    if (length) {
        *length = game->state.length;
    }
    return game->state.segments;
}

snake_point_t snake_game_get_food(const snake_game_t *game)
{
    return game ? game->state.food : (snake_point_t){0, 0};
}
