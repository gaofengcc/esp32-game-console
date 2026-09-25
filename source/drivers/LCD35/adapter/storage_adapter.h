/**
 * @file storage_adapter.h
 * @brief 存储适配器接口定义
 *
 * 提供跨平台的非易失性存储抽象,用于存储校准数据等
 *
 * @copyright Copyright (c) 2024
 */

#ifndef STORAGE_ADAPTER_H
#define STORAGE_ADAPTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 存储区域句柄
 */
typedef void* storage_handle_t;

/**
 * @brief 初始化存储子系统
 *
 * @param namespace 存储命名空间(用于区分不同模块的数据)
 * @return 存储句柄,失败返回NULL
 */
storage_handle_t storage_init(const char *namespace);

/**
 * @brief 释放存储资源
 *
 * @param handle 存储句柄
 */
void storage_deinit(storage_handle_t handle);

/**
 * @brief 写入数据到存储
 *
 * @param handle 存储句柄
 * @param key 键名
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return 0: 成功, 其他: 错误码
 */
int storage_write(storage_handle_t handle, const char *key, const uint8_t *data, size_t len);

/**
 * @brief 从存储读取数据
 *
 * @param handle 存储句柄
 * @param key 键名
 * @param data 数据缓冲区
 * @param len 缓冲区长度
 * @return 实际读取的字节数,失败返回负值
 */
int storage_read(storage_handle_t handle, const char *key, uint8_t *data, size_t len);

/**
 * @brief 检查键是否存在
 *
 * @param handle 存储句柄
 * @param key 键名
 * @return true: 存在, false: 不存在
 */
bool storage_exists(storage_handle_t handle, const char *key);

/**
 * @brief 删除指定键的数据
 *
 * @param handle 存储句柄
 * @param key 键名
 * @return 0: 成功, 其他: 错误码
 */
int storage_delete(storage_handle_t handle, const char *key);

/**
 * @brief 提交写入操作(确保数据写入非易失性存储)
 *
 * @param handle 存储句柄
 * @return 0: 成功, 其他: 错误码
 */
int storage_commit(storage_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_ADAPTER_H */
