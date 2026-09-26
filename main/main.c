/**
 * @file main.c
 * @brief ESP32-S3 游戏机固件入口与局域网诊断服务接入。
 */

#include "ad_keys.h"
#include "diag_service.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "game_ui.h"
#include "game_wifi.h"
#include "lvgl_port.h"
#include "lvgl_screenshot.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ota_update.h"
#include "cJSON.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_GAME_CONSOLE_ENABLE_WIFI
/* 允许在未生成 sdkconfig 时仍能编译模拟/最小配置。 */
#define CONFIG_GAME_CONSOLE_ENABLE_WIFI 0
#endif

#ifndef CONFIG_GAME_CONSOLE_DIAG_PORT
/* 诊断 HTTP 服务的默认端口，实际值可由 Kconfig 覆盖。 */
#define CONFIG_GAME_CONSOLE_DIAG_PORT DIAG_SERVICE_DEFAULT_PORT
#endif

#ifndef CONFIG_GAME_CONSOLE_DIAG_TOKEN
/* 空 token 表示运行时生成随机 token，且不会把明文写入日志。 */
#define CONFIG_GAME_CONSOLE_DIAG_TOKEN ""
#endif

/* 环形日志只保留最近内容，供诊断服务读取，避免无限增长。 */
#define GAME_LOG_RING_SIZE 4096U
/* OTA 请求体上限，防止诊断接口因异常请求消耗过多堆内存。 */
#define OTA_REQUEST_MAX_LEN 768U
/* token 包含结尾 NUL；编译期 token 过长时会被安全截断。 */
#define DIAG_TOKEN_MAX_LEN 65U

static const char *TAG = "game_main";
static int64_t s_boot_time_us;              /* 启动时间戳，用于 uptime。 */
static diag_service_handle_t s_diag_handle; /* 诊断 HTTP 服务句柄。 */
static char s_diag_token[DIAG_TOKEN_MAX_LEN]; /* OTA/诊断 Bearer token，仅内存保存。 */
/* 保存 app_main 的任务句柄，便于诊断状态报告其栈余量。 */
static TaskHandle_t s_main_task_handle;

static char s_log_ring[GAME_LOG_RING_SIZE]; /* 最近日志的循环存储区。 */
static size_t s_log_head;                   /* 下一个写入位置。 */
static size_t s_log_count;                  /* 当前有效字符数。 */
static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_log_previous_vprintf; /* 安装诊断拦截前的输出函数。 */

/**
 * @brief 记录启动/运行时资源快照。
 *
 * 该函数只在启动阶段和低频心跳中调用，不放进高频 UI 或 ADC 循环；
 * 除总堆外同时报告内部堆、PSRAM 和当前任务栈高水位，便于定位碎片化
 * 或任务栈不足问题。
 */
static void game_log_runtime_resources(const char *phase)
{
    const char *label = phase ? phase : "runtime";
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    size_t internal_free =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    UBaseType_t stack_free = s_main_task_handle
                                 ? uxTaskGetStackHighWaterMark(s_main_task_handle)
                                 : uxTaskGetStackHighWaterMark(NULL);

    ESP_LOGI(TAG,
             "%s: heap_free=%u min_heap=%u internal_free=%u internal_largest=%u "
             "psram_free=%u main_stack_free=%u tasks=%u",
             label, (unsigned)free_heap, (unsigned)min_free_heap,
             (unsigned)internal_free, (unsigned)internal_largest,
             (unsigned)psram_free, (unsigned)stack_free,
             (unsigned)uxTaskGetNumberOfTasks());
}

/* 同时转发串口日志并复制到诊断服务的环形缓冲。 */
static int game_diag_log_vprintf(const char *format, va_list args)
{
    va_list copy;
    char line[256];
    int result;

    va_copy(copy, args);
    if (s_log_previous_vprintf) {
        result = s_log_previous_vprintf(format, args);
    } else {
        result = vprintf(format, args);
    }
    (void)vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);

    size_t length = strnlen(line, sizeof(line));
    portENTER_CRITICAL(&s_log_mux);
    for (size_t i = 0; i < length; ++i) {
        s_log_ring[s_log_head] = line[i];
        s_log_head = (s_log_head + 1U) % GAME_LOG_RING_SIZE;
        if (s_log_count < GAME_LOG_RING_SIZE) {
            ++s_log_count;
        }
    }
    portEXIT_CRITICAL(&s_log_mux);
    return result;
}

static esp_err_t game_diag_logs(httpd_req_t *req, void *ctx)
{
    (void)ctx;
    char *snapshot = (char *)malloc(GAME_LOG_RING_SIZE + 1U);
    if (!snapshot) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "log buffer allocation failed",
                               HTTPD_RESP_USE_STRLEN);
    }

    portENTER_CRITICAL(&s_log_mux);
    size_t start = (s_log_head + GAME_LOG_RING_SIZE - s_log_count) %
                   GAME_LOG_RING_SIZE;
    for (size_t i = 0; i < s_log_count; ++i) {
        snapshot[i] = s_log_ring[(start + i) % GAME_LOG_RING_SIZE];
    }
    size_t length = s_log_count;
    portEXIT_CRITICAL(&s_log_mux);
    snapshot[length] = '\0';

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, snapshot, (ssize_t)length);
    free(snapshot);
    return err;
}

static int game_read_high_score(void)
{
    nvs_handle_t handle;
    uint32_t score = 0;
    if (nvs_open("game", NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }
    (void)nvs_get_u32(handle, "high_score", &score);
    nvs_close(handle);
    return (int)score;
}

static const char *game_page_name(void)
{
    const char *name = game_ui_get_page_name();
    return name ? name : "select";
}

/* 生成不含凭据的运行状态 JSON，供局域网诊断接口读取。 */
static esp_err_t game_diag_status(diag_json_writer_t *writer, void *ctx)
{
    (void)ctx;
    const esp_app_desc_t *app = esp_app_get_description();
    const snake_state_t *state = game_ui_get_state();
    uint32_t score = state ? state->score : 0U;
    uint16_t length = state ? state->length : 0U;

    esp_err_t err = diag_json_writer_kv_string(writer, "firmware_version",
                                                app ? app->version : "unknown");
    if (err == ESP_OK) {
        err = diag_json_writer_kv_u64(writer, "uptime_ms",
                                      (uint64_t)((esp_timer_get_time() -
                                                  s_boot_time_us) / 1000LL));
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_u64(writer, "free_heap",
                                      (uint64_t)esp_get_free_heap_size());
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_u64(writer, "minimum_free_heap",
                                      (uint64_t)esp_get_minimum_free_heap_size());
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_u64(
            writer, "internal_free_heap",
            (uint64_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_u64(
            writer, "psram_free_heap",
            (uint64_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_u64(
            writer, "task_count", (uint64_t)uxTaskGetNumberOfTasks());
    }
    if (err == ESP_OK && s_main_task_handle != NULL) {
        err = diag_json_writer_kv_u64(
            writer, "main_stack_high_water",
            (uint64_t)uxTaskGetStackHighWaterMark(s_main_task_handle));
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_i64(writer, "high_score",
                                      (int64_t)game_read_high_score());
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_string(writer, "page", game_page_name());
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_u64(writer, "score", score);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_u64(writer, "snake_length", length);
    }
    if (err == ESP_OK) {
        err = diag_json_writer_kv_u64(
            writer, "wiggle_updated_objects",
            (uint64_t)game_ui_get_wiggle_updated_objects());
    }
    return err;
}

/* 截图请求只负责调用线程安全的 LVGL 快照接口。 */
static esp_err_t game_diag_screenshot(uint8_t **bmp_buf, size_t *bmp_len,
                                      void *ctx)
{
    (void)ctx;
    if (!bmp_buf || !bmp_len) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 诊断 HTTP 任务只投递截图 job；真正的 LVGL 快照在 LVGL 任务中执行。 */
    return lvgl_port_capture_bmp(bmp_buf, bmp_len, 1500U);
}

static void game_diag_screenshot_free(uint8_t *bmp_buf, void *ctx)
{
    (void)ctx;
    free(bmp_buf);
}

static esp_err_t game_send_json(httpd_req_t *req, int status, const char *json)
{
    httpd_resp_set_status(req, status == 200 ? "200 OK" : "400 Bad Request");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static bool game_query_value(httpd_req_t *req, const char *key,
                             char *out, size_t out_len)
{
    int query_len = httpd_req_get_url_query_len(req);
    if (query_len <= 0 || !out || out_len == 0U) {
        return false;
    }
    char *query = (char *)calloc((size_t)query_len + 1U, 1U);
    if (!query) {
        return false;
    }
    bool found = httpd_req_get_url_query_str(req, query,
                                             (size_t)query_len + 1U) == ESP_OK &&
                 httpd_query_key_value(query, key, out, out_len) == ESP_OK;
    free(query);
    return found && out[0] != '\0';
}

static esp_err_t game_read_request_body(httpd_req_t *req, char *body,
                                        size_t body_len)
{
    if (!body || body_len < 2U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (req->content_len == 0) {
        body[0] = '\0';
        return ESP_OK;
    }
    if ((size_t)req->content_len >= body_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received = 0;
    while (received < (size_t)req->content_len) {
        int ret = httpd_req_recv(req, body + received,
                                 (size_t)req->content_len - received);
        if (ret <= 0) {
            return ret == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }
        received += (size_t)ret;
    }
    body[received] = '\0';
    return ESP_OK;
}

static bool game_manifest_url_from_request(httpd_req_t *req, char *url,
                                           size_t url_len)
{
    if (!url || url_len == 0U) {
        return false;
    }
    url[0] = '\0';
    if (game_query_value(req, "manifest_url", url, url_len) ||
        game_query_value(req, "url", url, url_len)) {
        return true;
    }

    char body[OTA_REQUEST_MAX_LEN] = {0};
    if (game_read_request_body(req, body, sizeof(body)) != ESP_OK ||
        body[0] == '\0') {
        return false;
    }

    cJSON *root = cJSON_ParseWithLength(body, strlen(body));
    if (root) {
        const char *keys[] = {"manifest_url", "url", "manifest"};
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
            cJSON *item = cJSON_GetObjectItem(root, keys[i]);
            if (cJSON_IsString(item) && item->valuestring &&
                item->valuestring[0] != '\0') {
                snprintf(url, url_len, "%s", item->valuestring);
                cJSON_Delete(root);
                return true;
            }
        }
        cJSON_Delete(root);
    }
    size_t body_length = strlen(body);
    if (body_length > 0U && body_length + 1U <= url_len &&
        strstr(body, "://") != NULL) {
        memcpy(url, body, body_length + 1U);
        return true;
    }
    return false;
}

static esp_err_t game_diag_ota_status(diag_json_writer_t *writer, void *ctx)
{
    (void)ctx;
    ota_update_status_t status = {0};
    esp_err_t err = ota_update_get_status(&status);
    if (err != ESP_OK) {
        return err;
    }
    char json[768] = {0};
    err = ota_update_status_json(&status, json, sizeof(json));
    if (err == ESP_OK) {
        err = diag_json_writer_kv_raw(writer, "ota", json);
    }
    return err;
}

static esp_err_t game_diag_ota_check(httpd_req_t *req, void *ctx)
{
    (void)ctx;
    char url[OTA_UPDATE_MANIFEST_URL_MAX_LEN] = {0};
    if (!game_manifest_url_from_request(req, url, sizeof(url))) {
        return game_send_json(req, 400,
                              "{\"ok\":false,\"error\":\"manifest_url_missing\"}");
    }
    char response[OTA_UPDATE_MANIFEST_URL_MAX_LEN + 96U];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"manifest_url\":\"%s\",\"mode\":\"manifest_url\"}",
             url);
    return game_send_json(req, 200, response);
}

static esp_err_t game_diag_ota_start(httpd_req_t *req, void *ctx)
{
    (void)ctx;
    char url[OTA_UPDATE_MANIFEST_URL_MAX_LEN] = {0};
    if (!game_manifest_url_from_request(req, url, sizeof(url))) {
        return game_send_json(req, 400,
                              "{\"ok\":false,\"error\":\"manifest_url_missing\"}");
    }

    esp_err_t err = ota_update_start_from_manifest_auth(url, s_diag_token);
    if (err != ESP_OK) {
        char response[128];
        snprintf(response, sizeof(response),
                 "{\"ok\":false,\"error\":\"ota_start_failed\",\"esp_err\":%d}",
                 (int)err);
        return game_send_json(req, 400, response);
    }

    esp_err_t send_err = game_send_json(
        req, 200, "{\"ok\":true,\"message\":\"ota_ready_to_reboot\"}");
    if (send_err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
    }
    return send_err;
}

static esp_err_t game_diag_reboot(httpd_req_t *req, void *ctx)
{
    (void)ctx;
    esp_err_t err = game_send_json(req, 200, "{\"ok\":true,\"message\":\"rebooting\"}");
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(150));
        esp_restart();
    }
    return err;
}

/* 获得 IPv4 后启动一次诊断服务；重复回调不会重复监听端口。 */
static void game_diag_start_for_ip(const char *ip_addr)
{
    if (!ip_addr || ip_addr[0] == '\0' || diag_service_is_running()) {
        return;
    }

    diag_service_config_t config = {
        .service_name = "esp32-game-console",
        .port = (uint16_t)CONFIG_GAME_CONSOLE_DIAG_PORT,
        .bearer_token = s_diag_token,
        .min_token_len = 16U,
        .on_status = game_diag_status,
        .on_logs = game_diag_logs,
        .on_screenshot = game_diag_screenshot,
        .on_screenshot_free = game_diag_screenshot_free,
        .on_ota_status = game_diag_ota_status,
        .on_ota_check = game_diag_ota_check,
        .on_ota_start = game_diag_ota_start,
        .on_reboot = game_diag_reboot,
    };
    esp_err_t err = diag_service_start(&config, &s_diag_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "诊断服务已启动: http://%s:%u", ip_addr,
                 (unsigned)config.port);
        game_log_runtime_resources("诊断服务启动后");
    } else {
        ESP_LOGW(TAG, "diag_service_start 失败: %s", esp_err_to_name(err));
    }
}

static void game_wifi_ip_callback(const char *ip_addr, void *ctx)
{
    (void)ctx;
    game_diag_start_for_ip(ip_addr);
}

static void game_diag_prepare_token(void)
{
    if (CONFIG_GAME_CONSOLE_DIAG_TOKEN[0] != '\0') {
        snprintf(s_diag_token, sizeof(s_diag_token), "%s",
                 CONFIG_GAME_CONSOLE_DIAG_TOKEN);
        return;
    }

    uint8_t random_bytes[16];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    for (size_t i = 0; i < sizeof(random_bytes); ++i) {
        snprintf(s_diag_token + i * 2U, sizeof(s_diag_token) - i * 2U,
                 "%02x", random_bytes[i]);
    }
    s_diag_token[sizeof(random_bytes) * 2U] = '\0';
    /* token 是 Bearer 凭据，日志中只说明生成结果，绝不打印明文。 */
    ESP_LOGI(TAG, "诊断服务已生成随机 token（长度=%u，不输出明文）",
             (unsigned)(sizeof(random_bytes) * 2U));
}

static void game_diag_init(void)
{
    s_log_previous_vprintf = esp_log_set_vprintf(game_diag_log_vprintf);
    game_diag_prepare_token();
    ESP_ERROR_CHECK(ota_update_service_init());
    ESP_LOGI(TAG, "诊断日志环形缓冲已启用，OTA 服务已初始化");
    esp_err_t err = ota_update_boot_guard_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA 启动自检未通过: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    s_main_task_handle = xTaskGetCurrentTaskHandle();
    s_boot_time_us = esp_timer_get_time();
    game_log_runtime_resources("启动入口");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需要擦除重建: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS 初始化完成");

    game_diag_init();
    game_log_runtime_resources("诊断/OTA 初始化后");

    ad_keys_config_t ad_config;
    ad_keys_config_default(&ad_config);
    ESP_ERROR_CHECK(ad_keys_init(&ad_config));
    ESP_ERROR_CHECK(ad_keys_start());
    ESP_LOGI(TAG, "AD 五键驱动已启动：采样=%dms，中值窗口=%d，稳定样本=%d",
             ad_config.sample_period_ms, ad_config.median_window,
             ad_config.stable_samples);
    /* 运行时键值采样已关闭, 不再阻塞等待开机强制标定. */

    ESP_ERROR_CHECK(lvgl_port_init());
    ESP_ERROR_CHECK(lvgl_screenshot_init());
    ESP_ERROR_CHECK(game_ui_init());
    game_log_runtime_resources("LVGL/游戏 UI 初始化后");

#if CONFIG_GAME_CONSOLE_ENABLE_WIFI
    game_wifi_register_ip_callback(game_wifi_ip_callback, NULL);
    ESP_ERROR_CHECK(game_wifi_start());
#else
    ESP_LOGI(TAG, "WiFi 已由 CONFIG_GAME_CONSOLE_ENABLE_WIFI 关闭");
#endif

    (void)ota_update_mark_app_valid_after_selftest();
    ESP_LOGI(TAG, "游戏机初始化完成：ILI9488 480x320 + XPT2046 + AD 五键");
    game_log_runtime_resources("启动完成");
    uint32_t next_resource_log_ms = 30000U;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if ((int32_t)(now_ms - next_resource_log_ms) >= 0) {
            ESP_LOGI(TAG, "运行状态：page=%s wifi=%s",
                     game_page_name(), game_wifi_is_connected() ? "connected" : "offline");
            game_log_runtime_resources("运行时心跳");
            next_resource_log_ms = now_ms + 30000U;
        }
    }
}
