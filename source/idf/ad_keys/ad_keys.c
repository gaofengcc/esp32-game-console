#include "ad_keys.h"

#include <math.h>
#include <string.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define AD_KEYS_TAG "ad_keys"
#define AD_KEYS_ADC_UNIT ADC_UNIT_1
#define AD_KEYS_ADC_CHANNEL ADC_CHANNEL_0
#define AD_KEYS_SAMPLE_BUF 7
#define AD_KEYS_NVS_VERSION 1U
#define AD_KEYS_NVS_KEY "calibration"
/* 5ms 采样, 3 点中值, 2 次确认. 防抖大约 10ms, 界面仍 20ms 取键. */
#define AD_KEYS_DEFAULT_PERIOD_MS 5
/* 按键任务钉在核 0, 避免优先级 5 抢 LVGL 所在的核 1. */
#define AD_KEYS_TASK_CORE 0
#define AD_KEYS_DEFAULT_MEDIAN_WINDOW 3
#define AD_KEYS_DEFAULT_STABLE 2
#define AD_KEYS_DEFAULT_DEBOUNCE_MS 5
/* 稳定后还要按住这么久才发 PRESS, 松手回弹 (如 17ms) 不产生事件. */
#define AD_KEYS_MIN_PRESS_MS 50U
#define AD_KEYS_DEFAULT_REPEAT_DELAY_MS 200
#define AD_KEYS_DEFAULT_REPEAT_MS 50
#define AD_KEYS_DEFAULT_CALIBRATION_MIN_DELTA_MV 160
/*
 * 运行时按键采样已关闭: 用户看不到提示, 不知道该按哪一颗.
 * 识别继续使用 NVS 合法窗口或出厂实测值. 后续由专用页面再打开.
 */
#ifndef AD_KEYS_ENABLE_RUNTIME_SAMPLING
#define AD_KEYS_ENABLE_RUNTIME_SAMPLING 0
#endif
#ifndef AD_KEYS_RELEASE_HYSTERESIS_MV
#define AD_KEYS_RELEASE_HYSTERESIS_MV 100U
#endif
#define AD_KEYS_FACTORY_IDLE_MV 3157U
/* 任意两键中心间距下限, 小于该值视为编号错位/重复采样的脏 NVS. */
#define AD_KEYS_MIN_CENTER_SPACING_MV 200U

static const uint16_t AD_KEYS_FACTORY_CENTER_MV[AD_KEYS_COUNT] = {
    0U,    /* K1 左, 0mV 是本机合法键值, 不能当无效电压丢掉 */
    460U,  /* K2 上 */
    980U,  /* K3 下 */
    1565U, /* K4 右 */
    2397U, /* K5 确定 */
};
#ifndef AD_KEYS_LOG_PERIOD_MS
/* 普通 ADC 快照的最小间隔，避免 5ms 采样任务刷满串口。 */
#define AD_KEYS_LOG_PERIOD_MS 250U
#endif
#ifndef AD_KEYS_LOG_DELTA_MV
/* 电压变化达到该幅度才提前触发一次快照。 */
#define AD_KEYS_LOG_DELTA_MV 100U
#endif
#ifndef AD_KEYS_LOG_HEARTBEAT_MS
/* 长时间无按键变化时仍保留一条低频资源/状态心跳。 */
#define AD_KEYS_LOG_HEARTBEAT_MS 30000U
#endif

typedef struct {
    /* NVS 中保存完整窗口，启动时会重新计算窗口并重新建立空闲高水位。 */
    uint32_t version;
    uint16_t min_mv[AD_KEYS_COUNT];
    uint16_t max_mv[AD_KEYS_COUNT];
    uint16_t center_mv[AD_KEYS_COUNT];
    uint16_t idle_mv;
} ad_keys_nvs_blob_t;

typedef struct {
    /* 硬件和任务资源。ADC 句柄只由采样任务使用，lock 保护公开快照/配置。 */
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t cali_handle;
    bool cali_enabled;
    TaskHandle_t task;
    SemaphoreHandle_t lock;
    ad_keys_config_t cfg;

    /* 运行/标定状态。calibration_index 表示下一个待采集的键号。 */
    bool running;
    bool calibration_valid;
    bool calibrating;
    bool calibration_wait_release;
    uint8_t calibration_index;
    uint8_t calibration_warned_mask;
    uint16_t centers_mv[AD_KEYS_COUNT];
    uint16_t min_mv[AD_KEYS_COUNT];
    uint16_t max_mv[AD_KEYS_COUNT];
    uint16_t idle_mv;

    /* 中值滤波环形缓冲和启动时的空闲电压高水位。 */
    uint16_t history[AD_KEYS_SAMPLE_BUF];
    uint8_t history_count;
    uint8_t history_pos;
    uint32_t idle_accum;
    uint8_t idle_samples;
    bool idle_ready;

    /* 当前候选键与连发计时。 */
    uint8_t candidate_key;
    uint8_t candidate_count;
    uint32_t candidate_since_ms;
    uint16_t calibration_samples[AD_KEYS_SAMPLE_BUF];
    uint8_t calibration_sample_count;
    uint8_t current_key;
    /* 已稳定按下, 未满最短按住时间, 期间不发 PRESS. */
    uint8_t armed_key;
    uint32_t press_start_ms;
    uint32_t next_repeat_ms;

    /* 诊断快照；日志字段与最近一次 ADC 采样分开保存。 */
    uint16_t last_voltage_mv;
    uint16_t last_raw_voltage_mv;
    uint32_t last_log_ms;
    uint16_t last_logged_voltage_mv;
    uint8_t last_logged_key;
    uint32_t last_heartbeat_ms;
    bool log_has_previous;
    bool log_event_pending;
    bool boot_force_pending;
    uint32_t boot_force_deadline_ms;
} ad_keys_ctx_t;

static ad_keys_ctx_t s_ctx;

static void calculate_windows_locked(void);

static uint16_t calibration_min_delta_mv(void)
{
    uint16_t configured = s_ctx.cfg.calibration_idle_delta_mv;
    return configured >= AD_KEYS_DEFAULT_CALIBRATION_MIN_DELTA_MV
               ? configured
               : AD_KEYS_DEFAULT_CALIBRATION_MIN_DELTA_MV;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static uint16_t abs_diff_u16(uint16_t a, uint16_t b)
{
    return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint16_t median_u16(const uint16_t *values, size_t count)
{
    uint16_t tmp[AD_KEYS_SAMPLE_BUF];
    if (count > AD_KEYS_SAMPLE_BUF) {
        count = AD_KEYS_SAMPLE_BUF;
    }
    memcpy(tmp, values, count * sizeof(tmp[0]));
    for (size_t i = 1; i < count; ++i) {
        uint16_t value = tmp[i];
        size_t j = i;
        while (j > 0 && tmp[j - 1] > value) {
            tmp[j] = tmp[j - 1];
            --j;
        }
        tmp[j] = value;
    }
    return count ? tmp[count / 2] : 0;
}

static uint16_t push_and_get_median(uint16_t value)
{
    s_ctx.history[s_ctx.history_pos] = value;
    s_ctx.history_pos = (uint8_t)((s_ctx.history_pos + 1U) % AD_KEYS_SAMPLE_BUF);
    if (s_ctx.history_count < AD_KEYS_SAMPLE_BUF) {
        ++s_ctx.history_count;
    }
    size_t count = s_ctx.history_count;
    if (s_ctx.cfg.median_window == 3 || s_ctx.cfg.median_window == 5 ||
        s_ctx.cfg.median_window == 7) {
        if ((size_t)s_ctx.cfg.median_window < count) {
            count = (size_t)s_ctx.cfg.median_window;
        }
    }
    uint16_t recent[AD_KEYS_SAMPLE_BUF];
    for (size_t i = 0; i < count; ++i) {
        size_t index = (s_ctx.history_pos + AD_KEYS_SAMPLE_BUF - 1U - i) % AD_KEYS_SAMPLE_BUF;
        recent[i] = s_ctx.history[index];
    }
    return median_u16(recent, count);
}

static void emit_event(ad_keys_event_type_t type, uint8_t key, uint16_t voltage, uint32_t held)
{
    ad_keys_event_cb_t cb = s_ctx.cfg.event_cb;
    if (key == 0 || key > AD_KEYS_COUNT) {
        return;
    }
    /* 连发事件只保留 DEBUG，不触发 ADC INFO 快照；按下/释放仍立即纳入诊断。 */
    if (type != AD_KEYS_EVENT_REPEAT) {
        s_ctx.log_event_pending = true;
    }
    ad_keys_event_t event = {
        .key = key,
        .type = type,
        .voltage_mv = voltage,
        .held_ms = held,
    };
    /* 连发周期可能只有 50ms，REPEAT 降为 DEBUG，避免正常长按刷屏。 */
    if (type == AD_KEYS_EVENT_REPEAT) {
        ESP_LOGD(AD_KEYS_TAG, "事件 K%u: REPEAT, %umV, held=%ums", key,
                 voltage, (unsigned)held);
    } else {
        ESP_LOGI(AD_KEYS_TAG, "事件 K%u: %s, %umV, held=%ums", key,
                 type == AD_KEYS_EVENT_PRESS ? "PRESS" :
                 type == AD_KEYS_EVENT_LONG ? "LONG" : "RELEASE",
                 voltage, (unsigned)held);
    }
    if (cb) {
        cb(&event, s_ctx.cfg.event_user_ctx);
    }
}

/**
 * @brief 按窗口匹配键号, 重叠时取离中心更近的键.
 *
 * voltage_mv 为 0 是合法输入 (出厂 K1 中心就是 0mV), 不得提前当空闲丢掉.
 */
static uint8_t classify_voltage(uint16_t voltage_mv)
{
    uint8_t key = 0;
    uint16_t best_distance = UINT16_MAX;
    for (uint8_t i = 0; i < AD_KEYS_COUNT; ++i) {
        if (voltage_mv >= s_ctx.min_mv[i] && voltage_mv <= s_ctx.max_mv[i]) {
            uint16_t distance = abs_diff_u16(voltage_mv, s_ctx.centers_mv[i]);
            if (distance < best_distance) {
                best_distance = distance;
                key = (uint8_t)(i + 1U);
            }
        }
    }
    return key;
}

static esp_err_t save_calibration_locked(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("adkeys", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    ad_keys_nvs_blob_t blob = {
        .version = AD_KEYS_NVS_VERSION,
        .idle_mv = s_ctx.idle_mv,
    };
    memcpy(blob.min_mv, s_ctx.min_mv, sizeof(blob.min_mv));
    memcpy(blob.max_mv, s_ctx.max_mv, sizeof(blob.max_mv));
    memcpy(blob.center_mv, s_ctx.centers_mv, sizeof(blob.center_mv));
    err = nvs_set_blob(nvs, AD_KEYS_NVS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static void discard_calibration_blob(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("adkeys", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(AD_KEYS_TAG, "丢弃非法标定数据失败：打开 NVS 失败(%s)", esp_err_to_name(err));
        return;
    }
    err = nvs_erase_key(nvs, AD_KEYS_NVS_KEY);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(AD_KEYS_TAG, "丢弃非法标定数据失败：擦除 NVS 失败(%s)", esp_err_to_name(err));
    }
}

static void load_factory_calibration_locked(void)
{
    memcpy(s_ctx.centers_mv, AD_KEYS_FACTORY_CENTER_MV, sizeof(s_ctx.centers_mv));
    s_ctx.idle_mv = AD_KEYS_FACTORY_IDLE_MV;
    s_ctx.idle_samples = 0;
    s_ctx.idle_ready = true;
    s_ctx.calibration_valid = true;
    calculate_windows_locked();
    ESP_LOGI(AD_KEYS_TAG,
             "使用出厂标定（实测值）：K1=%u K2=%u K3=%u K4=%u K5=%umV，空闲=%umV",
             s_ctx.centers_mv[0], s_ctx.centers_mv[1], s_ctx.centers_mv[2],
             s_ctx.centers_mv[3], s_ctx.centers_mv[4], s_ctx.idle_mv);
}

static esp_err_t load_calibration_locked(void)
{
    s_ctx.calibration_valid = false;
    s_ctx.idle_ready = false;
    s_ctx.idle_mv = 0;
    s_ctx.idle_samples = 0;
    memset(s_ctx.centers_mv, 0, sizeof(s_ctx.centers_mv));
    memset(s_ctx.min_mv, 0, sizeof(s_ctx.min_mv));
    memset(s_ctx.max_mv, 0, sizeof(s_ctx.max_mv));

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("adkeys", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        load_factory_calibration_locked();
        return ESP_OK;
    }
    ad_keys_nvs_blob_t blob = {0};
    size_t size = sizeof(blob);
    err = nvs_get_blob(nvs, AD_KEYS_NVS_KEY, &blob, &size);
    nvs_close(nvs);
    if (err != ESP_OK || size != sizeof(blob) || blob.version != AD_KEYS_NVS_VERSION) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            if (size == sizeof(blob)) {
                ESP_LOGW(AD_KEYS_TAG, "标定数据读取/版本无效(%s, version=%lu)，丢弃并回落到出厂标定",
                         esp_err_to_name(err),
                         (unsigned long)blob.version);
            } else {
                ESP_LOGW(AD_KEYS_TAG, "标定数据读取/长度无效(%s, size=%u)，丢弃并回落到出厂标定",
                         esp_err_to_name(err), (unsigned)size);
            }
            discard_calibration_blob();
        }
        load_factory_calibration_locked();
        return ESP_OK;
    }

    bool valid = blob.idle_mv > 0 && blob.idle_mv <= 3300;
    if (!valid) {
        ESP_LOGW(AD_KEYS_TAG, "标定数据无效：空闲基线 %umV 超出范围，丢弃并回落到出厂标定",
                 blob.idle_mv);
    }
    uint16_t min_delta_mv = calibration_min_delta_mv();
    for (uint8_t i = 0; i < AD_KEYS_COUNT; ++i) {
        uint16_t center = blob.center_mv[i];
        uint16_t delta = blob.idle_mv > center ? (uint16_t)(blob.idle_mv - center) : 0;
        /* i==0 且 center==0 合法, 只有 K2..K5 的 0mV 才视为脏数据. */
        if ((i > 0 && center == 0) || center >= blob.idle_mv || delta < min_delta_mv) {
            ESP_LOGW(AD_KEYS_TAG,
                     "标定数据无效：K%u中心%umV接近空闲基线%umV（差%umV<最小%umV），丢弃并回落到出厂标定",
                     i + 1U, center, blob.idle_mv, delta, min_delta_mv);
            valid = false;
        }
    }
    for (uint8_t i = 0; i < AD_KEYS_COUNT && valid; ++i) {
        for (uint8_t j = (uint8_t)(i + 1U); j < AD_KEYS_COUNT; ++j) {
            uint16_t distance = abs_diff_u16(blob.center_mv[i], blob.center_mv[j]);
            if (distance < AD_KEYS_MIN_CENTER_SPACING_MV) {
                ESP_LOGW(AD_KEYS_TAG,
                         "标定数据无效：K%u(%umV) 与 K%u(%umV) 间距 %umV<%umV，丢弃并回落到出厂标定",
                         i + 1U, blob.center_mv[i], j + 1U, blob.center_mv[j],
                         distance, AD_KEYS_MIN_CENTER_SPACING_MV);
                valid = false;
                break;
            }
        }
    }
    if (!valid) {
        discard_calibration_blob();
        load_factory_calibration_locked();
        return ESP_OK;
    }

    memcpy(s_ctx.min_mv, blob.min_mv, sizeof(s_ctx.min_mv));
    memcpy(s_ctx.max_mv, blob.max_mv, sizeof(s_ctx.max_mv));
    memcpy(s_ctx.centers_mv, blob.center_mv, sizeof(s_ctx.centers_mv));
    // NVS 中的基线只用于校验历史标定；启动后仍重新取真实高水位，
    // 避免旧的 3300mV 把当前硬件实际空闲值 3157mV 卡住。
    s_ctx.idle_mv = 0;
    s_ctx.idle_samples = 0;
    s_ctx.idle_ready = false;
    s_ctx.calibration_valid = true;
    calculate_windows_locked();
    ESP_LOGI(AD_KEYS_TAG,
             "加载 NVS 标定：K1=%u K2=%u K3=%u K4=%u K5=%umV，"
             "窗口 K1=%u..%u K2=%u..%u K3=%u..%u K4=%u..%u K5=%u..%u，"
             "空闲基线启动后重新采样",
             s_ctx.centers_mv[0], s_ctx.centers_mv[1], s_ctx.centers_mv[2],
             s_ctx.centers_mv[3], s_ctx.centers_mv[4],
             s_ctx.min_mv[0], s_ctx.max_mv[0], s_ctx.min_mv[1], s_ctx.max_mv[1],
             s_ctx.min_mv[2], s_ctx.max_mv[2], s_ctx.min_mv[3], s_ctx.max_mv[3],
             s_ctx.min_mv[4], s_ctx.max_mv[4]);
    return ESP_OK;
}

static void calculate_windows_locked(void)
{
    uint16_t min_distance = UINT16_MAX;
    for (uint8_t i = 0; i < AD_KEYS_COUNT; ++i) {
        for (uint8_t j = (uint8_t)(i + 1U); j < AD_KEYS_COUNT; ++j) {
            uint16_t distance = abs_diff_u16(s_ctx.centers_mv[i], s_ctx.centers_mv[j]);
            if (distance > 0 && distance < min_distance) {
                min_distance = distance;
            }
        }
    }
    uint16_t tolerance = 200;
    if (min_distance != UINT16_MAX) {
        uint16_t candidate = (uint16_t)((min_distance * 40U) / 100U);
        if (candidate > tolerance) {
            tolerance = candidate;
        }
    }
    for (uint8_t i = 0; i < AD_KEYS_COUNT; ++i) {
        s_ctx.min_mv[i] = s_ctx.centers_mv[i] > tolerance ?
                          (uint16_t)(s_ctx.centers_mv[i] - tolerance) : 0;
        uint32_t max = (uint32_t)s_ctx.centers_mv[i] + tolerance;
        s_ctx.max_mv[i] = max > 3300U ? 3300U : (uint16_t)max;
    }
}

/**
 * @brief 进入运行时键值采样. 当前默认关闭, 避免无提示采集错键.
 *
 * 上下文: ad_keys 任务或持锁的 API 调用方, 不可在 ISR 调用.
 */
static void start_calibration_locked(void)
{
#if AD_KEYS_ENABLE_RUNTIME_SAMPLING
    s_ctx.calibrating = true;
    s_ctx.current_key = 0;
    s_ctx.calibration_wait_release = false;
    s_ctx.calibration_index = 0;
    s_ctx.calibration_warned_mask = 0;
    s_ctx.candidate_key = 0;
    s_ctx.candidate_count = 0;
    s_ctx.candidate_since_ms = 0;
    s_ctx.calibration_sample_count = 0;
    memset(s_ctx.centers_mv, 0, sizeof(s_ctx.centers_mv));
    ESP_LOGI(AD_KEYS_TAG, "进入标定模式，请依次按下 K1..K5");
#else
    ESP_LOGW(AD_KEYS_TAG,
             "运行时键值采样已关闭, 继续使用出厂/NVS 窗口, 等待专用采样页");
#endif
}

static void process_calibration(uint16_t voltage_mv, uint32_t tick_ms)
{
    if (!s_ctx.idle_ready) {
        return;
    }
    uint16_t min_delta_mv = calibration_min_delta_mv();
    bool active = (uint32_t)voltage_mv + min_delta_mv <= s_ctx.idle_mv;
    if (s_ctx.calibration_wait_release) {
        if (!active) {
            s_ctx.calibration_wait_release = false;
            s_ctx.candidate_count = 0;
            s_ctx.calibration_sample_count = 0;
        }
        return;
    }
    if (!active) {
        uint8_t bit = (uint8_t)(1U << s_ctx.calibration_index);
        if ((s_ctx.calibration_warned_mask & bit) == 0) {
            s_ctx.calibration_warned_mask |= bit;
            uint16_t delta = s_ctx.idle_mv > voltage_mv
                                 ? (uint16_t)(s_ctx.idle_mv - voltage_mv)
                                 : 0;
            ESP_LOGW(AD_KEYS_TAG,
                     "标定 K%u 样本无效：电压 %umV 接近空闲基线 %umV（差%umV<最小%umV），请按住按键",
                     s_ctx.calibration_index + 1U, voltage_mv, s_ctx.idle_mv,
                     delta, min_delta_mv);
        }
        s_ctx.candidate_count = 0;
        s_ctx.calibration_sample_count = 0;
        return;
    }
    if (s_ctx.candidate_count == 0 ||
        abs_diff_u16(voltage_mv, s_ctx.centers_mv[s_ctx.calibration_index]) > 100) {
        s_ctx.centers_mv[s_ctx.calibration_index] = voltage_mv;
        s_ctx.candidate_count = 1;
        s_ctx.candidate_since_ms = tick_ms;
        s_ctx.calibration_sample_count = 0;
    } else {
        s_ctx.centers_mv[s_ctx.calibration_index] = voltage_mv;
        ++s_ctx.candidate_count;
    }
    if (s_ctx.calibration_sample_count < AD_KEYS_SAMPLE_BUF) {
        s_ctx.calibration_samples[s_ctx.calibration_sample_count++] = voltage_mv;
    }
    if (s_ctx.candidate_count >= s_ctx.cfg.stable_samples &&
        (tick_ms - s_ctx.candidate_since_ms) >= (uint32_t)s_ctx.cfg.debounce_ms) {
        s_ctx.centers_mv[s_ctx.calibration_index] =
            median_u16(s_ctx.calibration_samples, s_ctx.calibration_sample_count);
        ESP_LOGI(AD_KEYS_TAG, "标定 K%u: %umV", s_ctx.calibration_index + 1U,
                 s_ctx.centers_mv[s_ctx.calibration_index]);
        ++s_ctx.calibration_index;
        s_ctx.candidate_count = 0;
        s_ctx.calibration_sample_count = 0;
        s_ctx.calibration_wait_release = true;
        if (s_ctx.calibration_index >= AD_KEYS_COUNT) {
            calculate_windows_locked();
            if (save_calibration_locked() == ESP_OK) {
                s_ctx.calibration_valid = true;
                ESP_LOGI(AD_KEYS_TAG, "标定完成，结果已保存到 NVS");
            } else {
                ESP_LOGE(AD_KEYS_TAG, "标定完成但保存 NVS 失败");
            }
            s_ctx.calibrating = false;
            s_ctx.calibration_wait_release = false;
        }
    }
}

static void process_key(uint16_t voltage_mv, uint32_t tick_ms)
{
    uint8_t key = s_ctx.calibration_valid ? classify_voltage(voltage_mv) : 0;
    if (s_ctx.current_key != 0 && s_ctx.calibration_valid) {
        /*
         * 迟滞：当前键在原识别窗口外再偏移 100mV 才允许释放。
         * 跨键滑动时先释放当前键，再按新键的窗口重新确认，避免边界抖动切键。
         */
        uint8_t index = (uint8_t)(s_ctx.current_key - 1U);
        bool released = false;
        if (s_ctx.min_mv[index] > AD_KEYS_RELEASE_HYSTERESIS_MV &&
            voltage_mv <= (uint16_t)(s_ctx.min_mv[index] -
                                     AD_KEYS_RELEASE_HYSTERESIS_MV)) {
            released = true;
        }
        uint32_t release_max = (uint32_t)s_ctx.max_mv[index] +
                               AD_KEYS_RELEASE_HYSTERESIS_MV;
        if (release_max < 3300U && voltage_mv >= release_max) {
            released = true;
        }
        if (!released) {
            key = s_ctx.current_key;
        }
    }
    if (key == s_ctx.candidate_key) {
        if (s_ctx.candidate_count < UINT8_MAX) {
            ++s_ctx.candidate_count;
        }
    } else {
        s_ctx.candidate_key = key;
        s_ctx.candidate_count = 1;
        s_ctx.candidate_since_ms = tick_ms;
    }

    bool stable = s_ctx.candidate_count >= s_ctx.cfg.stable_samples &&
                  (tick_ms - s_ctx.candidate_since_ms) >= (uint32_t)s_ctx.cfg.debounce_ms;

    /* 最短按住时间未到就离开窗口: 回弹, 不发 PRESS/RELEASE. */
    if (s_ctx.armed_key != 0) {
        bool arm_lost = stable && key != s_ctx.armed_key;
        bool arm_ready = stable && key == s_ctx.armed_key &&
                         (tick_ms - s_ctx.press_start_ms) >= AD_KEYS_MIN_PRESS_MS;

        if (arm_lost) {
            ESP_LOGI(AD_KEYS_TAG, "忽略过短按键 K%u, held=%ums",
                     (unsigned)s_ctx.armed_key,
                     (unsigned)(tick_ms - s_ctx.press_start_ms));
            s_ctx.armed_key = 0;
        } else if (arm_ready) {
            s_ctx.current_key = s_ctx.armed_key;
            s_ctx.armed_key = 0;
            s_ctx.next_repeat_ms =
                tick_ms + (uint32_t)s_ctx.cfg.repeat_delay_ms;
            emit_event(AD_KEYS_EVENT_PRESS, s_ctx.current_key, voltage_mv, 0);
            return;
        } else {
            return;
        }
    }

    if (stable && key != s_ctx.current_key) {
        if (s_ctx.current_key != 0) {
            emit_event(AD_KEYS_EVENT_RELEASE, s_ctx.current_key, voltage_mv,
                       tick_ms - s_ctx.press_start_ms);
            s_ctx.current_key = 0;
        }
        if (key != 0) {
            s_ctx.armed_key = key;
            s_ctx.press_start_ms = tick_ms;
        }
        return;
    }

    /* 按下后 repeat_delay_ms 开始连发, 不再插入长按事件. */
    if (s_ctx.current_key != 0 && key == s_ctx.current_key &&
        tick_ms >= s_ctx.next_repeat_ms) {
        uint32_t held_ms = tick_ms - s_ctx.press_start_ms;

        s_ctx.next_repeat_ms = tick_ms + (uint32_t)s_ctx.cfg.repeat_ms;
        emit_event(AD_KEYS_EVENT_REPEAT, s_ctx.current_key, voltage_mv,
                   held_ms);
    }
}

static esp_err_t read_voltage_mv(uint16_t *voltage_mv)
{
    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_ctx.adc_handle, AD_KEYS_ADC_CHANNEL, &raw);
    if (err != ESP_OK) {
        return err;
    }
    int voltage = 0;
    if (s_ctx.cali_enabled &&
        adc_cali_raw_to_voltage(s_ctx.cali_handle, raw, &voltage) == ESP_OK) {
        *voltage_mv = (uint16_t)(voltage < 0 ? 0 : voltage);
    } else {
        // 12dB 近似量程，校准不可用时仅作降级显示，不用于精确阈值。
        *voltage_mv = (uint16_t)((raw * 3300L) / 4095L);
    }
    return ESP_OK;
}

static void maybe_log_sample(uint32_t tick_ms, uint16_t raw_mv, uint16_t filtered_mv)
{
    bool interval_due = !s_ctx.log_has_previous ||
                        (tick_ms - s_ctx.last_log_ms) >= AD_KEYS_LOG_PERIOD_MS;
    if (!interval_due) {
        return;
    }

    bool value_changed = !s_ctx.log_has_previous ||
                         abs_diff_u16(filtered_mv, s_ctx.last_logged_voltage_mv) >=
                             AD_KEYS_LOG_DELTA_MV;
    bool key_changed = !s_ctx.log_has_previous ||
                       s_ctx.current_key != s_ctx.last_logged_key;
    bool heartbeat_due = AD_KEYS_LOG_HEARTBEAT_MS != 0U &&
                         (!s_ctx.log_has_previous ||
                          (tick_ms - s_ctx.last_heartbeat_ms) >= AD_KEYS_LOG_HEARTBEAT_MS);
    if (!value_changed && !key_changed && !s_ctx.log_event_pending && !heartbeat_due) {
        return;
    }

    if (heartbeat_due) {
        size_t internal_free =
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_LOGI(AD_KEYS_TAG,
                 "ADC raw=%umV filtered=%umV key=%u candidate=%u(%u/%d) "
                 "idle=%umV cal=%s stack_free=%u heap=%u internal=%u",
                 raw_mv, filtered_mv, s_ctx.current_key,
                 s_ctx.candidate_key, s_ctx.candidate_count,
                 s_ctx.cfg.stable_samples, s_ctx.idle_mv,
                 s_ctx.calibration_valid ? "valid" : "fallback",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL),
                 (unsigned)esp_get_free_heap_size(), (unsigned)internal_free);
    } else {
        ESP_LOGI(AD_KEYS_TAG, "ADC raw=%umV filtered=%umV key=%u candidate=%u(%u/%d)",
                 raw_mv, filtered_mv, s_ctx.current_key,
                 s_ctx.candidate_key, s_ctx.candidate_count,
                 s_ctx.cfg.stable_samples);
    }
    s_ctx.last_log_ms = tick_ms;
    s_ctx.last_logged_voltage_mv = filtered_mv;
    s_ctx.last_logged_key = s_ctx.current_key;
    s_ctx.last_heartbeat_ms = tick_ms;
    s_ctx.log_has_previous = true;
    s_ctx.log_event_pending = false;
}

static void ad_keys_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(s_ctx.cfg.sample_period_ms);
    uint32_t read_error_count = 0;
    while (s_ctx.running) {
        uint16_t voltage = 0;
        esp_err_t read_err = read_voltage_mv(&voltage);
        if (read_err == ESP_OK) {
            uint16_t filtered = push_and_get_median(voltage);
            uint32_t tick_ms = now_ms();
            if (!s_ctx.idle_ready && s_ctx.idle_samples < 20) {
                if (filtered > s_ctx.idle_mv) {
                    s_ctx.idle_mv = filtered;
                }
                ++s_ctx.idle_samples;
                if (s_ctx.idle_samples == 20) {
                    s_ctx.idle_ready = true;
                    ESP_LOGI(AD_KEYS_TAG, "空闲电压基线: %umV", s_ctx.idle_mv);
#if AD_KEYS_ENABLE_RUNTIME_SAMPLING
                    if (!s_ctx.calibration_valid && !s_ctx.calibrating) {
                        start_calibration_locked();
                    }
#endif
                }
            } else if (filtered > s_ctx.idle_mv) {
                // 按键按下时电压只会下降，基线只向更高电压方向收敛。
                s_ctx.idle_mv = filtered;
            }
#if AD_KEYS_ENABLE_RUNTIME_SAMPLING
            if (s_ctx.boot_force_pending) {
                if (tick_ms >= s_ctx.boot_force_deadline_ms) {
                    s_ctx.boot_force_pending = false;
                    if (!s_ctx.calibration_valid && !s_ctx.calibrating) {
                        start_calibration_locked();
                    }
                } else {
                    // 基线尚未就绪时只用 3.3V 作为"是否明显按下"的临时参考;
                    // 它不写入 idle_mv, 也不会替代后续真实高水位采样.
                    uint16_t reference_mv = s_ctx.idle_ready ? s_ctx.idle_mv : 3300U;
                    if ((uint32_t)filtered + calibration_min_delta_mv() <= reference_mv) {
                        if (!s_ctx.calibrating) {
                            start_calibration_locked();
                        }
                        s_ctx.boot_force_pending = false;
                    }
                }
            }
#endif
            s_ctx.last_voltage_mv = filtered;
            s_ctx.last_raw_voltage_mv = voltage;
            if (s_ctx.calibrating) {
                process_calibration(filtered, tick_ms);
            } else {
                process_key(filtered, tick_ms);
            }
            maybe_log_sample(tick_ms, s_ctx.last_raw_voltage_mv, filtered);
        } else {
            ++read_error_count;
            /* ADC 瞬时读失败不应每 5ms 刷屏；首次和每 100 次各报一次。 */
            if (read_error_count == 1U || (read_error_count % 100U) == 0U) {
                ESP_LOGW(AD_KEYS_TAG, "ADC 采样失败 #%lu: %s",
                         (unsigned long)read_error_count,
                         esp_err_to_name(read_err));
            }
        }
        vTaskDelay(period);
    }
    ESP_LOGI(AD_KEYS_TAG, "按键任务退出，stack_free=%u，ADC 失败次数=%lu",
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned long)read_error_count);
    s_ctx.task = NULL;
    vTaskDelete(NULL);
}

void ad_keys_config_default(ad_keys_config_t *config)
{
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->sample_period_ms = AD_KEYS_DEFAULT_PERIOD_MS;
    config->median_window = AD_KEYS_DEFAULT_MEDIAN_WINDOW;
    config->stable_samples = AD_KEYS_DEFAULT_STABLE;
    config->debounce_ms = AD_KEYS_DEFAULT_DEBOUNCE_MS;
    config->repeat_delay_ms = AD_KEYS_DEFAULT_REPEAT_DELAY_MS;
    config->repeat_ms = AD_KEYS_DEFAULT_REPEAT_MS;
    config->calibration_idle_delta_mv = AD_KEYS_DEFAULT_CALIBRATION_MIN_DELTA_MV;
}

esp_err_t ad_keys_init(const ad_keys_config_t *config)
{
    if (s_ctx.lock) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
    ad_keys_config_default(&s_ctx.cfg);
    if (config) {
        s_ctx.cfg = *config;
        if (s_ctx.cfg.sample_period_ms <= 0) s_ctx.cfg.sample_period_ms = AD_KEYS_DEFAULT_PERIOD_MS;
        if (s_ctx.cfg.stable_samples <= 0) s_ctx.cfg.stable_samples = AD_KEYS_DEFAULT_STABLE;
        if (s_ctx.cfg.debounce_ms <= 0) s_ctx.cfg.debounce_ms = AD_KEYS_DEFAULT_DEBOUNCE_MS;
        if (s_ctx.cfg.repeat_delay_ms <= 0) {
            s_ctx.cfg.repeat_delay_ms = AD_KEYS_DEFAULT_REPEAT_DELAY_MS;
        }
        if (s_ctx.cfg.repeat_ms <= 0) s_ctx.cfg.repeat_ms = AD_KEYS_DEFAULT_REPEAT_MS;
        if (s_ctx.cfg.calibration_idle_delta_mv == 0) {
        s_ctx.cfg.calibration_idle_delta_mv = AD_KEYS_DEFAULT_CALIBRATION_MIN_DELTA_MV;
        }
    }

    s_ctx.lock = xSemaphoreCreateMutex();
    if (!s_ctx.lock) {
        return ESP_ERR_NO_MEM;
    }
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = AD_KEYS_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_ctx.adc_handle);
    if (err != ESP_OK) {
        return err;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_ctx.adc_handle, AD_KEYS_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_ctx.adc_handle);
        s_ctx.adc_handle = NULL;
        return err;
    }
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = AD_KEYS_ADC_UNIT,
        .chan = AD_KEYS_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_ctx.cali_handle);
    if (err == ESP_OK) {
        s_ctx.cali_enabled = true;
    } else {
        ESP_LOGW(AD_KEYS_TAG, "ADC 校准曲线不可用(%s)，降级为近似电压", esp_err_to_name(err));
    }

    err = load_calibration_locked();
    if (err != ESP_OK) {
        ESP_LOGW(AD_KEYS_TAG, "未找到有效标定数据，请进入标定模式");
    }
    ESP_LOGI(AD_KEYS_TAG,
             "初始化完成：ADC1_CH0(GPIO%d)，校准=%s，采样=%dms，中值=%d，稳定=%d，"
             "重复延迟=%dms/%dms，标定窗口=%umV",
             AD_KEYS_GPIO, s_ctx.cali_enabled ? "enabled" : "approx",
             s_ctx.cfg.sample_period_ms, s_ctx.cfg.median_window,
             s_ctx.cfg.stable_samples, s_ctx.cfg.repeat_delay_ms,
             s_ctx.cfg.repeat_ms, calibration_min_delta_mv());
    return ESP_OK;
}

esp_err_t ad_keys_start(void)
{
    if (!s_ctx.lock || !s_ctx.adc_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ctx.task) {
        return ESP_OK;
    }
    s_ctx.running = true;
    if (xTaskCreatePinnedToCore(ad_keys_task, "ad_keys", 4096, NULL, 5,
                                &s_ctx.task, AD_KEYS_TASK_CORE) != pdPASS) {
        s_ctx.running = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(AD_KEYS_TAG, "按键任务已钉到核 %d, 优先级 5, 采样 %dms",
             AD_KEYS_TASK_CORE, s_ctx.cfg.sample_period_ms);
    ESP_LOGI(AD_KEYS_TAG, "按键任务栈余量初值=%u words，标定=%s，空闲基线=%s",
             (unsigned)uxTaskGetStackHighWaterMark(s_ctx.task),
             s_ctx.calibration_valid ? "valid" : "fallback",
             s_ctx.idle_ready ? "ready" : "sampling");
    return ESP_OK;
}

esp_err_t ad_keys_stop(void)
{
    if (!s_ctx.lock) {
        return ESP_ERR_INVALID_STATE;
    }
    s_ctx.running = false;
    while (s_ctx.task) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    s_ctx.current_key = 0;
    return ESP_OK;
}

esp_err_t ad_keys_deinit(void)
{
    if (!s_ctx.lock) {
        return ESP_ERR_INVALID_STATE;
    }
    ad_keys_stop();
    if (s_ctx.cali_enabled) {
        adc_cali_delete_scheme_curve_fitting(s_ctx.cali_handle);
        s_ctx.cali_enabled = false;
    }
    if (s_ctx.adc_handle) {
        adc_oneshot_del_unit(s_ctx.adc_handle);
        s_ctx.adc_handle = NULL;
    }
    vSemaphoreDelete(s_ctx.lock);
    ESP_LOGI(AD_KEYS_TAG, "按键驱动已释放");
    memset(&s_ctx, 0, sizeof(s_ctx));
    return ESP_OK;
}

esp_err_t ad_keys_get_state(ad_keys_state_t *state)
{
    if (!state || !s_ctx.lock) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
    state->voltage_mv = s_ctx.last_voltage_mv;
    state->key = s_ctx.current_key;
    state->pressed = s_ctx.current_key != 0;
    state->calibrating = s_ctx.calibrating;
    state->calibration_index = s_ctx.calibration_index;
    state->calibration_valid = s_ctx.calibration_valid;
    xSemaphoreGive(s_ctx.lock);
    return ESP_OK;
}

uint16_t ad_keys_get_voltage_mv(void)
{
    return s_ctx.last_voltage_mv;
}

uint8_t ad_keys_get_key(void)
{
    return s_ctx.current_key;
}

esp_err_t ad_keys_start_calibration(void)
{
#if !AD_KEYS_ENABLE_RUNTIME_SAMPLING
    start_calibration_locked();
    ESP_LOGW(AD_KEYS_TAG, "忽略标定请求：运行时采样功能未启用");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_ctx.lock) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
    if (!s_ctx.idle_ready) {
        xSemaphoreGive(s_ctx.lock);
        return ESP_ERR_INVALID_STATE;
    }
    start_calibration_locked();
    xSemaphoreGive(s_ctx.lock);
    return ESP_OK;
#endif
}

esp_err_t ad_keys_request_calibration(void)
{
    return ad_keys_start_calibration();
}

bool ad_keys_is_calibrating(void)
{
    return s_ctx.calibrating;
}

uint8_t ad_keys_calibration_index(void)
{
    return s_ctx.calibration_index;
}

esp_err_t ad_keys_get_status(ad_keys_status_t *status)
{
    return ad_keys_get_state(status);
}

void ad_keys_set_event_callback(ad_keys_event_cb_t event_cb, void *user_ctx)
{
    if (!s_ctx.lock) {
        return;
    }
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
    s_ctx.cfg.event_cb = event_cb;
    s_ctx.cfg.event_user_ctx = user_ctx;
    xSemaphoreGive(s_ctx.lock);
}

esp_err_t ad_keys_get_calibration_centers(uint16_t centers_mv[AD_KEYS_COUNT])
{
    if (!centers_mv || !s_ctx.lock) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
    memcpy(centers_mv, s_ctx.centers_mv, sizeof(s_ctx.centers_mv));
    xSemaphoreGive(s_ctx.lock);
    return ESP_OK;
}

esp_err_t ad_keys_get_calibration_windows(uint16_t min_mv[AD_KEYS_COUNT],
                                          uint16_t max_mv[AD_KEYS_COUNT],
                                          uint16_t *idle_mv)
{
    if (!min_mv || !max_mv || !idle_mv || !s_ctx.lock) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
    memcpy(min_mv, s_ctx.min_mv, sizeof(s_ctx.min_mv));
    memcpy(max_mv, s_ctx.max_mv, sizeof(s_ctx.max_mv));
    *idle_mv = s_ctx.idle_mv;
    xSemaphoreGive(s_ctx.lock);
    return ESP_OK;
}

esp_err_t ad_keys_load_calibration(void)
{
    if (!s_ctx.lock) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_ctx.lock, portMAX_DELAY);
    esp_err_t err = load_calibration_locked();
    xSemaphoreGive(s_ctx.lock);
    return err;
}

esp_err_t ad_keys_clear_calibration(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("adkeys", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(nvs, AD_KEYS_NVS_KEY);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    s_ctx.calibration_valid = false;
    ESP_LOGI(AD_KEYS_TAG, "NVS 按键标定已清除，下一次启动将使用出厂窗口");
    return err;
}

bool ad_keys_boot_force_calibration_check(uint32_t window_ms)
{
#if !AD_KEYS_ENABLE_RUNTIME_SAMPLING
    (void)window_ms;
    return false;
#else
    if (!s_ctx.lock || !s_ctx.running) {
        return false;
    }
    uint32_t deadline = now_ms() + (window_ms ? window_ms : 1500U);
    // 基线由采样任务从真实 ADC 数据建立；按键按下时电压下降，不能用固定
    // 猜值直接标记 idle_ready，否则会把实际空闲电压误判成按键。
    s_ctx.boot_force_pending = true;
    s_ctx.boot_force_deadline_ms = deadline;
    while (now_ms() < deadline) {
        if (s_ctx.calibrating) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    s_ctx.boot_force_pending = false;
    return s_ctx.calibrating;
#endif
}

bool ad_keys_force_calibration_window(uint32_t window_ms)
{
    return ad_keys_boot_force_calibration_check(window_ms ? window_ms : 1500U);
}
