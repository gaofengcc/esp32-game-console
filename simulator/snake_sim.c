#include <SDL2/SDL.h>
#include <lvgl.h>
#include "snake_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#define W 480
#define H 320
extern const lv_font_t lv_font_cjk_16;

static uint16_t fb[W * H];
static lv_display_t *disp;
static lv_obj_t *root;
static snake_game_t game;
static const char *shot_path;

static int score_load(void *ctx, int *score) {
    const char *p = (const char *)ctx; FILE *f = fopen(p, "rb");
    if(!f) { *score = 0; return 0; }
    int ok = fread(score, sizeof(*score), 1, f) == 1; fclose(f); return ok ? 0 : -1;
}
static int score_save(void *ctx, int score) {
    const char *p = (const char *)ctx; FILE *f = fopen(p, "wb");
    if(!f) return -1; int ok = fwrite(&score, sizeof(score), 1, f) == 1; fclose(f); return ok ? 0 : -1;
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    (void)d;
    uint32_t w = (uint32_t)(a->x2 - a->x1 + 1), h = (uint32_t)(a->y2 - a->y1 + 1);
    const uint16_t *src = (const uint16_t *)px;
    for(uint32_t y = 0; y < h; y++) memcpy(&fb[(a->y1 + y) * W + a->x1], src + y * w, w * 2);
    lv_display_flush_ready(d);
}

static void write_bmp(const char *path) {
    FILE *f = fopen(path, "wb"); if(!f) return;
    uint32_t row = W * 3, pad = (4 - row % 4) % 4, size = 54 + (row + pad) * H;
    uint8_t hdr[54] = {0}; hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &size, 4); uint32_t off = 54; memcpy(hdr + 10, &off, 4);
    uint32_t hs = 40; memcpy(hdr + 14, &hs, 4); int32_t iw = W, ih = H; memcpy(hdr + 18, &iw, 4); memcpy(hdr + 22, &ih, 4);
    uint16_t planes = 1, bpp = 24; memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2);
    fwrite(hdr, 1, 54, f);
    uint8_t *line = (uint8_t *)malloc(row + pad);
    for(int y = H - 1; y >= 0; y--) {
        for(int x = 0; x < W; x++) {
            uint16_t c = fb[y * W + x]; uint8_t r = ((c >> 11) & 31) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
            line[x * 3 + 0] = b; line[x * 3 + 1] = g; line[x * 3 + 2] = r;
        }
        memset(line + row, 0, pad); fwrite(line, 1, row + pad, f);
    }
    free(line); fclose(f);
}

static void png_u32(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static int write_png(const char *path) {
    const size_t row_bytes = 1U + (size_t)W * 3U;
    const size_t raw_size = row_bytes * H;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) return -1;
    for (int y = 0; y < H; ++y) {
        uint8_t *row = raw + (size_t)y * row_bytes;
        row[0] = 0;
        for (int x = 0; x < W; ++x) {
            uint16_t c = fb[y * W + x];
            row[1 + x * 3 + 0] = (uint8_t)(((c >> 11) & 31U) * 255U / 31U);
            row[1 + x * 3 + 1] = (uint8_t)(((c >> 5) & 63U) * 255U / 63U);
            row[1 + x * 3 + 2] = (uint8_t)((c & 31U) * 255U / 31U);
        }
    }
    uLongf compressed_size = compressBound((uLong)raw_size);
    uint8_t *compressed = (uint8_t *)malloc(compressed_size);
    if (!compressed || compress2(compressed, &compressed_size, raw, (uLong)raw_size,
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
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(signature, 1, sizeof(signature), f);
    const uint8_t ihdr[13] = {
        0, 0, 1, 0xE0, 0, 0, 1, 0x40, 8, 2, 0, 0, 0
    };
    uint8_t chunk_header[8];
    png_u32(chunk_header, sizeof(ihdr));
    memcpy(chunk_header + 4, "IHDR", 4);
    fwrite(chunk_header, 1, sizeof(chunk_header), f);
    fwrite(ihdr, 1, sizeof(ihdr), f);
    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)"IHDR", 4);
    crc = crc32(crc, ihdr, sizeof(ihdr));
    uint8_t crc_bytes[4];
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
    crc = crc32(0L, (const Bytef *)"IEND", 4);
    png_u32(crc_bytes, crc);
    fwrite(crc_bytes, 1, sizeof(crc_bytes), f);
    fclose(f);
    free(raw);
    free(compressed);
    return 0;
}

static void write_shot(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext && !strcmp(ext, ".png")) {
        (void)write_png(path);
    } else {
        write_bmp(path);
    }
}

static void clear_root(void) {
    if(root) lv_obj_clean(root);
    root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x101820), 0);
}
static void label_text(const char *txt, int x, int y, int size) {
    lv_obj_t *l = lv_label_create(root); lv_label_set_text(l, txt); lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(l, &lv_font_cjk_16, 0);
}
static void draw_scene(int mode) {
    clear_root();
    if(mode == 0) {
        label_text("贪吃蛇", 180, 45, 0); label_text("K1开始  K2难度  K3穿墙  K5退出", 95, 120, 0);
        char buf[96]; snprintf(buf, sizeof(buf), "最高分: %u   穿墙: %s", (unsigned)game.state.best_score, game.state.wrap_walls ? "开" : "关"); label_text(buf, 125, 170, 0);
    } else if(mode == 2) {
        char buf[96]; snprintf(buf, sizeof(buf), "游戏结束  得分: %u  最高分: %u", (unsigned)game.state.score, (unsigned)game.state.best_score); label_text(buf, 90, 120, 0);
        label_text("K1再来一次   K5返回菜单", 130, 180, 0);
    } else {
        /* 棋盘背景和局部格子 */
        lv_obj_t *board = lv_obj_create(root); lv_obj_set_size(board, W, H); lv_obj_set_pos(board, 0, 0);
        lv_obj_set_style_bg_color(board, lv_color_hex(0x081018), 0); lv_obj_set_style_border_width(board, 0, 0); lv_obj_set_style_pad_all(board, 0, 0);
        uint16_t n = snake_game_state(&game)->length; const snake_point_t *seg = snake_game_state(&game)->segments;
        for(uint16_t i = 0; i < n; i++) { lv_obj_t *c = lv_obj_create(board); lv_obj_set_size(c, 14, 14); lv_obj_set_pos(c, seg[i].x * 16 + 1, seg[i].y * 16 + 1); lv_obj_set_style_bg_color(c, lv_color_hex(i == 0 ? 0x00ff88 : 0x00aa55), 0); lv_obj_set_style_border_width(c, 0, 0); }
        snake_point_t food = snake_game_state(&game)->food; if(1) { lv_obj_t *c = lv_obj_create(board); lv_obj_set_size(c, 14, 14); lv_obj_set_pos(c, food.x * 16 + 1, food.y * 16 + 1); lv_obj_set_style_bg_color(c, lv_color_hex(0xff3344), 0); lv_obj_set_style_border_width(c, 0, 0); }
        char buf[64]; snprintf(buf, sizeof(buf), "分数 %u  速度 %ums%s", (unsigned)game.state.score, (unsigned)game.state.speed_ms, game.state.paused ? "  [暂停]" : ""); label_text(buf, 8, 4, 0);
    }
}

static snake_input_t parse_key(const char *s) {
    if(!strcmp(s, "K1")) return SNAKE_INPUT_UP;
    if(!strcmp(s, "K2")) return SNAKE_INPUT_DOWN;
    if(!strcmp(s, "K3")) return SNAKE_INPUT_LEFT;
    if(!strcmp(s, "K4")) return SNAKE_INPUT_RIGHT;
    return SNAKE_INPUT_PAUSE;
}

static int check_item(const char *name, int ok) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    return ok;
}

static int selftest(void) {
    int all = 1;
    const char *path = "simulator/.snake_test_score";
    remove(path);
    snake_config_t cfg = {30, 20, 260, true, score_load, score_save, (void *)path};
    snake_game_init(&game, &cfg);
    snake_game_reset(&game);

    snake_point_t before = game.state.segments[0];
    all &= check_item("前进", snake_game_step(&game) &&
                      game.state.segments[0].x == (uint8_t)(before.x + 1U));

    snake_game_set_input(&game, SNAKE_INPUT_LEFT);
    all &= check_item("反向输入忽略",
                      game.pending_direction == SNAKE_DIRECTION_RIGHT);

    snake_point_t food = game.state.segments[0];
    food.x = (uint8_t)(food.x + 1U);
    all &= check_item("强制食物", snake_game_force_food(&game, food));
    uint16_t old_speed = game.state.speed_ms;
    all &= check_item("吃食物计分", snake_game_step(&game) &&
                      game.state.score == 10);
    for (int i = 0; i < 4; ++i) {
        snake_point_t next = game.state.segments[0];
        next.x = (uint8_t)((next.x + 1U) % game.state.width);
        if (!snake_game_force_food(&game, next) || !snake_game_step(&game)) {
            all = 0;
            break;
        }
    }
    all &= check_item("每 5 个提速 10ms", game.state.foods_eaten == 5U &&
                      game.state.speed_ms == (uint16_t)(old_speed - 10U));

    snake_game_set_input(&game, SNAKE_INPUT_PAUSE);
    int paused = game.state.paused;
    snake_game_set_input(&game, SNAKE_INPUT_PAUSE);
    all &= check_item("暂停", paused && !game.state.paused);

    snake_game_set_wrap(&game, false);
    snake_game_reset(&game);
    game.state.segments[0].x = (uint8_t)(game.state.width - 1U);
    all &= check_item("关闭穿墙撞墙结束", !snake_game_step(&game) &&
                      game.state.game_over);

    snake_game_set_wrap(&game, true);
    snake_game_reset(&game);
    game.state.segments[0].x = 0;
    all &= check_item("开启穿墙", snake_game_step(&game) &&
                      game.state.segments[0].x == 1U);

    snake_game_reset(&game);
    game.state.length = 4;
    game.state.segments[0] = (snake_point_t){10, 10};
    game.state.segments[1] = (snake_point_t){9, 10};
    game.state.segments[2] = (snake_point_t){10, 9};
    game.state.segments[3] = (snake_point_t){11, 9};
    game.direction = SNAKE_DIRECTION_UP;
    game.pending_direction = SNAKE_DIRECTION_UP;
    all &= check_item("撞自己结束", !snake_game_step(&game) &&
                      game.state.game_over);

    all &= check_item("最高分写入", score_save((void *)path, 42) == 0);
    snake_game_init(&game, &cfg);
    all &= check_item("最高分读取", game.state.best_score == 42);
    remove(path);
    return all ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *keys = NULL, *scene = NULL; int steps = 0, self = 0; shot_path = NULL;
#ifdef SNAKE_TEST_ONLY
    (void)argc;
    (void)argv;
    return selftest();
#endif
    for(int i = 1; i < argc; i++) { if(!strcmp(argv[i], "--keys") && i + 1 < argc) keys = argv[++i]; else if(!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]); else if(!strcmp(argv[i], "--shot") && i + 1 < argc) shot_path = argv[++i]; else if(!strcmp(argv[i], "--scene") && i + 1 < argc) scene = argv[++i]; else if(!strcmp(argv[i], "--selftest")) self = 1; }
    if(self) return selftest();
    SDL_SetHint(SDL_HINT_VIDEODRIVER, getenv("SDL_VIDEODRIVER") ? getenv("SDL_VIDEODRIVER") : "dummy"); SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win = SDL_CreateWindow("snake", 0, 0, W, H, SDL_WINDOW_HIDDEN); (void)win;
    lv_init(); disp = lv_display_create(W, H); lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565); lv_display_set_flush_cb(disp, flush_cb);
    static uint16_t b1[W * 20], b2[W * 20]; lv_display_set_buffers(disp, b1, b2, sizeof(b1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    snake_config_t st = {30, 20, 260, true, score_load, score_save, (void *)"simulator/.snake_score"}; snake_game_init(&game, &st); snake_game_reset(&game);
    if(keys) { char *tmp = strdup(keys), *tok = strtok(tmp, ","); while(tok) { snake_game_set_input(&game, parse_key(tok)); tok = strtok(NULL, ","); } free(tmp); }
    for(int i = 0; i < steps; i++) snake_game_step(&game);
    int mode = scene && !strcmp(scene, "menu") ? 0 : (scene && !strcmp(scene, "end") ? 2 : (game.state.game_over ? 2 : 1)); draw_scene(mode); lv_timer_handler(); if(shot_path) write_shot(shot_path);
    if(win) SDL_DestroyWindow(win); SDL_Quit(); return 0;
}
