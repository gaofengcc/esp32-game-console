/*
 * 华容道关卡求解器, 开发期主机工具, 不进入固件.
 *
 * 职责:
 * 1. 组合枚举 4x5 标准 10 棋子的全部合法布局 (65,880 个).
 * 2. 多源反向 BFS (单格滑动口径) 求每个布局到出口的最少步数.
 * 3. 按难度目标挑选关卡, 保证可解且步数实测, 生成 source/game/klotski_levels.h.
 *
 * 校验锚点: 横刀立马单格口径 116 步, 与公开 BFS 结果一致.
 *
 * 用法: cc -O2 -std=c11 -Wall -Wextra -o /tmp/klotski_solver tools/klotski_solver.c
 *       /tmp/klotski_solver [输出头文件路径]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 4
#define H 5
#define NCELLS (W * H)
#define EXIT_ANCHOR 13 /* 曹操 anchor 到底部中间: (x=1, y=3) */
#define TOP_CENTER 1   /* 曹操 anchor 在顶部中间: (x=1, y=0) */
#define MAX_STATES 70000

/* 状态编码 50bit: cao(5) | guan(5) | generals 4x5 (升序) | soldiers 4x5 (升序) */

static uint64_t pack(int cao, int h, const int *v, const int *s)
{
    uint64_t k = 0;
    int i;

    k |= (uint64_t)cao;
    k |= (uint64_t)h << 5;
    for (i = 0; i < 4; i++) {
        k |= (uint64_t)v[i] << (10 + 5 * i);
    }
    for (i = 0; i < 4; i++) {
        k |= (uint64_t)s[i] << (30 + 5 * i);
    }
    return k;
}

static void unpack(uint64_t k, int *cao, int *h, int *v, int *s)
{
    int i;

    *cao = (int)(k & 31);
    *h = (int)((k >> 5) & 31);
    for (i = 0; i < 4; i++) {
        v[i] = (int)((k >> (10 + 5 * i)) & 31);
    }
    for (i = 0; i < 4; i++) {
        s[i] = (int)((k >> (30 + 5 * i)) & 31);
    }
}

static void sort4(int *a)
{
    int i;

    for (i = 1; i < 4; i++) {
        int t = a[i];
        int j = i - 1;

        while (j >= 0 && a[j] > t) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = t;
    }
}

#define HASH_BITS 18
#define HASH_SIZE (1u << HASH_BITS)
#define HASH_EMPTY UINT64_MAX

static uint64_t h_keys[HASH_SIZE];
static uint32_t h_vals[HASH_SIZE];

static uint64_t mix(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static uint32_t hash_find(uint64_t key)
{
    uint64_t slot = mix(key) & (HASH_SIZE - 1);

    while (h_keys[slot] != HASH_EMPTY) {
        if (h_keys[slot] == key) {
            return h_vals[slot];
        }
        slot = (slot + 1) & (HASH_SIZE - 1);
    }
    return UINT32_MAX;
}

static void hash_insert(uint64_t key, uint32_t val)
{
    uint64_t slot = mix(key) & (HASH_SIZE - 1);

    while (h_keys[slot] != HASH_EMPTY) {
        slot = (slot + 1) & (HASH_SIZE - 1);
    }
    h_keys[slot] = key;
    h_vals[slot] = val;
}

static void piece_cells(int tag, int anchor, int *cells, int *cnt)
{
    if (tag == 1) { /* 曹操 2x2 */
        cells[0] = anchor;
        cells[1] = anchor + 1;
        cells[2] = anchor + W;
        cells[3] = anchor + W + 1;
        *cnt = 4;
    } else if (tag == 2) { /* 关羽 2x1 */
        cells[0] = anchor;
        cells[1] = anchor + 1;
        *cnt = 2;
    } else if (tag == 3) { /* 竖将 1x2 */
        cells[0] = anchor;
        cells[1] = anchor + W;
        *cnt = 2;
    } else { /* 兵 1x1 */
        cells[0] = anchor;
        *cnt = 1;
    }
}

static void build_board(uint64_t key, int8_t *board, int *cao, int *h,
                        int *v, int *s)
{
    int cells[4];
    int cnt;
    int i;
    int j;

    unpack(key, cao, h, v, s);
    memset(board, 0, NCELLS);
    piece_cells(1, *cao, cells, &cnt);
    for (i = 0; i < cnt; i++) {
        board[cells[i]] = 1;
    }
    piece_cells(2, *h, cells, &cnt);
    for (i = 0; i < cnt; i++) {
        board[cells[i]] = 2;
    }
    for (i = 0; i < 4; i++) {
        piece_cells(3, v[i], cells, &cnt);
        for (j = 0; j < cnt; j++) {
            board[cells[j]] = 3 + i;
        }
    }
    for (i = 0; i < 4; i++) {
        board[s[i]] = 7 + i;
    }
}

/* 单格滑动口径的后继状态生成 */
static int gen_neighbors(uint64_t key, uint64_t *out)
{
    int cao;
    int h;
    int v[4];
    int s[4];
    int8_t board[NCELLS];
    int n = 0;
    int p;
    int d;
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    build_board(key, board, &cao, &h, v, s);
    for (p = 0; p < 10; p++) {
        int tag;
        int anchor;
        int self;
        int pidx;
        int pc[4];
        int pcnt;

        if (p == 0) {
            tag = 1; anchor = cao; self = 1; pidx = -1;
        } else if (p == 1) {
            tag = 2; anchor = h; self = 2; pidx = -1;
        } else if (p < 6) {
            tag = 3; pidx = p - 2; anchor = v[pidx]; self = 3 + pidx;
        } else {
            tag = 4; pidx = p - 6; anchor = s[pidx]; self = 7 + pidx;
        }
        piece_cells(tag, anchor, pc, &pcnt);

        for (d = 0; d < 4; d++) {
            int dx = dirs[d][0];
            int dy = dirs[d][1];
            int nc[4];
            int ok = 1;
            int i;
            int na;
            int nv[4];
            int ns[4];
            int ncao = cao;
            int nh = h;

            for (i = 0; i < pcnt && ok; i++) {
                int x = pc[i] % W + dx;
                int y = pc[i] / W + dy;
                int c;

                if (x < 0 || x >= W || y < 0 || y >= H) {
                    ok = 0;
                    break;
                }
                c = y * W + x;
                if (board[c] != 0 && board[c] != self) {
                    ok = 0;
                    break;
                }
                nc[i] = c;
            }
            if (!ok) {
                continue;
            }
            na = nc[0];
            for (i = 1; i < pcnt; i++) {
                if (nc[i] < na) {
                    na = nc[i];
                }
            }
            memcpy(nv, v, sizeof(nv));
            memcpy(ns, s, sizeof(ns));
            if (p == 0) {
                ncao = na;
            } else if (p == 1) {
                nh = na;
            } else if (p < 6) {
                nv[pidx] = na;
                sort4(nv);
            } else {
                ns[pidx] = na;
                sort4(ns);
            }
            out[n++] = pack(ncao, nh, nv, ns);
        }
    }
    return n;
}

static uint64_t all[MAX_STATES];
static uint32_t all_n;
static uint16_t *dist_global;

static void enum_soldiers(int cao, int h, const int *v, const int8_t *board)
{
    int rest[8];
    int rn = 0;
    int c;
    int i;
    int j;

    for (c = 0; c < NCELLS; c++) {
        if (board[c] == 0) {
            rest[rn++] = c;
        }
    }
    /* 剩 6 格: 选 2 个当空格, 其余 4 个是兵 */
    for (i = 0; i < rn; i++) {
        for (j = i + 1; j < rn; j++) {
            int s[4];
            int sn = 0;
            int k;

            for (k = 0; k < rn; k++) {
                if (k != i && k != j) {
                    s[sn++] = rest[k];
                }
            }
            sort4(s);
            all[all_n] = pack(cao, h, v, s);
            hash_insert(all[all_n], all_n);
            all_n++;
        }
    }
}

static void enum_v(int k, int start, int cao, int h, int *v, int8_t *board)
{
    int a;

    if (k == 4) {
        enum_soldiers(cao, h, v, board);
        return;
    }
    for (a = start; a + W < NCELLS; a++) {
        if (board[a] == 0 && board[a + W] == 0) {
            board[a] = 3;
            board[a + W] = 3;
            v[k] = a;
            enum_v(k + 1, a + 1, cao, h, v, board);
            board[a] = 0;
            board[a + W] = 0;
        }
    }
}

static void enumerate_all(void)
{
    int8_t board[NCELLS];
    int cx;
    int cy;
    int hx;
    int hy;

    for (cy = 0; cy < H - 1; cy++) {
        for (cx = 0; cx < W - 1; cx++) {
            int cao = cy * W + cx;

            memset(board, 0, sizeof(board));
            board[cao] = 1;
            board[cao + 1] = 1;
            board[cao + W] = 1;
            board[cao + W + 1] = 1;
            for (hy = 0; hy < H; hy++) {
                for (hx = 0; hx < W - 1; hx++) {
                    int h = hy * W + hx;
                    int v[4];

                    if (board[h] != 0 || board[h + 1] != 0) {
                        continue;
                    }
                    board[h] = 2;
                    board[h + 1] = 2;
                    enum_v(0, 0, cao, h, v, board);
                    board[h] = 0;
                    board[h + 1] = 0;
                }
            }
        }
    }
}

/* 多源反向 BFS: 从全部胜利状态出发, 求每个布局的最少解题步数 */
static void solve_distances(void)
{
    uint32_t *q = malloc(all_n * sizeof(uint32_t));
    uint64_t *nbuf = malloc(64 * sizeof(uint64_t));
    uint64_t head = 0;
    uint64_t tail = 0;
    uint32_t i;

    if (!q || !nbuf) {
        fprintf(stderr, "内存不足\n");
        exit(1);
    }
    memset(dist_global, 0xFF, all_n * sizeof(uint16_t));
    for (i = 0; i < all_n; i++) {
        if ((int)(all[i] & 31) == EXIT_ANCHOR) {
            dist_global[i] = 0;
            q[tail++] = i;
        }
    }
    while (head < tail) {
        uint32_t ci = q[head++];
        int nn = gen_neighbors(all[ci], nbuf);
        int k;

        for (k = 0; k < nn; k++) {
            uint32_t idx = hash_find(nbuf[k]);

            if (idx != UINT32_MAX && dist_global[idx] == 0xFFFF) {
                dist_global[idx] = (uint16_t)(dist_global[ci] + 1U);
                q[tail++] = idx;
            }
        }
    }
    free(q);
    free(nbuf);
}

/*
 * 在约束下挑一个最少步数最接近 target 且未使用过的布局.
 * top_center 非 0 时要求曹操在顶部中间.
 * 返回状态下标, 失败返回 UINT32_MAX.
 */
static uint32_t pick_layout(int target, int top_center, const uint8_t *used)
{
    uint32_t best = UINT32_MAX;
    int best_delta = 1 << 30;
    uint32_t i;

    for (i = 0; i < all_n; i++) {
        int d;
        int delta;

        if (used[i] || dist_global[i] == 0xFFFF) {
            continue;
        }
        if (top_center && (int)(all[i] & 31) != TOP_CENTER) {
            continue;
        }
        d = (int)dist_global[i];
        delta = d > target ? d - target : target - d;
        if (delta < best_delta ||
            (delta == best_delta && best != UINT32_MAX && all[i] < all[best])) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static int emit_header(const char *path, const uint32_t *levels,
                       const char *const *names, const uint8_t *tiers,
                       uint32_t count)
{
    FILE *f = fopen(path, "w");
    uint32_t i;

    if (!f) {
        fprintf(stderr, "无法写入 %s\n", path);
        return -1;
    }
    fprintf(f, "/* 由 tools/klotski_solver 生成, 请勿手改.\n");
    fprintf(f, " * min_steps 为单格滑动口径的实测最少步数, 布局均已验证可解. */\n");
    fprintf(f,
            "static const klotski_level_def_t "
            "s_klotski_levels[KLOTSKI_LEVEL_COUNT] = {\n");
    for (i = 0; i < count; i++) {
        int cao;
        int h;
        int v[4];
        int s[4];

        unpack(all[levels[i]], &cao, &h, v, s);
        fprintf(f,
                "    {\"%s\", %u, %uU, {%uU, %uU, {%uU, %uU, %uU, %uU}, "
                "{%uU, %uU, %uU, %uU}}},\n",
                names[i], (unsigned)tiers[i], (unsigned)dist_global[levels[i]],
                (unsigned)cao, (unsigned)h, (unsigned)v[0], (unsigned)v[1],
                (unsigned)v[2], (unsigned)v[3], (unsigned)s[0],
                (unsigned)s[1], (unsigned)s[2], (unsigned)s[3]);
    }
    fprintf(f, "};\n");
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *out_path =
        (argc > 1) ? argv[1] : "source/game/klotski_levels.h";
    /* 入门: 任意布局; 进阶/大师: 曹操顶部中间的传统开局 */
    static const int easy_targets[6] = {6, 12, 18, 24, 30, 36};
    static const int normal_targets[12] = {41, 44, 47, 50, 53, 56,
                                           60, 64, 68, 72, 76, 80};
    static const int master_targets[5] = {85, 90, 95, 100, 105};
    uint32_t levels[24];
    const char *names[24];
    uint8_t tiers[24];
    char name_buf[24][16];
    uint8_t *used;
    uint32_t n = 0;
    uint32_t i;
    uint32_t hengdao_idx;

    for (i = 0; i < HASH_SIZE; i++) {
        h_keys[i] = HASH_EMPTY;
    }
    enumerate_all();
    printf("全部合法布局: %u\n", all_n);
    if (all_n != 65880U) {
        fprintf(stderr, "布局总数异常, 预期 65880\n");
        return 1;
    }
    dist_global = malloc(all_n * sizeof(uint16_t));
    used = calloc(all_n, 1);
    if (!dist_global || !used) {
        fprintf(stderr, "内存不足\n");
        return 1;
    }
    solve_distances();

    /* 校验锚点: 横刀立马单格口径 116 步 */
    {
        int v0[4] = {0, 3, 8, 11};
        int s0[4] = {13, 14, 16, 19};
        uint64_t key = pack(1, 9, v0, s0);

        hengdao_idx = hash_find(key);
        if (hengdao_idx == UINT32_MAX ||
            dist_global[hengdao_idx] != 116U) {
            fprintf(stderr, "横刀立马校验失败\n");
            return 1;
        }
        printf("横刀立马最少步数: %u (校验通过)\n",
               (unsigned)dist_global[hengdao_idx]);
    }

    for (i = 0; i < 6U; i++) {
        levels[n] = pick_layout(easy_targets[i], 0, used);
        tiers[n] = 0;
        n++;
    }
    for (i = 0; i < 12U; i++) {
        levels[n] = pick_layout(normal_targets[i], 1, used);
        tiers[n] = 1;
        n++;
    }
    for (i = 0; i < 5U; i++) {
        levels[n] = pick_layout(master_targets[i], 1, used);
        tiers[n] = 2;
        n++;
    }
    levels[n] = hengdao_idx;
    tiers[n] = 2;
    n++;
    if (n != 24U) {
        fprintf(stderr, "关卡数异常\n");
        return 1;
    }
    for (i = 0; i < n; i++) {
        if (levels[i] == UINT32_MAX) {
            fprintf(stderr, "第 %u 关选不出布局\n", (unsigned)(i + 1));
            return 1;
        }
        used[levels[i]] = 1;
        if (i == n - 1U) {
            snprintf(name_buf[i], sizeof(name_buf[i]), "横刀立马");
        } else {
            snprintf(name_buf[i], sizeof(name_buf[i]), "第%u关",
                     (unsigned)(i + 1));
        }
        names[i] = name_buf[i];
    }
    if (emit_header(out_path, levels, names, tiers, n) != 0) {
        return 1;
    }
    printf("已生成 %s (%u 关)\n", out_path, (unsigned)n);
    for (i = 0; i < n; i++) {
        printf("  %-10s 难度%u 最少 %3u 步\n", names[i],
               (unsigned)tiers[i], (unsigned)dist_global[levels[i]]);
    }
    free(dist_global);
    free(used);
    return 0;
}
