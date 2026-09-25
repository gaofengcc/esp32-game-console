/**
 * @file lvgl_port.c
 * @brief LVGL platform porting for ESP32-S3 + ILI9488 + XPT2046
 *
 * Display: 480x320 RGB565 via lcd_driver_draw_pixels()
 * Touch:   XPT2046 via touch_driver_scan() / touch_driver_get_point()
 * Tick:    esp_timer at 1ms period -> lv_tick_inc(1)
 * Handler: FreeRTOS task calling lv_task_handler() periodically
 */

#include "lvgl_port.h"
#include "lvgl.h"
#include "lcd_driver.h"
#include "touch_driver.h"
#include "board_lcd_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "lvgl_port";

/* Display dimensions */
#define DISP_H_RES  480
#define DISP_V_RES  320

/* Draw buffer(s) - 20 lines of framebuffer for better throughput
 * Note: LVGL buffers don't need DMA_ATTR as pixel data is copied
 * through an intermediate DMA buffer in lcd_driver_draw_pixels() */
#define BUF_ROWS    20
static lv_color_t buf1[DISP_H_RES * BUF_ROWS];
static lv_color_t buf2[DISP_H_RES * BUF_ROWS];  /* Double buffer */

static lv_display_t *disp = NULL;
static lv_indev_t  *indev = NULL;
static QueueHandle_t s_work_queue = NULL;
static TaskHandle_t s_lvgl_task_handle = NULL;
static bool s_touch_available = false;

/* Deferred UI init callback + completion semaphore */
static void (*s_deferred_ui_init)(void) = NULL;
static SemaphoreHandle_t s_ui_done_sem = NULL;

typedef struct {
    lvgl_port_work_cb_t cb;
    void *user_data;
    SemaphoreHandle_t done;
} lvgl_port_work_item_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t reserved;
    float ax;
    float bx;
    float cx;
    float ay;
    float by;
    float cy;
} lvgl_touch_cal_blob_t;

typedef struct {
    uint16_t raw_x;
    uint16_t raw_y;
    int16_t screen_x;
    int16_t screen_y;
} lvgl_touch_cal_point_t;

#define TOUCH_CAL_MAGIC       0x5443414cU  /* TCAL */
#define TOUCH_CAL_VERSION     1
#define TOUCH_CAL_NVS_NS      "lvgl_port"
#define TOUCH_CAL_NVS_KEY     "touch_aff"
#define TOUCH_CAL_POINT_COUNT 4
#define TOUCH_CAL_MARGIN      28
#define TOUCH_CAL_MIN_SAMPLES 3
#define TOUCH_RAW_AVG_SAMPLES 3

static lvgl_touch_cal_blob_t s_touch_cal = {0};
static bool s_touch_cal_valid = false;
static bool s_touch_cal_auto_pending = false;
static bool s_touch_cal_active = false;
static bool s_touch_cal_was_pressed = false;
static uint8_t s_touch_cal_index = 0;
static uint8_t s_touch_cal_sample_count = 0;
static uint32_t s_touch_cal_sum_x = 0;
static uint32_t s_touch_cal_sum_y = 0;
static lvgl_touch_cal_point_t s_touch_cal_points[TOUCH_CAL_POINT_COUNT];
static lv_obj_t *s_touch_cal_overlay = NULL;
static lv_obj_t *s_touch_cal_target = NULL;
static lv_obj_t *s_touch_cal_label = NULL;

static const lv_point_t s_touch_cal_targets[TOUCH_CAL_POINT_COUNT] = {
    {TOUCH_CAL_MARGIN, TOUCH_CAL_MARGIN},
    {DISP_H_RES - TOUCH_CAL_MARGIN - 1, TOUCH_CAL_MARGIN},
    {DISP_H_RES - TOUCH_CAL_MARGIN - 1, DISP_V_RES - TOUCH_CAL_MARGIN - 1},
    {TOUCH_CAL_MARGIN, DISP_V_RES - TOUCH_CAL_MARGIN - 1},
};

/* Forward declarations */
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data);
static void lv_tick_timer_cb(void *arg);
static void lv_task_handler_task(void *arg);
static void lvgl_port_process_deferred_ui_init(void);
static void lvgl_port_process_work_queue(void);
static void touch_calibration_request_cb(void *user_data);
static esp_err_t touch_calibration_start_internal(bool automatic);
static void touch_calibration_read_cb(lv_indev_data_t *data);
static esp_err_t touch_calibration_load(void);
static esp_err_t touch_calibration_save(void);
static void touch_calibration_show_point(void);
static void touch_calibration_finish(void);
static bool touch_calibration_solve(void);
static bool touch_solve_3x3(float a[3][3], float b[3], float out[3]);
static bool touch_read_raw_average(uint16_t *raw_x, uint16_t *raw_y);
static void touch_apply_calibration(uint16_t raw_x, uint16_t raw_y, int16_t *screen_x, int16_t *screen_y);

/* ----------------------------------------------------------- */

esp_err_t lvgl_port_init(void)
{
    /* 1. Init LCD driver */
    lcd_driver_config_t lcd_cfg = {
        .pin_led   = BOARD_LCD_PIN_LED,
        .pin_dc    = BOARD_LCD_PIN_DC,
        .pin_rst   = BOARD_LCD_PIN_RST,
        .pin_cs    = BOARD_LCD_PIN_CS,
        .spi_sck   = BOARD_LCD_SPI_SCK,
        .spi_mosi  = BOARD_LCD_SPI_MOSI,
        .spi_miso  = BOARD_LCD_SPI_MISO,
        .spi_freq  = BOARD_LCD_SPI_FREQ_HZ,
    };
    esp_err_t ret = lcd_driver_init(&lcd_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD driver init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. Init touch driver */
    touch_driver_config_t touch_cfg = {
        .pin_irq  = BOARD_TOUCH_PIN_IRQ,
        .pin_cs   = BOARD_TOUCH_PIN_CS,
        .pin_clk  = BOARD_TOUCH_PIN_CLK,
        .pin_din  = BOARD_TOUCH_PIN_DIN,
        .pin_do   = BOARD_TOUCH_PIN_DO,
    };
    ret = touch_driver_init(&touch_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch driver init failed: %s (continuing without touch)", esp_err_to_name(ret));
    } else {
        s_touch_available = true;
        if (touch_calibration_load() == ESP_OK) {
            ESP_LOGI(TAG, "Touch affine calibration loaded");
        } else {
            s_touch_cal_auto_pending = true;
            ESP_LOGW(TAG, "No touch affine calibration found, calibration will start after UI init");
        }
    }

    /* 3. Init LVGL core (memory pool, timers, etc.) */
    lv_init();

    /* 4. Create LVGL display */
    disp = lv_display_create(DISP_H_RES, DISP_V_RES);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return ESP_FAIL;
    }

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    s_work_queue = xQueueCreate(4, sizeof(lvgl_port_work_item_t *));
    if (s_work_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL work queue");
        return ESP_ERR_NO_MEM;
    }

    /* 5. Register touch input device */
    if (ret == ESP_OK) {
        indev = lv_indev_create();
        if (indev == NULL) {
            ESP_LOGE(TAG, "Failed to create LVGL input device");
            return ESP_FAIL;
        }
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_display(indev, disp);
    }

    /* 6. Start LVGL tick timer (1ms) */
    const esp_timer_create_args_t tick_args = {
        .callback = lv_tick_timer_cb,
        .name     = "lv_tick",
    };
    esp_timer_handle_t tick_timer;
    esp_timer_create(&tick_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, 1000); /* 1ms = 1000us */

    /* 7. Start LVGL task handler task
     * Priority: tskIDLE_PRIORITY + 2 — high enough for smooth UI,
     * but low enough to let IDLE task run and feed the watchdog */
    xTaskCreatePinnedToCore(lv_task_handler_task, "lv_task", 10 * 1024, NULL,
                            tskIDLE_PRIORITY + 2, NULL, 1);

    ESP_LOGI(TAG, "LVGL port initialized: %dx%d RGB565, touch=%s",
             DISP_H_RES, DISP_V_RES, indev ? "yes" : "no");

    return ESP_OK;
}

void *lvgl_port_get_display(void)
{
    return disp;
}

void *lvgl_port_get_indev(void)
{
    return indev;
}

esp_err_t lvgl_port_call(lvgl_port_work_cb_t cb, void *user_data)
{
    if (cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xTaskGetCurrentTaskHandle() == s_lvgl_task_handle) {
        cb(user_data);
        return ESP_OK;
    }

    if (s_work_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    lvgl_port_work_item_t item = {
        .cb = cb,
        .user_data = user_data,
        .done = done,
    };
    lvgl_port_work_item_t *item_ptr = &item;

    if (xQueueSend(s_work_queue, &item_ptr, pdMS_TO_TICKS(1000)) != pdTRUE) {
        vSemaphoreDelete(done);
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    return ESP_OK;
}

esp_err_t lvgl_port_request_touch_calibration(void)
{
    if (!s_touch_available) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xTaskGetCurrentTaskHandle() == s_lvgl_task_handle) {
        return touch_calibration_start_internal(false);
    }

    esp_err_t result = ESP_OK;
    esp_err_t err = lvgl_port_call(touch_calibration_request_cb, &result);
    return err == ESP_OK ? result : err;
}

bool lvgl_port_touch_calibration_valid(void)
{
    return s_touch_cal_valid;
}

static void touch_calibration_request_cb(void *user_data)
{
    esp_err_t err = touch_calibration_start_internal(false);
    if (user_data != NULL) {
        *(esp_err_t *)user_data = err;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch calibration request failed: %s", esp_err_to_name(err));
    }
}

/* ===========================================================
 * Display flush callback
 * =========================================================== */
static void disp_flush_cb(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    /* Send pixel data (lcd_driver_draw_pixels sets window internally) */
    lcd_driver_draw_pixels(area->x1, area->y1, w, h, (const uint16_t *)px_map);

    lv_display_flush_ready(disp_drv);

    /* Yield 1 tick to let IDLE task run on this core, preventing WDT timeout.
     * This is critical during initial full-screen draw which takes multiple flushes. */
    vTaskDelay(pdMS_TO_TICKS(1));
}

/* ===========================================================
 * Touch input read callback
 * =========================================================== */
static void touch_read_cb(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    static int16_t last_x = 0;
    static int16_t last_y = 0;
    static bool s_logged_cal = false;

    (void)indev_drv;

    if (s_touch_cal_active) {
        touch_calibration_read_cb(data);
        return;
    }

    if (!s_touch_available) {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (!s_touch_cal_valid) {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    /* One-time: log calibration status for debugging */
    if (!s_logged_cal) {
        ESP_LOGI(TAG, "Touch affine cal: ax=%.6f bx=%.6f cx=%.2f ay=%.6f by=%.6f cy=%.2f",
                 s_touch_cal.ax, s_touch_cal.bx, s_touch_cal.cx,
                 s_touch_cal.ay, s_touch_cal.by, s_touch_cal.cy);
        s_logged_cal = true;
    }

    if (touch_driver_is_touched()) {
        uint16_t raw_x = 0;
        uint16_t raw_y = 0;
        int16_t screen_x = last_x;
        int16_t screen_y = last_y;

        if (touch_read_raw_average(&raw_x, &raw_y)) {
            touch_apply_calibration(raw_x, raw_y, &screen_x, &screen_y);
            ESP_LOGD(TAG, "Touch PRESSED: (%d, %d) raw=(%u,%u)",
                     screen_x, screen_y, raw_x, raw_y);
            data->point.x = screen_x;
            data->point.y = screen_y;
            data->state = LV_INDEV_STATE_PRESSED;
            last_x = screen_x;
            last_y = screen_y;
            return;
        }
    }

    data->point.x = last_x;
    data->point.y = last_y;
    data->state = LV_INDEV_STATE_RELEASED;
}

static esp_err_t touch_calibration_start_internal(bool automatic)
{
    if (!s_touch_available) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_touch_cal_overlay != NULL) {
        lv_obj_delete(s_touch_cal_overlay);
        s_touch_cal_overlay = NULL;
    }

    memset(s_touch_cal_points, 0, sizeof(s_touch_cal_points));
    s_touch_cal_active = true;
    s_touch_cal_was_pressed = false;
    s_touch_cal_index = 0;
    s_touch_cal_sample_count = 0;
    s_touch_cal_sum_x = 0;
    s_touch_cal_sum_y = 0;

    s_touch_cal_overlay = lv_obj_create(lv_screen_active());
    if (s_touch_cal_overlay == NULL) {
        s_touch_cal_active = false;
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(s_touch_cal_overlay, DISP_H_RES, DISP_V_RES);
    lv_obj_set_pos(s_touch_cal_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_touch_cal_overlay, lv_color_hex(0x11111B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_touch_cal_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_touch_cal_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_touch_cal_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_touch_cal_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_touch_cal_overlay, LV_OBJ_FLAG_CLICKABLE);

    s_touch_cal_label = lv_label_create(s_touch_cal_overlay);
    lv_obj_set_style_text_color(s_touch_cal_label, lv_color_hex(0xCDD6F4), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_touch_cal_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_touch_cal_label, FONT_CJK, LV_PART_MAIN);
    lv_label_set_text(s_touch_cal_label, "触摸校准");
    lv_obj_align(s_touch_cal_label, LV_ALIGN_CENTER, 0, 0);

    s_touch_cal_target = lv_obj_create(s_touch_cal_overlay);
    lv_obj_set_size(s_touch_cal_target, 34, 34);
    lv_obj_set_style_bg_opa(s_touch_cal_target, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_touch_cal_target, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_touch_cal_target, lv_color_hex(0x89B4FA), LV_PART_MAIN);
    lv_obj_set_style_radius(s_touch_cal_target, 17, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_touch_cal_target, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_touch_cal_target, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(s_touch_cal_target);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xA6E3A1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dot, 4, LV_PART_MAIN);
    lv_obj_center(dot);

    touch_calibration_show_point();
    ESP_LOGI(TAG, "Touch calibration started (%s)", automatic ? "auto" : "manual");
    return ESP_OK;
}

static void touch_calibration_show_point(void)
{
    if (s_touch_cal_label == NULL || s_touch_cal_target == NULL ||
        s_touch_cal_index >= TOUCH_CAL_POINT_COUNT) {
        return;
    }

    const lv_point_t *target = &s_touch_cal_targets[s_touch_cal_index];
    lv_label_set_text_fmt(s_touch_cal_label,
                          "触摸校准\n请点击第 %u/%u 个标记点",
                          (unsigned)(s_touch_cal_index + 1),
                          (unsigned)TOUCH_CAL_POINT_COUNT);
    lv_obj_align(s_touch_cal_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_pos(s_touch_cal_target, target->x - 17, target->y - 17);
    lv_obj_move_to_index(s_touch_cal_target, -1);
}

static void touch_calibration_read_cb(lv_indev_data_t *data)
{
    data->point.x = 0;
    data->point.y = 0;
    data->state = LV_INDEV_STATE_RELEASED;

    if (!touch_driver_is_touched()) {
        if (s_touch_cal_was_pressed) {
            if (s_touch_cal_sample_count >= TOUCH_CAL_MIN_SAMPLES &&
                s_touch_cal_index < TOUCH_CAL_POINT_COUNT) {
                lvgl_touch_cal_point_t *point = &s_touch_cal_points[s_touch_cal_index];
                const lv_point_t *target = &s_touch_cal_targets[s_touch_cal_index];
                point->raw_x = (uint16_t)(s_touch_cal_sum_x / s_touch_cal_sample_count);
                point->raw_y = (uint16_t)(s_touch_cal_sum_y / s_touch_cal_sample_count);
                point->screen_x = (int16_t)target->x;
                point->screen_y = (int16_t)target->y;
                ESP_LOGI(TAG, "Touch cal point %u/%u: raw=(%u,%u) screen=(%d,%d)",
                         (unsigned)(s_touch_cal_index + 1),
                         (unsigned)TOUCH_CAL_POINT_COUNT,
                         point->raw_x, point->raw_y,
                         point->screen_x, point->screen_y);

                s_touch_cal_index++;
                s_touch_cal_sample_count = 0;
                s_touch_cal_sum_x = 0;
                s_touch_cal_sum_y = 0;
                s_touch_cal_was_pressed = false;

                if (s_touch_cal_index >= TOUCH_CAL_POINT_COUNT) {
                    touch_calibration_finish();
                } else {
                    touch_calibration_show_point();
                }
            } else {
                s_touch_cal_sample_count = 0;
                s_touch_cal_sum_x = 0;
                s_touch_cal_sum_y = 0;
                s_touch_cal_was_pressed = false;
            }
        }
        return;
    }

    uint16_t raw_x = 0;
    uint16_t raw_y = 0;
    if (touch_read_raw_average(&raw_x, &raw_y)) {
        s_touch_cal_was_pressed = true;
        if (s_touch_cal_sample_count < 32) {
            s_touch_cal_sum_x += raw_x;
            s_touch_cal_sum_y += raw_y;
            s_touch_cal_sample_count++;
        }
    }
}

static void touch_calibration_finish(void)
{
    if (!touch_calibration_solve()) {
        ESP_LOGW(TAG, "Touch calibration solve failed, restarting");
        s_touch_cal_index = 0;
        s_touch_cal_sample_count = 0;
        s_touch_cal_sum_x = 0;
        s_touch_cal_sum_y = 0;
        s_touch_cal_was_pressed = false;
        touch_calibration_show_point();
        return;
    }

    s_touch_cal_valid = true;
    esp_err_t err = touch_calibration_save();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch calibration save failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Touch calibration done: ax=%.6f bx=%.6f cx=%.2f ay=%.6f by=%.6f cy=%.2f",
             s_touch_cal.ax, s_touch_cal.bx, s_touch_cal.cx,
             s_touch_cal.ay, s_touch_cal.by, s_touch_cal.cy);

    if (s_touch_cal_overlay != NULL) {
        lv_obj_delete(s_touch_cal_overlay);
    }
    s_touch_cal_overlay = NULL;
    s_touch_cal_target = NULL;
    s_touch_cal_label = NULL;
    s_touch_cal_active = false;
    s_touch_cal_was_pressed = false;
}

static esp_err_t touch_calibration_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(TOUCH_CAL_NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    lvgl_touch_cal_blob_t blob;
    size_t len = sizeof(blob);
    err = nvs_get_blob(handle, TOUCH_CAL_NVS_KEY, &blob, &len);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    if (len != sizeof(blob) ||
        blob.magic != TOUCH_CAL_MAGIC ||
        blob.version != TOUCH_CAL_VERSION ||
        blob.width != DISP_H_RES ||
        blob.height != DISP_V_RES ||
        !isfinite(blob.ax) || !isfinite(blob.bx) || !isfinite(blob.cx) ||
        !isfinite(blob.ay) || !isfinite(blob.by) || !isfinite(blob.cy)) {
        return ESP_ERR_INVALID_CRC;
    }

    s_touch_cal = blob;
    s_touch_cal_valid = true;
    return ESP_OK;
}

static esp_err_t touch_calibration_save(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(TOUCH_CAL_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, TOUCH_CAL_NVS_KEY, &s_touch_cal, sizeof(s_touch_cal));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static bool touch_calibration_solve(void)
{
    float normal[3][3] = {{0}};
    float vx[3] = {0};
    float vy[3] = {0};
    float coeff_x[3] = {0};
    float coeff_y[3] = {0};

    for (int i = 0; i < TOUCH_CAL_POINT_COUNT; i++) {
        float raw[3] = {
            (float)s_touch_cal_points[i].raw_x,
            (float)s_touch_cal_points[i].raw_y,
            1.0f,
        };
        float sx = (float)s_touch_cal_points[i].screen_x;
        float sy = (float)s_touch_cal_points[i].screen_y;

        for (int r = 0; r < 3; r++) {
            vx[r] += raw[r] * sx;
            vy[r] += raw[r] * sy;
            for (int c = 0; c < 3; c++) {
                normal[r][c] += raw[r] * raw[c];
            }
        }
    }

    float normal_x[3][3];
    float normal_y[3][3];
    memcpy(normal_x, normal, sizeof(normal));
    memcpy(normal_y, normal, sizeof(normal));
    if (!touch_solve_3x3(normal_x, vx, coeff_x) ||
        !touch_solve_3x3(normal_y, vy, coeff_y)) {
        return false;
    }

    s_touch_cal.magic = TOUCH_CAL_MAGIC;
    s_touch_cal.version = TOUCH_CAL_VERSION;
    s_touch_cal.width = DISP_H_RES;
    s_touch_cal.height = DISP_V_RES;
    s_touch_cal.reserved = 0;
    s_touch_cal.ax = coeff_x[0];
    s_touch_cal.bx = coeff_x[1];
    s_touch_cal.cx = coeff_x[2];
    s_touch_cal.ay = coeff_y[0];
    s_touch_cal.by = coeff_y[1];
    s_touch_cal.cy = coeff_y[2];

    if (!isfinite(s_touch_cal.ax) || !isfinite(s_touch_cal.bx) || !isfinite(s_touch_cal.cx) ||
        !isfinite(s_touch_cal.ay) || !isfinite(s_touch_cal.by) || !isfinite(s_touch_cal.cy)) {
        return false;
    }

    float max_err = 0.0f;
    for (int i = 0; i < TOUCH_CAL_POINT_COUNT; i++) {
        int16_t x = 0;
        int16_t y = 0;
        touch_apply_calibration(s_touch_cal_points[i].raw_x, s_touch_cal_points[i].raw_y, &x, &y);
        float ex = fabsf((float)x - (float)s_touch_cal_points[i].screen_x);
        float ey = fabsf((float)y - (float)s_touch_cal_points[i].screen_y);
        if (ex > max_err) max_err = ex;
        if (ey > max_err) max_err = ey;
    }
    ESP_LOGI(TAG, "Touch calibration max residual %.1f px", max_err);
    return max_err <= 80.0f;
}

static bool touch_solve_3x3(float a[3][3], float b[3], float out[3])
{
    for (int col = 0; col < 3; col++) {
        int pivot = col;
        float max_abs = fabsf(a[col][col]);
        for (int row = col + 1; row < 3; row++) {
            float v = fabsf(a[row][col]);
            if (v > max_abs) {
                max_abs = v;
                pivot = row;
            }
        }

        if (max_abs < 1.0e-6f) {
            return false;
        }

        if (pivot != col) {
            for (int k = col; k < 3; k++) {
                float tmp = a[col][k];
                a[col][k] = a[pivot][k];
                a[pivot][k] = tmp;
            }
            float tb = b[col];
            b[col] = b[pivot];
            b[pivot] = tb;
        }

        float div = a[col][col];
        for (int k = col; k < 3; k++) {
            a[col][k] /= div;
        }
        b[col] /= div;

        for (int row = 0; row < 3; row++) {
            if (row == col) continue;
            float factor = a[row][col];
            for (int k = col; k < 3; k++) {
                a[row][k] -= factor * a[col][k];
            }
            b[row] -= factor * b[col];
        }
    }

    out[0] = b[0];
    out[1] = b[1];
    out[2] = b[2];
    return true;
}

static bool touch_read_raw_average(uint16_t *raw_x, uint16_t *raw_y)
{
    uint32_t sx = 0;
    uint32_t sy = 0;
    uint8_t count = 0;

    for (int i = 0; i < TOUCH_RAW_AVG_SAMPLES; i++) {
        uint16_t x = 0;
        uint16_t y = 0;
        if (touch_driver_get_raw(&x, &y) == ESP_OK) {
            sx += x;
            sy += y;
            count++;
        }
    }

    if (count == 0) {
        return false;
    }

    *raw_x = (uint16_t)(sx / count);
    *raw_y = (uint16_t)(sy / count);
    return true;
}

static void touch_apply_calibration(uint16_t raw_x, uint16_t raw_y, int16_t *screen_x, int16_t *screen_y)
{
    float xf = s_touch_cal.ax * (float)raw_x + s_touch_cal.bx * (float)raw_y + s_touch_cal.cx;
    float yf = s_touch_cal.ay * (float)raw_x + s_touch_cal.by * (float)raw_y + s_touch_cal.cy;
    int32_t x = (int32_t)(xf >= 0.0f ? xf + 0.5f : xf - 0.5f);
    int32_t y = (int32_t)(yf >= 0.0f ? yf + 0.5f : yf - 0.5f);

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= DISP_H_RES) x = DISP_H_RES - 1;
    if (y >= DISP_V_RES) y = DISP_V_RES - 1;

    *screen_x = (int16_t)x;
    *screen_y = (int16_t)y;
}

/* ===========================================================
 * LVGL tick timer (1ms)
 * =========================================================== */
static void lv_tick_timer_cb(void *arg)
{
    lv_tick_inc(1);
}

/* ===========================================================
 * LVGL task handler thread
 * =========================================================== */
static void lv_task_handler_task(void *arg)
{
    s_lvgl_task_handle = xTaskGetCurrentTaskHandle();

    /* Run one handler cycle to initialize LVGL internals */
    lv_task_handler();
    vTaskDelay(pdMS_TO_TICKS(1));

    lvgl_port_process_deferred_ui_init();

    while (1) {
        lvgl_port_process_deferred_ui_init();
        lvgl_port_process_work_queue();
        lv_task_handler();
        lvgl_port_process_work_queue();
        /* Yield to IDLE task to prevent watchdog timeout */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void lvgl_port_process_deferred_ui_init(void)
{
    if (s_deferred_ui_init == NULL) {
        return;
    }

    void (*init_cb)(void) = s_deferred_ui_init;
    s_deferred_ui_init = NULL;
    init_cb();

    if (s_touch_cal_auto_pending && s_touch_available && !s_touch_cal_valid) {
        s_touch_cal_auto_pending = false;
        esp_err_t err = touch_calibration_start_internal(true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Auto touch calibration start failed: %s", esp_err_to_name(err));
        }
    }

    if (s_ui_done_sem != NULL) {
        xSemaphoreGive(s_ui_done_sem);
    }
}

static void lvgl_port_process_work_queue(void)
{
    if (s_work_queue == NULL) {
        return;
    }

    lvgl_port_work_item_t *item = NULL;
    while (xQueueReceive(s_work_queue, &item, 0) == pdTRUE) {
        if (item && item->cb) {
            item->cb(item->user_data);
        }
        if (item && item->done) {
            xSemaphoreGive(item->done);
        }
    }
}

/* ===========================================================
 * Deferred main screen creation (safe cross-task scheduling)
 * =========================================================== */
esp_err_t lvgl_port_deferred_create_main_screen(void)
{
    if (s_deferred_ui_init != NULL) {
        ESP_LOGW(TAG, "UI init already scheduled");
        return ESP_FAIL;
    }

    s_ui_done_sem = xSemaphoreCreateBinary();
    if (s_ui_done_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_deferred_ui_init = nas_ui_create_dashboard;

    /* Wait for the LVGL handler task to complete the init (max 30s) */
    if (xSemaphoreTake(s_ui_done_sem, pdMS_TO_TICKS(30000)) != pdTRUE) {
        ESP_LOGE(TAG, "UI init timeout (30s)");
        s_deferred_ui_init = NULL;
        vSemaphoreDelete(s_ui_done_sem);
        s_ui_done_sem = NULL;
        return ESP_ERR_TIMEOUT;
    }

    vSemaphoreDelete(s_ui_done_sem);
    s_ui_done_sem = NULL;
    return ESP_OK;
}
