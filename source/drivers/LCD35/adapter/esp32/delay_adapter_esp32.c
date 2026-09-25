/**
 * @file delay_adapter_esp32.c
 * @brief ESP32平台延时适配器实现
 *
 * 使用FreeRTOS的vTaskDelay实现延时
 *
 * @copyright Copyright (c) 2024
 */

#include "delay_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

/**
 * @brief 微秒级延时实现
 *
 * 使用ESP32 ROM函数实现高精度微秒延时
 */
void delay_us(uint32_t us)
{
    if (us > 0) {
        esp_rom_delay_us(us);
    }
}

/**
 * @brief 毫秒级延时实现
 *
 * 使用FreeRTOS任务延时实现毫秒级延时
 */
void delay_ms(uint32_t ms)
{
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}
