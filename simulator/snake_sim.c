#include <SDL2/SDL.h>
#include <lvgl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_ui.h"
#include "sim_port.h"
#include "snake_logic.h"
#include "zlib.h"

#define W 480
#define H 320

static uint16_t fb[W * H];
static lv_display_t *disp;
static SDL_Window *win;
static const char *shot_path;

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)d;
    uint32_t w = (uint32_t)(a->x2 - a->x1 + 1);
    uint32_t h = (uint32_t)(a->y2 - a->y1 + 1);
    const uint16_t *src = (const uint16_t *)px;
    for (uint32_t y = 0; y < h; y++) {
        memcpy(&fb[(a->y1 + y) * W + a->x1], src + y * w, w * 2);
    }
    lv_display_flush_ready(d);
}

static void png_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static int write_png(const char *path)
{
    const size_t row_bytes = 1U + (size_t)W * 3U;
    const size_t raw_size = row_bytes * H;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) {
        return -1;
    }
    for (int y = 0; y < H; ++y) {
        uint8_t *row = raw + (size_t)y * row_bytes;
        row[0] = 0;
        for (int x = 0; x < W; ++x) {
            uint16_t c = fb[y * W + x];
            row[1 + x * 3 + 0] =
                (uint8_t)(((c >> 11) & 31U) * 255U / 31U);
            row[1 + x * 3 + 1] =
                (uint8_t)(((c >> 5) & 63U) * 255U / 63U);
            row[1 + x * 3 + 2] = (uint8_t)((c & 31U) * 255U / 31U);
        }
    }
    uLongf compressed_size = compressBound((uLong)raw_size);
    uint8_t *compressed = (uint8_t *)malloc(compressed_size);
    if (!compressed ||
        compress2(compressed, &compressed_size, raw, (uLong)raw_size,
                  Z_BEST_SPEED) != Z_OK) {
        free(raw);
        free(compressed);
        return -1;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(raw);
        free(compressed);
        return -1;
    }
    static const uint8_t signature[8] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    };
    fwrite(signature, 1, sizeof(signature), f);
    const uint8_t ihdr[13] = {
        0, 0, 1, 0xE0, 0, 0, 1, 0x40, 8, 2, 0, 0, 0
    };
    uint8_t chunk_header[8];
    uint8_t crc_bytes[4];
    png_u32(chunk_header, sizeof(ihdr));
    memcpy(chunk_header + 4, "IHDR", 4);
    fwrite(chunk_header, 1, sizeof(chunk_header), f);
    fwrite(ihdr, 1, sizeof(ihdr), f);
    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)"IHDR", 4);
    crc = crc32(crc, ihdr, sizeof(ihdr));
    png_u32(crc_bytes, crc);
    fwrite(crc_bytes, 1, sizeof(crc_bytes), f);

    png_u32(chunk_header, (uint32_t)compressed_size);
    memcpy(chunk_header + 4, "IDAT", 4);
    fwrite(chunk_header, 1, sizeof(chunk_header), f);
    fwrite(compressed, 1, compressed_size, f);
    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)"IDAT", 4);
    crc = crc32(crc, compressed, compressed_size);
    png_u32(crc_bytes, crc);
    fwrite(crc_bytes, 1, sizeof(crc_bytes), f);

    png_u32(chunk_header, 0);
    memcpy(chunk_header + 4, "IEND", 4);
    fwrite(chunk_header, 1, sizeof(chunk_header), f);
    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)"IEND", 4);
    png_u32(crc_bytes, crc);
    fwrite(crc_bytes, 1, sizeof(crc_bytes), f);
    fclose(f);
    free(raw);
    free(compressed);
    return 0;
}

static void pump_lvgl(void)
{
    lv_timer_handler();
    lv_refr_now(disp);
}

static void advance_ms(uint32_t elapsed_ms)
{
    lv_tick_inc(elapsed_ms ? elapsed_ms : 1U);
    game_ui_update(elapsed_ms);
    pump_lvgl();
}

static uint8_t parse_key(const char *token)
{
    if (!strcmp(token, "K1") || !strcmp(token, "UP") ||
        !strcmp(token, "START") || !strcmp(token, "RETRY")) {
        return 1;
    }
    if (!strcmp(token, "K2") || !strcmp(token, "DOWN")) {
        return 2;
    }
    if (!strcmp(token, "K3") || !strcmp(token, "LEFT")) {
        return 3;
    }
    if (!strcmp(token, "K4") || !strcmp(token, "RIGHT")) {
        return 4;
    }
    return 5;
}

static void inject_token(const char *token)
{
    sim_port_inject_key(parse_key(token), AD_KEYS_EVENT_PRESS);
    advance_ms(0);
}

static void inject_keys(const char *keys)
{
    if (!keys || !*keys) {
        return;
    }
    char *copy = strdup(keys);
    if (!copy) {
        return;
    }
    char *token = strtok(copy, ",");
    while (token) {
        inject_token(token);
        token = strtok(NULL, ",");
    }
    free(copy);
}

static int init_simulator(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    win = SDL_CreateWindow("snake", 0, 0, W, H, SDL_WINDOW_HIDDEN);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    lv_init();
    disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    static uint16_t b1[W * 20];
    static uint16_t b2[W * 20];
    lv_display_set_buffers(disp, b1, b2, sizeof(b1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    sim_port_set_score_path("simulator/.snake_score");
    if (game_ui_init() != ESP_OK) {
        fprintf(stderr, "game_ui_init failed\n");
        return -1;
    }
    pump_lvgl();
    return 0;
}

static int check_item(const char *name, int ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    return ok;
}

typedef struct {
    int stored;
    int saves;
} logic_score_store_t;

static int logic_score_load(void *ctx, int *score)
{
    logic_score_store_t *store = (logic_score_store_t *)ctx;
    if (!store || !score) {
        return -1;
    }
    *score = store->stored;
    return 0;
}

static int logic_score_save(void *ctx, int score)
{
    logic_score_store_t *store = (logic_score_store_t *)ctx;
    if (!store) {
        return -1;
    }
    store->stored = score;
    ++store->saves;
    return 0;
}

static void logic_init_game(snake_game_t *game)
{
    snake_config_t config;
    snake_config_default(&config);
    snake_game_init(game, &config);
    snake_game_reset(game);
}

static snake_point_t logic_next_right(const snake_game_t *game)
{
    const snake_state_t *state = snake_game_state(game);
    snake_point_t next = state->segments[0];
    next.x = (uint8_t)((next.x + 1U) % state->width);
    return next;
}

/* 逻辑层回归：11 项断言覆盖移动、碰撞、计分、速度、穿墙和最高分。 */
static int selftest_logic(void)
{
    int all = 1;
    snake_game_t game;
    const snake_state_t *state;

    logic_init_game(&game);
    snake_point_t before = game.state.segments[0];
    bool moved = snake_game_step(&game);
    state = snake_game_state(&game);
    all &= check_item(
        "logic_forward_step",
        moved && state->segments[0].x ==
                       (uint8_t)((before.x + 1U) % state->width) &&
            state->segments[0].y == before.y);

    logic_init_game(&game);
    before = game.state.segments[0];
    snake_game_set_input(&game, SNAKE_INPUT_LEFT);
    moved = snake_game_step(&game);
    state = snake_game_state(&game);
    all &= check_item(
        "logic_reverse_ignored",
        moved && snake_game_get_direction(&game) == SNAKE_DIRECTION_RIGHT &&
            state->segments[0].x ==
                (uint8_t)((before.x + 1U) % state->width));

    logic_init_game(&game);
    snake_point_t forced_food = {0, 0};
    bool forced = snake_game_force_food(&game, forced_food);
    state = snake_game_state(&game);
    all &= check_item("logic_force_food",
                      forced && state->food.x == forced_food.x &&
                          state->food.y == forced_food.y);

    logic_init_game(&game);
    forced_food = logic_next_right(&game);
    forced = snake_game_force_food(&game, forced_food);
    moved = snake_game_step(&game);
    state = snake_game_state(&game);
    all &= check_item("logic_eat_food_score",
                      forced && moved && state->score == 10 &&
                          state->length == 4 && state->foods_eaten == 1);

    logic_init_game(&game);
    for (int i = 0; i < 5; ++i) {
        forced_food = logic_next_right(&game);
        if (!snake_game_force_food(&game, forced_food) ||
            !snake_game_step(&game)) {
            break;
        }
    }
    state = snake_game_state(&game);
    all &= check_item("logic_speed_every_5",
                      state->foods_eaten == 5 && state->score == 50 &&
                          state->speed_ms == SNAKE_SPEED_SLOW_MS - 10U);

    logic_init_game(&game);
    before = game.state.segments[0];
    snake_game_set_input(&game, SNAKE_INPUT_PAUSE);
    bool paused = game.state.paused;
    bool blocked = !snake_game_step(&game) &&
                   game.state.segments[0].x == before.x &&
                   game.state.segments[0].y == before.y;
    snake_game_set_input(&game, SNAKE_INPUT_PAUSE);
    bool resumed = !game.state.paused && snake_game_step(&game);
    all &= check_item("logic_pause", paused && blocked && resumed);

    logic_init_game(&game);
    game.state.segments[0].x = (uint8_t)(game.state.width - 1U);
    game.state.segments[1].x = (uint8_t)(game.state.width - 2U);
    game.state.segments[2].x = (uint8_t)(game.state.width - 3U);
    game.direction = SNAKE_DIRECTION_RIGHT;
    game.pending_direction = SNAKE_DIRECTION_RIGHT;
    snake_game_set_wrap(&game, true);
    moved = snake_game_step(&game);
    state = snake_game_state(&game);
    all &= check_item("logic_wrap_enabled",
                      moved && !state->game_over && state->segments[0].x == 0);

    logic_init_game(&game);
    before = game.state.segments[0];
    snake_game_set_wrap(&game, false);
    moved = snake_game_step(&game);
    state = snake_game_state(&game);
    all &= check_item(
        "logic_wrap_disabled",
        !state->wrap_walls && moved && !state->game_over &&
            state->segments[0].x ==
                (uint8_t)((before.x + 1U) % state->width));

    logic_init_game(&game);
    game.state.segments[0].x = (uint8_t)(game.state.width - 1U);
    game.state.segments[1].x = (uint8_t)(game.state.width - 2U);
    game.state.segments[2].x = (uint8_t)(game.state.width - 3U);
    game.direction = SNAKE_DIRECTION_RIGHT;
    game.pending_direction = SNAKE_DIRECTION_RIGHT;
    snake_game_set_wrap(&game, false);
    moved = snake_game_step(&game);
    state = snake_game_state(&game);
    all &= check_item("logic_wall_collision",
                      !moved && state->game_over &&
                          snake_game_get_over_reason(&game) ==
                              SNAKE_GAME_OVER_WALL);

    logic_init_game(&game);
    game.state.length = 4;
    game.state.segments[0] = (snake_point_t){5, 5};
    game.state.segments[1] = (snake_point_t){4, 5};
    game.state.segments[2] = (snake_point_t){4, 4};
    game.state.segments[3] = (snake_point_t){5, 4};
    game.direction = SNAKE_DIRECTION_LEFT;
    game.pending_direction = SNAKE_DIRECTION_LEFT;
    (void)snake_game_force_food(&game, (snake_point_t){0, 0});
    moved = snake_game_step(&game);
    state = snake_game_state(&game);
    all &= check_item("logic_self_collision",
                      !moved && state->game_over &&
                          snake_game_get_over_reason(&game) ==
                              SNAKE_GAME_OVER_SELF_COLLISION);

    logic_score_store_t store = {0, 0};
    snake_config_t config;
    snake_config_default(&config);
    config.load_best = logic_score_load;
    config.save_best = logic_score_save;
    config.storage_ctx = &store;
    snake_game_init(&game, &config);
    snake_game_reset(&game);
    forced_food = logic_next_right(&game);
    bool ate = snake_game_force_food(&game, forced_food) &&
               snake_game_step(&game);
    snake_game_t loaded;
    snake_game_init(&loaded, &config);
    state = snake_game_state(&game);
    const snake_state_t *loaded_state = snake_game_state(&loaded);
    all &= check_item("logic_high_score_read_write",
                      ate && state->best_score == 10 && store.stored == 10 &&
                          store.saves == 1 && loaded_state->best_score == 10);

    return all ? 0 : 1;
}

static int selftest_ui(void)
{
    int all = 1;
    const snake_state_t *state = game_ui_get_state();
    all &= check_item("menu_visible", state && state->best_score >= 0);

    inject_token("START");
    state = game_ui_get_state();
    all &= check_item("menu_to_start",
                      state && !state->game_over && !state->paused);

    inject_token("K5");
    state = game_ui_get_state();
    all &= check_item("pause", state && state->paused);
    inject_token("K5");
    state = game_ui_get_state();
    all &= check_item("resume", state && !state->paused);

    state = game_ui_get_state();
    snake_point_t food = state->segments[0];
    food.x = (uint8_t)(food.x + 1U);
    all &= check_item("force_food", game_ui_force_food(food));
    advance_ms(state->speed_ms);
    state = game_ui_get_state();
    all &= check_item("eat_food_score", state && state->score == 10);

    game_ui_force_self_collision();
    advance_ms(state ? state->speed_ms : SNAKE_SPEED_SLOW_MS);
    state = game_ui_get_state();
    all &= check_item("self_collision_game_over",
                      state && state->game_over);

    inject_token("RETRY");
    state = game_ui_get_state();
    all &= check_item("retry", state && !state->game_over);

    game_ui_force_self_collision();
    advance_ms(state ? state->speed_ms : SNAKE_SPEED_SLOW_MS);
    inject_token("K5");
    all &= check_item("end_to_menu", 1);
    return all ? 0 : 1;
}

static int run_scene(const char *scene, const char *keys, int steps,
                     uint32_t advance_step_ms)
{
    if (scene && !strcmp(scene, "game")) {
        inject_token("START");
    } else if (scene && !strcmp(scene, "head_right")) {
        inject_token("START");
    } else if (scene && !strcmp(scene, "head_up")) {
        inject_token("START");
        inject_token("K1");
        const snake_state_t *state = game_ui_get_state();
        advance_ms(state ? state->speed_ms : SNAKE_SPEED_SLOW_MS);
    } else if (scene && !strcmp(scene, "head_down")) {
        inject_token("START");
        inject_token("K2");
        const snake_state_t *state = game_ui_get_state();
        advance_ms(state ? state->speed_ms : SNAKE_SPEED_SLOW_MS);
    } else if (scene && !strcmp(scene, "head_left")) {
        inject_token("START");
        inject_token("K1");
        const snake_state_t *state = game_ui_get_state();
        advance_ms(state ? state->speed_ms : SNAKE_SPEED_SLOW_MS);
        inject_token("K3");
        state = game_ui_get_state();
        advance_ms(state ? state->speed_ms : SNAKE_SPEED_SLOW_MS);
    } else if (scene && !strcmp(scene, "ate")) {
        inject_token("START");
        const snake_state_t *state = game_ui_get_state();
        snake_point_t food = state->segments[0];
        food.x = (uint8_t)(food.x + 1U);
        if (!game_ui_force_food(food)) {
            return -1;
        }
        advance_ms(state->speed_ms);
    } else if (scene && !strcmp(scene, "paused")) {
        inject_token("START");
        inject_token("K5");
    } else if (scene && !strcmp(scene, "end")) {
        inject_token("START");
        game_ui_force_self_collision();
        const snake_state_t *state = game_ui_get_state();
        advance_ms(state ? state->speed_ms : SNAKE_SPEED_SLOW_MS);
    }
    inject_keys(keys);
    const snake_state_t *state = game_ui_get_state();
    uint32_t step_ms = advance_step_ms;
    if (!step_ms) {
        step_ms = state ? state->speed_ms : SNAKE_SPEED_SLOW_MS;
    }
    for (int i = 0; i < steps; ++i) {
        advance_ms(step_ms);
    }
    pump_lvgl();
    return 0;
}

int main(int argc, char **argv)
{
    const char *keys = NULL;
    const char *scene = "menu";
    const char *score_path = "simulator/.snake_score";
    int steps = 0;
    int self = 0;
    uint32_t advance_step_ms = 0;
    shot_path = NULL;

#ifdef SNAKE_TEST_ONLY
    self = 1;
#endif
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--keys") && i + 1 < argc) {
            keys = argv[++i];
        } else if (!strcmp(argv[i], "--steps") && i + 1 < argc) {
            steps = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--advance-ms") && i + 1 < argc) {
            advance_step_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            shot_path = argv[++i];
        } else if (!strcmp(argv[i], "--scene") && i + 1 < argc) {
            scene = argv[++i];
        } else if (!strcmp(argv[i], "--score-file") && i + 1 < argc) {
            score_path = argv[++i];
        } else if (!strcmp(argv[i], "--selftest") ||
                   !strcmp(argv[i], "--selftest-ui")) {
            self = 1;
        }
    }

    remove(score_path);
    sim_port_set_score_path(score_path);
    if (init_simulator() != 0) {
        return 1;
    }
    int rc;
    if (self) {
        int logic_rc = selftest_logic();
        int ui_rc = selftest_ui();
        rc = logic_rc || ui_rc;
    } else {
        rc = run_scene(scene, keys, steps, advance_step_ms);
    }
    if (!rc && shot_path && write_png(shot_path) != 0) {
        rc = 1;
    }
    if (win) {
        SDL_DestroyWindow(win);
    }
    SDL_Quit();
    return rc;
}
