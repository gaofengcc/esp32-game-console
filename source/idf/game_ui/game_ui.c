#include "game_ui.h"

#include "game_select_ui.h"
#include "game_ui_common.h"
#include "maze_ui.h"
#include "snake_ui.h"

#define GAME_UI_KEY_QUEUE_LEN 24

typedef enum {
    GAME_UI_KIND_SELECT = 0,
    GAME_UI_KIND_SNAKE,
    GAME_UI_KIND_MAZE,
} game_ui_kind_t;

typedef struct {
    uint8_t key;
    ad_keys_event_type_t type;
} game_ui_key_event_t;

static const char *TAG = "game_ui";
static game_ui_key_event_t s_key_queue[GAME_UI_KEY_QUEUE_LEN];
static uint8_t s_key_head;
static uint8_t s_key_tail;
static bool s_task_started;
static game_ui_kind_t s_kind;
static bool s_touch_calibration_requested;

/**
 * @brief 取当前活动模块的诊断页名.
 *
 * @return 选择页/贪吃蛇/迷宫各自的静态页名.
 */
static const char *game_ui_kind_page_name(void)
{
    if (s_kind == GAME_UI_KIND_SNAKE) {
        return snake_ui_page_name();
    }
    if (s_kind == GAME_UI_KIND_MAZE) {
        return maze_ui_page_name();
    }
    return game_select_ui_page_name();
}

/**
 * @brief 各游戏模块返回选择页时的统一入口.
 *
 * @return 无.
 */
static void game_ui_back_to_select(void)
{
    s_kind = GAME_UI_KIND_SELECT;
    game_select_ui_enter();
    game_ui_port_log_i(TAG, "返回游戏选择");
}

/**
 * @brief 选择页确认后切到对应游戏设置.
 *
 * @param id 选中的游戏, GAME_SELECT_MAZE 或贪吃蛇.
 * @return 无.
 */
static void game_ui_on_choose(game_select_id_t id)
{
    if (id == GAME_SELECT_MAZE) {
        s_kind = GAME_UI_KIND_MAZE;
        maze_ui_enter_menu();
        game_ui_port_log_i(TAG, "进入迷宫设置");
        return;
    }
    s_kind = GAME_UI_KIND_SNAKE;
    snake_ui_enter_menu();
    game_ui_port_log_i(TAG, "进入贪吃蛇设置");
}

/**
 * @brief 按键中断/任务回调: 只入队 PRESS/REPEAT, 不直接碰 LVGL.
 *
 * @param event 按键事件, 为空则忽略.
 * @param user_ctx 未使用.
 * @return 无.
 */
static void game_ui_event_callback(const ad_keys_event_t *event, void *user_ctx)
{
    uint8_t next;

    (void)user_ctx;
    if (!event || (event->type != AD_KEYS_EVENT_PRESS &&
                   event->type != AD_KEYS_EVENT_REPEAT)) {
        return;
    }
    next = (uint8_t)((s_key_head + 1U) % GAME_UI_KEY_QUEUE_LEN);
    if (next == s_key_tail) {
        game_ui_port_log_i(TAG, "按键队列已满, 丢弃 K%u",
                           (unsigned)event->key);
        return;
    }
    s_key_queue[s_key_head] = (game_ui_key_event_t){
        .key = event->key,
        .type = event->type,
    };
    s_key_head = next;
}

/**
 * @brief 从环形队列取出一个按键事件.
 *
 * @param item 输出槽, 为空时仍会丢弃队头.
 * @return 队列非空则为 true.
 */
static bool game_ui_pop_key(game_ui_key_event_t *item)
{
    if (s_key_tail == s_key_head) {
        return false;
    }
    if (item) {
        *item = s_key_queue[s_key_tail];
    }
    s_key_tail = (uint8_t)((s_key_tail + 1U) % GAME_UI_KEY_QUEUE_LEN);
    return true;
}

/**
 * @brief 按当前模块把按键分发给选择页, 贪吃蛇或迷宫.
 *
 * @param item 已出队的按键, 为空则忽略.
 * @return 无.
 */
static void game_ui_process_key_event(const game_ui_key_event_t *item)
{
    if (!item) {
        return;
    }
    game_ui_port_log_i(TAG, "处理按键 K%u type=%u page=%s",
                       (unsigned)item->key, (unsigned)item->type,
                       game_ui_kind_page_name());
    if (s_kind == GAME_UI_KIND_SNAKE) {
        snake_ui_handle_key(item->key, item->type);
        return;
    }
    if (s_kind == GAME_UI_KIND_MAZE) {
        maze_ui_handle_key(item->key, item->type);
        return;
    }
    game_select_ui_handle_key(item->key, item->type);
}

/**
 * @brief 在 LVGL 线程渲染当前活动模块.
 *
 * @param user_data 透传给具体模块, 当前未使用.
 * @return 无.
 */
static void game_ui_render_current(void *user_data)
{
    if (s_kind == GAME_UI_KIND_SNAKE) {
        snake_ui_render(user_data);
        return;
    }
    if (s_kind == GAME_UI_KIND_MAZE) {
        maze_ui_render(user_data);
        return;
    }
    game_select_ui_render(user_data);
}

/**
 * @brief LVGL 线程入口: 有待重绘或蛇身扭动脏标记时刷新.
 *
 * @param user_data 未使用.
 * @return 无.
 */
static void game_ui_update_lvgl(void *user_data)
{
    bool pending;

    (void)user_data;
    pending = game_ui_consume_render();
    if (pending || (s_kind == GAME_UI_KIND_SNAKE && snake_ui_wiggle_dirty())) {
        game_ui_render_current(NULL);
        snake_ui_clear_wiggle_dirty();
    }
}

/**
 * @brief 消化按键, 推进当前游戏逻辑, 再切到 LVGL 线程重绘.
 *
 * @param elapsed_ms 距上次调用的毫秒数.
 * @return 无.
 */
void game_ui_update(uint32_t elapsed_ms)
{
    game_ui_key_event_t item;

    while (game_ui_pop_key(&item)) {
        game_ui_process_key_event(&item);
    }
    if (s_kind == GAME_UI_KIND_MAZE) {
        maze_ui_advance(elapsed_ms);
    } else if (s_kind == GAME_UI_KIND_SNAKE) {
        snake_ui_advance(elapsed_ms);
    }
    (void)game_ui_port_call(game_ui_update_lvgl, NULL);
}

/**
 * @brief 游戏 UI 周期任务: 20ms 推进一次, 并在需要时请求触摸校准.
 *
 * @param arg 未使用.
 * @return 无. 函数不会返回.
 */
static void game_ui_task(void *arg)
{
    uint32_t last;

    (void)arg;
    last = game_ui_port_tick_ms();
    for (;;) {
        uint32_t now = game_ui_port_tick_ms();
        uint32_t elapsed = now - last;

        last = now;
        if (elapsed > 1000U) {
            elapsed = GAME_UI_TICK_MS;
        }
        game_ui_update(elapsed);
        if (!game_ui_port_touch_calibration_valid() &&
            !s_touch_calibration_requested) {
            (void)game_ui_port_request_touch_calibration();
            s_touch_calibration_requested = true;
        }
        game_ui_port_delay_ms(GAME_UI_TICK_MS);
    }
}

/**
 * @brief 首次加载游戏选择页, 必须在 LVGL 线程调用.
 *
 * @param user_data 未使用.
 * @return 无.
 */
static void game_ui_create_initial(void *user_data)
{
    (void)user_data;
    s_kind = GAME_UI_KIND_SELECT;
    game_select_ui_enter();
    game_ui_port_log_i(TAG, "首次加载游戏选择");
    game_select_ui_render(NULL);
    (void)game_ui_consume_render();
}

/**
 * @brief 初始化各 UI 模块, 注册按键并启动 game_ui 任务.
 *
 * @return ESP_OK 成功; 已初始化也返回 ESP_OK; 失败返回 port 错误码.
 */
esp_err_t game_ui_init(void)
{
    esp_err_t err;

    if (s_task_started) {
        return ESP_OK;
    }
    game_ui_common_init();
    game_select_ui_init(game_ui_on_choose);
    snake_ui_init(game_ui_back_to_select);
    maze_ui_init(game_ui_back_to_select);
    s_kind = GAME_UI_KIND_SELECT;
    s_key_head = 0;
    s_key_tail = 0;
    game_ui_port_set_key_callback(game_ui_event_callback, NULL);
    err = game_ui_port_call(game_ui_create_initial, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = game_ui_port_start_task(game_ui_task, "game_ui", 8192, 4, NULL);
    if (err != ESP_OK) {
        return err;
    }
    s_task_started = true;
    game_ui_port_log_i(TAG, "游戏 UI 已初始化, 贪吃蛇 %ux%u, 迷宫 %ux%u",
                       SNAKE_BOARD_WIDTH, SNAKE_BOARD_HEIGHT,
                       MAZE_WIDTH, MAZE_HEIGHT);
    return ESP_OK;
}

/**
 * @brief 取当前页诊断名, 供 /api/status 使用.
 *
 * @return 静态页名字符串.
 */
const char *game_ui_get_page_name(void)
{
    return game_ui_kind_page_name();
}

/**
 * @brief 取迷宫逻辑只读快照.
 *
 * @return 迷宫状态指针.
 */
const maze_state_t *game_ui_get_maze_state(void)
{
    return maze_ui_state();
}

/**
 * @brief 取贪吃蛇逻辑只读快照.
 *
 * @return 贪吃蛇状态指针.
 */
const snake_state_t *game_ui_get_state(void)
{
    return snake_ui_state();
}

/**
 * @brief 测试辅助: 把食物放到指定格子.
 *
 * @param food 目标坐标, 必须落在空格上.
 * @return 放置成功为 true.
 */
bool game_ui_force_food(snake_point_t food)
{
    return snake_ui_force_food(food);
}

/**
 * @brief 测试辅助: 切到贪吃蛇对局并摆出自撞形状.
 *
 * @return 无.
 */
void game_ui_force_self_collision(void)
{
    s_kind = GAME_UI_KIND_SNAKE;
    snake_ui_force_self_collision();
}

/**
 * @brief 按蛇当前方向把触摸坐标映射成转弯输入.
 *
 * @param x 屏幕 X, 负值按 0 处理.
 * @param y 屏幕 Y, 负值按 0 处理.
 * @return 左右走映射上下, 上下走映射左右.
 */
snake_input_t game_ui_map_touch(int32_t x, int32_t y)
{
    return snake_ui_map_touch(x, y);
}

/**
 * @brief 打开或关闭贪吃蛇触摸转向.
 *
 * @param enabled true 允许点棋盘转向.
 * @return 无.
 */
void game_ui_set_touch_control(bool enabled)
{
    snake_ui_set_touch_control(enabled);
}

/**
 * @brief 查询贪吃蛇触摸转向是否开启.
 *
 * @return 开启为 true.
 */
bool game_ui_get_touch_control(void)
{
    return snake_ui_get_touch_control();
}

/**
 * @brief 取蛇身扭动相位, 供仿真观测.
 *
 * @return 当前周期内的毫秒相位, 范围 0 到周期-1.
 */
uint32_t game_ui_get_wiggle_phase_ms(void)
{
    return snake_ui_get_wiggle_phase_ms();
}

/**
 * @brief 取某一节最近一次算出的扭动偏移.
 *
 * @param segment_index 蛇节下标, 0 是头.
 * @return 像素偏移; 越界返回 0.
 */
int16_t game_ui_get_wiggle_offset(uint16_t segment_index)
{
    return snake_ui_get_wiggle_offset(segment_index);
}

/**
 * @brief 取上一帧实际改过的蛇节对象数.
 *
 * @return 更新过的 LVGL 对象个数.
 */
uint16_t game_ui_get_wiggle_updated_objects(void)
{
    return snake_ui_get_wiggle_updated_objects();
}
