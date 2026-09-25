# LCD35 Driver CMakeLists.txt
# ILI9488 LCD + XPT2046 Touch Driver
#
# 平台相关源文件位于 adapter/<platform>/ 与 hal/<platform>/ 下,
# 接入新芯片时新增对应目录并在下方 SRCS 中切换或追加.

idf_component_register(
    SRCS
        # Adapter layer - ESP32
        "adapter/esp32/delay_adapter_esp32.c"
        "adapter/esp32/gpio_adapter_esp32.c"
        "adapter/esp32/spi_adapter_esp32.c"
        "adapter/esp32/storage_adapter_nvs.c"
        # HAL layer - ESP32
        "hal/esp32/lcd_hal_esp32.c"
        "hal/esp32/touch_hal_esp32.c"
        # Core layer (platform independent)
        "core/lcd_core.c"
        "core/lcd_font.c"
        "core/touch_core.c"
        # Driver API layer
        "core/lcd_driver.c"
        "core/touch_driver.c"
    INCLUDE_DIRS
        "include"
        "adapter"
        "hal"
        "core"
    PRIV_REQUIRES
        driver
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_spi
        esp_timer
        nvs_flash
        freertos
        log
)
