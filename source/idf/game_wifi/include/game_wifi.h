/**
 * @file game_wifi.h
 * @brief 游戏机最小 WiFi STA 管理组件。
 *
 * 该组件只使用 Kconfig 中的编译期凭据，不包含 NVS 配网流程。
 */

#ifndef GAME_WIFI_H
#define GAME_WIFI_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief STA 成功获得 IPv4 地址时调用的回调。
 *
 * 回调在 ESP-IDF 事件任务中执行，不应长时间阻塞。
 */
typedef void (*game_wifi_ip_callback_t)(const char *ip_addr, void *ctx);

/**
 * @brief 启动编译期配置的 WiFi STA。
 *
 * 初始化网络接口、WiFi 驱动和断线重连任务；不会等待关联或 DHCP 完成。
 * 当 GAME_CONSOLE_ENABLE_WIFI 关闭时，该函数不初始化任何 WiFi 资源并返回
 * ESP_OK。
 */
esp_err_t game_wifi_start(void);

/**
 * @brief 查询当前 STA IPv4 地址。
 *
 * @param out_ip 输出缓冲区
 * @param len 输出缓冲区大小，至少 16 字节
 * @return ESP_OK 表示已有地址；未连接时返回 ESP_ERR_INVALID_STATE
 */
esp_err_t game_wifi_get_ip4(char *out_ip, size_t len);

/**
 * @brief 查询当前是否已获得有效 IPv4 地址。
 */
bool game_wifi_is_connected(void);

/**
 * @brief 注册或取消注册“拿到 IP”回调。
 *
 * 传入 NULL 回调即可取消注册。ctx 原样传给回调。
 */
void game_wifi_register_ip_callback(game_wifi_ip_callback_t callback, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
