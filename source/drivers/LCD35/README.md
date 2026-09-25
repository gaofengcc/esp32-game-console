# LCD_Drivers / esp32_ILI9488

ESP32 平台 ILI9488 (320x480) LCD + XPT2046 触摸驱动, 分层架构便于移植.

本分支 `esp32_ILI9488` 为 ESP32-S3 平台实现.

## ESP-IDF 使用

作为 git submodule 置于 `source/drivers/LCD35`:

```cmake
list(APPEND EXTRA_COMPONENT_DIRS "source/drivers/LCD35")
```

业务组件: `REQUIRES LCD35`.

克隆指定分支:

```bash
git submodule update --init --remote source/drivers/LCD35
```

## 引脚配置

编辑 `include/lcd_config.h` 与 `include/touch_config.h`.
