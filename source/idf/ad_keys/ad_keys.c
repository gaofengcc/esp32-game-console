#include "ad_keys.h"

#include <math.h>
#include <string.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
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
#define AD_KEYS_DEFAULT_PERIOD_MS 10
#define AD_KEYS_DEFAULT_STABLE 3
#define AD_KEYS_DEFAULT_DEBOUNCE_MS 25
#define AD_KEYS_DEFAULT_LONG_MS 800
#define AD_KEYS_DEFAULT_REPEAT_MS 150
#define AD_KEYS_DEFAULT_CALIBRATION_MIN_DELTA_MV 160
#ifndef AD_KEYS_LOG_PERIOD_MS
#define AD_KEYS_LOG_PERIOD_MS 100U
#endif
#ifndef AD_KEYS_LOG_DELTA_MV
#define AD_KEYS_LOG_DELTA_MV 100U
#endif
#ifndef AD_KEYS_LOG_HEARTBEAT_MS
#define AD_KEYS_LOG_HEARTBEAT_MS 30000U
#endif

typedef struct {
    uint32_t version;
    uint16_t min_mv[AD_KEYS_COUNT];
    uint16_t max_mv[AD_KEYS_COUNT];
    uint16_t center_mv[AD_KEYS_COUNT];
    uint16_t idle_mv;
} ad_keys_nvs_blob_t;

typedef struct {
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t cali_handle;
    bool cali_enabled;
    TaskHandle_t task;
    SemaphoreHandle_t lock;
    ad_keys_config_t cfg;

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

    uint16_t history[AD_KEYS_SAMPLE_BUF];
    uint8_t history_count;
    uint8_t history_pos;
    uint32_t idle_accum;
    uint8_t idle_samples;
    bool idle_ready;

    uint8_t candidate_key;
    uint8_t candidate_count;
    uint32_t candidate_since_ms;
    uint16_t calibration_samples[AD_KEYS_SAMPLE_BUF];
    uint8_t calibration_sample_count;
    uint8_t current_key;
    uint32_t press_start_ms;
    uint32_t next_repeat_ms;
    bool long_sent;

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
    s_ctx.log_event_pending = true;
    ad_keys_event_t event = {
        .key = key,
        .type = type,
        .voltage_mv = voltage,
        .held_ms = held,
    };
    ESP_LOGI(AD_KEYS_TAG, "事件 K%u: %s, %umV, held=%ums", key,
             type == AD_KEYS_EVENT_PRESS ? "PRESS" :
             type == AD_KEYS_EVENT_LONG ? "LONG" :
             type == AD_KEYS_EVENT_REPEAT ? "REPEAT" : "RELEASE",
             voltage, (unsigned)held);
    if (cb) {
        cb(&event, s_ctx.cfg.event_user_ctx);
    }
}

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
        s_ctx.calibration_valid = false;
        return err;
    }
    ad_keys_nvs_blob_t blob = {0};
    size_t size = sizeof(blob);
    err = nvs_get_blob(nvs, AD_KEYS_NVS_KEY, &blob, &size);
    nvs_close(nvs);
    if (err != ESP_OK || size != sizeof(blob) || blob.version != AD_KEYS_NVS_VERSION) {
        s_ctx.calibration_valid = false;
        if (err == ESP_OK && size == sizeof(blob)) {
            ESP_LOGW(AD_KEYS_TAG, "标定数据版本无效(%lu)，丢弃并要求重标", (unsigned long)blob.version);
            discard_calibration_blob();
        }
        return err == ESP_OK ? ESP_ERR_INVALID_VERSION : err;
    }

    bool valid = blob.idle_mv > 0 && blob.idle_mv <= 3300;
    if (!valid) {
        ESP_LOGW(AD_KEYS_TAG, "标定数据无效：空闲基线 %umV 超出范围，丢弃并要求重标",
                 blob.idle_mv);
    }
    uint16_t min_delta_mv = calibration_min_delta_mv();
    for (uint8_t i = 0; i < AD_KEYS_COUNT; ++i) {
        uint16_t center = blob.center_mv[i];
        uint16_t delta = blob.idle_mv > center ? (uint16_t)(blob.idle_mv - center) : 0;
        if (center == 0 || center >= blob.idle_mv || delta < min_delta_mv) {
            ESP_LOGW(AD_KEYS_TAG,
                     "标定数据无效：K%u中心%umV接近空闲基线%umV（差%umV<最小%umV），丢弃并要求重标",
                     i + 1U, center, blob.idle_mv, delta, min_delta_mv);
            valid = false;
        }
    }
    if (!valid) {
        discard_calibration_blob();
        return ESP_ERR_INVALID_RESPONSE;
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

static void start_calibration_locked(void)
{
    s_ctx.calibrating = true;
    s_ctx.current_key = 0;
    s_ctx.long_sent = false;
    s_ctx.calibration_wait_release = false;
    s_ctx.calibration_index = 0;
    s_ctx.calibration_warned_mask = 0;
    s_ctx.candidate_key = 0;
    s_ctx.candidate_count = 0;
    s_ctx.candidate_since_ms = 0;
    s_ctx.calibration_sample_count = 0;
    memset(s_ctx.centers_mv, 0, sizeof(s_ctx.centers_mv));
    ESP_LOGI(AD_KEYS_TAG, "进入标定模式，请依次按下 K1..K5");
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
    if (stable && key != s_ctx.current_key) {
        if (s_ctx.current_key != 0) {
            emit_event(AD_KEYS_EVENT_RELEASE, s_ctx.current_key, voltage_mv,
                       tick_ms - s_ctx.press_start_ms);
        }
        s_ctx.current_key = key;
        s_ctx.long_sent = false;
        if (key != 0) {
            s_ctx.press_start_ms = tick_ms;
            s_ctx.next_repeat_ms = tick_ms + (uint32_t)s_ctx.cfg.long_press_ms;
            emit_event(AD_KEYS_EVENT_PRESS, key, voltage_mv, 0);
        }
    }

    if (s_ctx.current_key != 0 && key == s_ctx.current_key) {
        uint32_t held_ms = tick_ms - s_ctx.press_start_ms;
        if (!s_ctx.long_sent && held_ms >= (uint32_t)s_ctx.cfg.long_press_ms) {
            s_ctx.long_sent = true;
            s_ctx.next_repeat_ms = tick_ms + (uint32_t)s_ctx.cfg.repeat_ms;
            emit_event(AD_KEYS_EVENT_LONG, s_ctx.current_key, voltage_mv, held_ms);
        } else if (s_ctx.long_sent && tick_ms >= s_ctx.next_repeat_ms) {
            s_ctx.next_repeat_ms = tick_ms + (uint32_t)s_ctx.cfg.repeat_ms;
            emit_event(AD_KEYS_EVENT_REPEAT, s_ctx.current_key, voltage_mv, held_ms);
        }
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

    ESP_LOGI(AD_KEYS_TAG, "ADC raw=%umV filtered=%umV key=%u",
             raw_mv, filtered_mv, s_ctx.current_key);
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
    while (s_ctx.running) {
        uint16_t voltage = 0;
        if (read_voltage_mv(&voltage) == ESP_OK) {
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
                    if (!s_ctx.calibration_valid && !s_ctx.calibrating) {
                        // NVS 没有标定数据时自动进入首次标定，主应用可据此切换标定页。
                        start_calibration_locked();
                    }
                }
            } else if (filtered > s_ctx.idle_mv) {
                // 按键按下时电压只会下降，基线只向更高电压方向收敛。
                s_ctx.idle_mv = filtered;
            }
            if (s_ctx.boot_force_pending) {
                if (tick_ms >= s_ctx.boot_force_deadline_ms) {
                    s_ctx.boot_force_pending = false;
                    if (!s_ctx.calibration_valid && !s_ctx.calibrating) {
                        start_calibration_locked();
                    }
                } else {
                    // 基线尚未就绪时只用 3.3V 作为“是否明显按下”的临时参考；
                    // 它不写入 idle_mv，也不会替代后续真实高水位采样。
                    uint16_t reference_mv = s_ctx.idle_ready ? s_ctx.idle_mv : 3300U;
                    if ((uint32_t)filtered + calibration_min_delta_mv() <= reference_mv) {
                        if (!s_ctx.calibrating) {
                            start_calibration_locked();
                        }
                        s_ctx.boot_force_pending = false;
                    }
                }
            }
            s_ctx.last_voltage_mv = filtered;
            s_ctx.last_raw_voltage_mv = voltage;
            if (s_ctx.calibrating) {
                process_calibration(filtered, tick_ms);
            } else {
                process_key(filtered, tick_ms);
            }
            maybe_log_sample(tick_ms, s_ctx.last_raw_voltage_mv, filtered);
        }
        vTaskDelay(period);
    }
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
    config->median_window = 5;
    config->stable_samples = AD_KEYS_DEFAULT_STABLE;
    config->debounce_ms = AD_KEYS_DEFAULT_DEBOUNCE_MS;
    config->long_press_ms = AD_KEYS_DEFAULT_LONG_MS;
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
        if (s_ctx.cfg.long_press_ms <= 0) s_ctx.cfg.long_press_ms = AD_KEYS_DEFAULT_LONG_MS;
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
    if (xTaskCreate(ad_keys_task, "ad_keys", 4096, NULL, 5, &s_ctx.task) != pdPASS) {
        s_ctx.running = false;
        return ESP_ERR_NO_MEM;
    }
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
    return err;
}

bool ad_keys_boot_force_calibration_check(uint32_t window_ms)
{
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
}

bool ad_keys_force_calibration_window(uint32_t window_ms)
{
    return ad_keys_boot_force_calibration_check(window_ms ? window_ms : 1500U);
}
