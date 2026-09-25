/**
 * @file main.c
 * @brief ESP32-S3 游戏机 P1 固件入口。
 *
 * P1 只负责屏幕、触摸和 AD 单路五键链路，不启动 WiFi、OTA 或游戏逻辑。
 */

#include "ad_keys.h"
#include "esp_err.h"
#include "esp_log.h"
#include "game_ui.h"
#include "lvgl_port.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "game_main";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ad_keys_config_t ad_config;
    ad_keys_config_default(&ad_config);
    ESP_ERROR_CHECK(ad_keys_init(&ad_config));
    ESP_ERROR_CHECK(ad_keys_start());

    /* 启动后 1.5 秒内按住任意实体键可强制进入 AD 标定。 */
    bool force_calibration = ad_keys_boot_force_calibration_check(1500);
    if (force_calibration) {
        ESP_LOGI(TAG, "检测到开机强制标定按键");
    }

    ESP_ERROR_CHECK(lvgl_port_init());
    ESP_ERROR_CHECK(game_ui_init());

    ESP_LOGI(TAG, "P1 初始化完成：ILI9488 480x320 + XPT2046 + AD 五键");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
