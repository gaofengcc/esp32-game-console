/**
 * @file game_wifi.c
 * @brief 编译期凭据 STA、IP 通知和非阻塞断线重连。
 */

#include "game_wifi.h"

#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"

static const char *TAG = "game_wifi";

#ifndef CONFIG_USER_WIFI_SSID
#define CONFIG_USER_WIFI_SSID ""
#endif

#ifndef CONFIG_USER_WIFI_PASSWORD
#define CONFIG_USER_WIFI_PASSWORD ""
#endif

#ifndef CONFIG_GAME_CONSOLE_ENABLE_WIFI
#define CONFIG_GAME_CONSOLE_ENABLE_WIFI 1
#endif

#define GAME_WIFI_RECONNECT_BIT   BIT0
/* 断线重连采用指数退避，避免 AP 不可用时持续占满事件任务。 */
#define GAME_WIFI_RECONNECT_BASE_MS 1000U
#define GAME_WIFI_RECONNECT_MAX_MS 30000U
/* 重连任务只等待事件，不直接处理业务 UI。 */
#define GAME_WIFI_RECONNECT_STACK 3072U
#define GAME_WIFI_RECONNECT_PRIO  (tskIDLE_PRIORITY + 2)

/* 这些状态由 ESP-IDF 事件任务和重连任务共同读取，更新保持简短。 */
static esp_netif_t *s_sta_netif;
static EventGroupHandle_t s_events;
static TaskHandle_t s_reconnect_task;
static game_wifi_ip_callback_t s_ip_callback;
static void *s_ip_callback_ctx;
static char s_ip4[16];
static volatile bool s_started;
static volatile bool s_connected;
static volatile uint32_t s_retry_count;

/**
 * @brief 根据失败次数计算下一次重连延时。
 *
 * retry_count 从 1 开始；延时最大封顶 30 秒，防止整数溢出和长时间
 * 阻塞事件通知。
 */
static uint32_t reconnect_delay_ms(uint32_t retry_count)
{
    uint32_t delay_ms = GAME_WIFI_RECONNECT_BASE_MS;
    uint32_t shifts = retry_count > 0 ? retry_count - 1U : 0U;

    while (shifts-- > 0U && delay_ms < GAME_WIFI_RECONNECT_MAX_MS) {
        delay_ms *= 2U;
        if (delay_ms > GAME_WIFI_RECONNECT_MAX_MS) {
            delay_ms = GAME_WIFI_RECONNECT_MAX_MS;
        }
    }
    return delay_ms;
}

static void game_wifi_reconnect_task(void *arg)
{
    (void)arg;

    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(
            s_events,
            GAME_WIFI_RECONNECT_BIT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);
        if ((bits & GAME_WIFI_RECONNECT_BIT) == 0U || !s_started ||
            s_connected) {
            continue;
        }

        uint32_t retry = s_retry_count;
        uint32_t delay_ms = reconnect_delay_ms(retry);
        if (retry <= 1U || (retry % 5U) == 0U) {
            ESP_LOGI(TAG, "WiFi 断线，将在 %lu ms 后重连（第 %lu 次）",
                     (unsigned long)delay_ms, (unsigned long)retry);
        } else {
            ESP_LOGD(TAG, "WiFi 断线重连退避：%lu ms（第 %lu 次）",
                     (unsigned long)delay_ms, (unsigned long)retry);
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));

        if (!s_started || s_connected) {
            continue;
        }

        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_STATE &&
            err != ESP_ERR_WIFI_CONN) {
            ESP_LOGW(TAG, "esp_wifi_connect 失败: %s", esp_err_to_name(err));
        } else if ((retry % 5U) == 0U) {
            /* 网络长期不可用时只每 5 次记录一次任务资源，避免刷屏。 */
            ESP_LOGI(TAG, "重连任务状态：retry=%lu stack_free=%u heap=%u",
                     (unsigned long)retry,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                       MALLOC_CAP_8BIT));
        }
    }
}

static void game_wifi_event_handler(void *arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
            if (ps_err != ESP_OK) {
                ESP_LOGW(TAG, "关闭 WiFi 省电模式失败: %s",
                         esp_err_to_name(ps_err));
            }
            ESP_LOGI(TAG, "STA 已启动，开始连接（配置来源：编译期 CONFIG_USER_WIFI_*）");
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_STATE &&
                err != ESP_ERR_WIFI_CONN) {
                ESP_LOGW(TAG, "首次 esp_wifi_connect 失败: %s",
                         esp_err_to_name(err));
            }
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            s_connected = false;
            s_ip4[0] = '\0';
            s_retry_count++;
            const wifi_event_sta_disconnected_t *event =
                (const wifi_event_sta_disconnected_t *)event_data;
            if (s_retry_count <= 1U || (s_retry_count % 5U) == 0U) {
                if (event != NULL) {
                    ESP_LOGW(TAG, "WiFi 断开：reason=%u，累计重试=%lu",
                             (unsigned)event->reason,
                             (unsigned long)s_retry_count);
                } else {
                    ESP_LOGW(TAG, "WiFi 断开：无 reason，累计重试=%lu",
                             (unsigned long)s_retry_count);
                }
            } else {
                ESP_LOGD(TAG, "WiFi 断开（第 %lu 次，reason=%u）",
                         (unsigned long)s_retry_count,
                         event ? (unsigned)event->reason : 0U);
            }
            xEventGroupSetBits(s_events, GAME_WIFI_RECONNECT_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        snprintf(s_ip4, sizeof(s_ip4), IPSTR, IP2STR(&event->ip_info.ip));
        uint32_t retry_count = s_retry_count;
        s_retry_count = 0;
        s_connected = true;
        ESP_LOGI(TAG, "WiFi 已连接，IP=%s，重试次数=%lu（配置来源：编译期 CONFIG_USER_WIFI_*）",
                 s_ip4, (unsigned long)retry_count);
        if (s_ip_callback != NULL) {
            s_ip_callback(s_ip4, s_ip_callback_ctx);
        }
    }
}

void game_wifi_register_ip_callback(game_wifi_ip_callback_t callback, void *ctx)
{
    s_ip_callback = callback;
    s_ip_callback_ctx = ctx;
}

bool game_wifi_is_connected(void)
{
    return s_connected;
}

esp_err_t game_wifi_get_ip4(char *out_ip, size_t len)
{
    if (out_ip == NULL || len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_connected || s_ip4[0] == '\0') {
        out_ip[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(out_ip, len, "%s", s_ip4);
    return ESP_OK;
}

esp_err_t game_wifi_start(void)
{
#if !CONFIG_GAME_CONSOLE_ENABLE_WIFI
    ESP_LOGI(TAG, "WiFi 已由 CONFIG_GAME_CONSOLE_ENABLE_WIFI 关闭");
    return ESP_OK;
#else
    if (s_started) {
        return ESP_OK;
    }
    if (CONFIG_USER_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "未配置 CONFIG_USER_WIFI_SSID，跳过 WiFi STA 启动");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "准备启动 WiFi STA：SSID 已配置（长度=%u），密码=%s",
             (unsigned)(sizeof(CONFIG_USER_WIFI_SSID) - 1U),
             CONFIG_USER_WIFI_PASSWORD[0] != '\0' ? "已配置" : "未配置");

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        return err;
    }
    (void)esp_wifi_set_ps(WIFI_PS_NONE);

    err = esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, game_wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, game_wifi_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(game_wifi_reconnect_task, "game_wifi_reconnect",
                    GAME_WIFI_RECONNECT_STACK, NULL,
                    GAME_WIFI_RECONNECT_PRIO, &s_reconnect_task) != pdPASS) {
        vEventGroupDelete(s_events);
        s_events = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "WiFi 重连任务已创建：stack=%u words，priority=%u，internal_free=%u",
             (unsigned)GAME_WIFI_RECONNECT_STACK,
             (unsigned)GAME_WIFI_RECONNECT_PRIO,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                               MALLOC_CAP_8BIT));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, CONFIG_USER_WIFI_SSID,
            sizeof(wifi_config.sta.ssid) - 1U);
    if (CONFIG_USER_WIFI_PASSWORD[0] != '\0') {
        strncpy((char *)wifi_config.sta.password, CONFIG_USER_WIFI_PASSWORD,
                sizeof(wifi_config.sta.password) - 1U);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }
    if (err == ESP_OK) {
        s_started = true;
        err = esp_wifi_start();
    }
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        s_started = false;
        ESP_LOGE(TAG, "WiFi STA 启动失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "WiFi STA 启动请求已提交（配置来源：编译期 CONFIG_USER_WIFI_*，"
             "省电模式=关闭）");
    return ESP_OK;
#endif
}
