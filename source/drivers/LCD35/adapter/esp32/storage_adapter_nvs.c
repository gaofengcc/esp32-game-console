/**
 * @file storage_adapter_nvs.c
 * @brief ESP32 NVS存储适配器实现
 *
 * 使用ESP-IDF的NVS(Non-Volatile Storage)实现数据持久化
 *
 * @copyright Copyright (c) 2024
 */

#include "storage_adapter.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "storage_adapter";
static bool s_nvs_initialized = false;

/* 内部上下文结构体 */
typedef struct {
    nvs_handle_t nvs_handle;
    char namespace[16];
} storage_context_t;

/**
 * @brief 初始化NVS
 */
static esp_err_t ensure_nvs_initialized(void)
{
    if (s_nvs_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS init failed without erase: %s", esp_err_to_name(ret));
        return ret;
    }

    if (ret != ESP_OK) {
        return ret;
    }

    s_nvs_initialized = true;
    return ESP_OK;
}

/**
 * @brief 存储初始化实现
 */
storage_handle_t storage_init(const char *namespace)
{
    if (namespace == NULL) {
        return NULL;
    }

    /* 确保NVS已初始化 */
    esp_err_t ret = ensure_nvs_initialized();
    if (ret != ESP_OK) {
        return NULL;
    }

    storage_context_t *ctx = (storage_context_t *)malloc(sizeof(storage_context_t));
    if (ctx == NULL) {
        return NULL;
    }

    memset(ctx, 0, sizeof(storage_context_t));
    strncpy(ctx->namespace, namespace, sizeof(ctx->namespace) - 1);

    ret = nvs_open(namespace, NVS_READWRITE, &ctx->nvs_handle);
    if (ret != ESP_OK) {
        free(ctx);
        return NULL;
    }

    return (storage_handle_t)ctx;
}

/**
 * @brief 存储去初始化实现
 */
void storage_deinit(storage_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    storage_context_t *ctx = (storage_context_t *)handle;
    nvs_close(ctx->nvs_handle);
    free(ctx);
}

/**
 * @brief 存储写入实现
 */
int storage_write(storage_handle_t handle, const char *key, const uint8_t *data, size_t len)
{
    if (handle == NULL || key == NULL || data == NULL) {
        return -1;
    }

    storage_context_t *ctx = (storage_context_t *)handle;

    esp_err_t ret = nvs_set_blob(ctx->nvs_handle, key, data, len);
    return (ret == ESP_OK) ? 0 : -1;
}

/**
 * @brief 存储读取实现
 */
int storage_read(storage_handle_t handle, const char *key, uint8_t *data, size_t len)
{
    if (handle == NULL || key == NULL || data == NULL) {
        return -1;
    }

    storage_context_t *ctx = (storage_context_t *)handle;

    size_t required_size = len;
    esp_err_t ret = nvs_get_blob(ctx->nvs_handle, key, data, &required_size);

    if (ret == ESP_OK) {
        return (int)required_size;
    } else if (ret == ESP_ERR_NVS_INVALID_LENGTH) {
        return (int)required_size; /* 返回所需缓冲区大小 */
    } else {
        return -1;
    }
}

/**
 * @brief 检查键是否存在实现
 */
bool storage_exists(storage_handle_t handle, const char *key)
{
    if (handle == NULL || key == NULL) {
        return false;
    }

    storage_context_t *ctx = (storage_context_t *)handle;

    size_t len = 0;
    esp_err_t ret = nvs_get_blob(ctx->nvs_handle, key, NULL, &len);

    return (ret == ESP_OK);
}

/**
 * @brief 删除键实现
 */
int storage_delete(storage_handle_t handle, const char *key)
{
    if (handle == NULL || key == NULL) {
        return -1;
    }

    storage_context_t *ctx = (storage_context_t *)handle;

    esp_err_t ret = nvs_erase_key(ctx->nvs_handle, key);
    return (ret == ESP_OK) ? 0 : -1;
}

/**
 * @brief 提交写入实现
 */
int storage_commit(storage_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }

    storage_context_t *ctx = (storage_context_t *)handle;

    esp_err_t ret = nvs_commit(ctx->nvs_handle);
    return (ret == ESP_OK) ? 0 : -1;
}
