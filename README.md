# ESP32-S3 贪吃蛇游戏机

本工程面向 `ESP32-S3 N16R8`，当前交付范围为：

- ILI9488 横屏显示：`480x320`，SPI 40 MHz。
- XPT2046 电阻触摸：软件 SPI，保留校准流程。
- GPIO1（ADC1_CH0）单路 AD 五键：单键识别、自动标定、NVS 持久化、短按/长按/连发事件。
- LVGL v9 贪吃蛇菜单、游戏、暂停和结束页。
- PC 端 LVGL 9.5 + SDL2 无头模拟器，可脚本注入按键并导出 PNG/BMP。
- `esp32-lab-bridge v0.2.0` 的 `diag_service`、`ota_update`、`lvgl_screenshot`：
  局域网状态/日志/截图/OTA/重启，Bearer 鉴权。
- 编译期配置的 WiFi STA；拿到 IP 后自动启动诊断服务。

本阶段不包含重力感应；本次代码只做编译、模拟器和静态证据验证，不执行烧录或实机操作。

## lab-bridge 导入

`third_party/esp32-lab-bridge` 是固定到 tag `v0.2.0` 的 Git submodule，根
`CMakeLists.txt` 只注册以下三个组件：

- `wireless/diag_service`
- `wireless/ota_update`
- `firmware/lvgl_screenshot`

没有导入 `log_gate`（它依赖 EasyLogger），也没有注册 `cdc_command`。正常联网环境下：

```bash
git submodule update --init --checkout
git -C third_party/esp32-lab-bridge checkout v0.2.0
```

若在离线环境改用本地镜像，先把 `.gitmodules` 中的 URL 换成本地路径，再执行：

```bash
git submodule sync -- third_party/esp32-lab-bridge
git submodule update --init --checkout
```

恢复远端 URL：

```bash
git submodule set-url third_party/esp32-lab-bridge \
  https://github.com/gaofengcc/esp32-lab-bridge.git
```

## 贪吃蛇架构

- `source/game/` 是纯 C 逻辑层，不依赖 ESP-IDF 或 LVGL；设备端和 PC 端共用。
- `source/idf/game_ui/game_ui.c` 只负责 LVGL 渲染、按键队列和页面切换。
- 最高分通过 `snake_config_t` 的 `load_best/save_best` 函数指针抽象：设备端接 NVS namespace `game`、key `high_score`，PC 端接本地文件。
- 默认棋盘为 `30x18`、每格 `16px`；默认慢速 `260ms/格`、默认穿墙。每吃 5 个食物速度减少 `10ms`，最低 `100ms/格`。
- 实体键映射宏位于 `source/idf/game_ui/game_ui.c`：`GAME_UI_KEY_UP/DOWN/LEFT/RIGHT/PAUSE`，默认对应 K1/K2/K3/K4/K5。
- 修改棋盘大小：调整 `source/game/snake_logic.h` 中的 `SNAKE_BOARD_WIDTH/HEIGHT`；修改格子像素：调整 `GAME_UI_CELL_PX`，并确保总尺寸仍为 `480x320`。

## PC 模拟器

LVGL 9.5.0 源码从本地参考工程复制到 `simulator/third_party/lvgl`，不联网下载。构建和运行：

```bash
cmake -S simulator -B simulator/build \
  -DCONFIG_LV_BUILD_DEMOS=OFF \
  -DCONFIG_LV_BUILD_EXAMPLES=OFF
cmake --build simulator/build -j2
SDL_VIDEODRIVER=dummy simulator/build/snake_sim --selftest
SDL_VIDEODRIVER=dummy simulator/build/snake_sim \
  --scene game --keys "K3,K3,K2" --steps 6 --shot output/sim/game.png
```

`--scene menu|game|end` 可生成首页、进行中和结束页截图；`--shot` 使用 `.png` 时写 PNG，其他扩展名写 BMP。模拟器支持无头运行，不需要显示器。

## 环境与构建

构建脚本默认使用 `/home/gaofeng/esp/esp-idf-v5.3.5`；也可以通过 `IDF_PATH` 显式覆盖：

```bash
cd /home/gaofeng/code/esp32-game-console
IDF_PATH=/home/gaofeng/esp/esp-idf-v5.3.5 ./build_esp32.sh
```

只使用 ESP-IDF 命令时：

```bash
source /home/gaofeng/esp/esp-idf-v5.3.5/export.sh
idf.py set-target esp32s3
idf.py build
```

`build_esp32.sh` 会：

1. 编译 bootloader、分区表和 `esp32_game_console.bin`。
2. 将产物复制到 `output/`。
3. 输出 `bootloader.bin`、`partition-table.bin`、`ota_data_initial.bin`、
   app bin、`flash_args` 和 `output/manifest.json`。
4. 对每个产物输出绝对路径、字节数和 SHA256。

### 重要：禁止 merged 整包烧录

双 OTA 布局之后，脚本**停止生成整包 merged 固件**。

- ✅ 正确烧录：`idf.py -p <PORT> flash`，或按 `output/flash_args` 中的地址分开写入。
- ❌ 禁止把 merged 整包写到 `0x0`。未指定区间会被填成 `0xFF`，会擦掉
  NVS/配网数据；这个坑曾在 Nas 工程中发生过。

本任务不执行烧录，也不调用 Win10 串口桥。

## 分区

`partitions_game.csv` 使用 16MB 双 OTA 布局：

| 分区 | 偏移 | 大小 | 用途 |
|---|---:|---:|---|
| `nvs` | `0x9000` | `0x6000` | AD 标定、最高分、WiFi/NVS 数据 |
| `otadata` | `0xF000` | `0x2000` | 当前 OTA 启动槽 |
| `phy_init` | `0x11000` | `0x1000` | PHY 初始化数据 |
| `ota_0` | `0x20000` | `0x200000` | OTA app 槽 0 |
| `ota_1` | `0x220000` | `0x200000` | OTA app 槽 1 |
| `storage` | `0x420000` | `0xBE0000` | SPIFFS，后续存档/资源 |

## WiFi 与诊断配置

`main/Kconfig.projbuild` 提供：

- `CONFIG_USER_WIFI_SSID`
- `CONFIG_USER_WIFI_PASSWORD`
- `CONFIG_GAME_CONSOLE_ENABLE_WIFI`
- `CONFIG_GAME_CONSOLE_DIAG_PORT`
- `CONFIG_GAME_CONSOLE_DIAG_TOKEN`

STA 只使用编译期配置，不包含 NVS 配网流程；关闭
`CONFIG_GAME_CONSOLE_ENABLE_WIFI` 时完全不初始化 WiFi。凭据和固定 token
属于敏感信息，只能写被 gitignore 的 `sdkconfig` 或
`sdkconfig.defaults.local`，禁止提交。token 留空时启动随机生成并打印到串口。

WiFi 获得 IPv4 后才启动 `diag_service`。游戏 UI 不显示网络/调试信息，只保留串口日志。

## 真机诊断路由速查

下面令牌只作占位，真机启动时从串口日志复制实际 token：

```bash
IP=192.168.1.123
PORT=8080
TOKEN='replace-with-serial-token'
AUTH=(-H "Authorization: Bearer ${TOKEN}")

curl "http://${IP}:${PORT}/api/status" "${AUTH[@]}"
curl "http://${IP}:${PORT}/api/logs" "${AUTH[@]}"
curl -o screenshot.bmp "http://${IP}:${PORT}/api/screenshot.bmp" "${AUTH[@]}"
curl "http://${IP}:${PORT}/api/ota/status" "${AUTH[@]}"
curl -X POST "http://${IP}:${PORT}/api/ota/check?manifest_url=http://host/manifest.json" "${AUTH[@]}"
curl -X POST "http://${IP}:${PORT}/api/ota/start?manifest_url=http://host/manifest.json" "${AUTH[@]}"
curl -X POST "http://${IP}:${PORT}/api/reboot" "${AUTH[@]}"
```

`/api/health` 不需要 Bearer 头；其余路由都需要
`Authorization: Bearer <token>`。OTA manifest URL 也可放在 JSON body：

```bash
curl -X POST "http://${IP}:${PORT}/api/ota/start" \
  "${AUTH[@]}" -H 'Content-Type: application/json' \
  -d '{"manifest_url":"http://host/manifest.json"}'
```

`/api/ota/check` 当前执行 URL/请求格式校验并回显 manifest URL；真正下载、校验、
写入 OTA 槽由 `/api/ota/start` 调用 `ota_update` 完成。

## 引脚表

| 功能 | GPIO |
|---|---:|
| LCD 背光 | 8 |
| LCD DC / CS / MOSI / SCK / MISO / RST | 9 / 10 / 11 / 12 / 13 / 14 |
| XPT2046 CS / IRQ / DO / DIN / CLK | 4 / 5 / 6 / 7 / 15 |
| AD 五键 OUT | 1（ADC1_CH0） |
| UART0 日志 TX/RX | 43 / 44 |
| USB Serial/JTAG | 19 / 20 |
| 预留 | 2 / 16 / 17 / 18 |

AD 输入必须使用 ADC1，衰减默认 12 dB（ESP-IDF v5.3.5 对应 11/12 dB 量程配置），通过 `adc_oneshot` + `adc_cali` 获取校准电压，不能用裸 raw 值作为阈值。

## AD 按键标定与事件

首次启动或 NVS namespace `adkeys` 缺少当前版本标定数据时，进入标定页，依次提示按下 `K1` 到 `K5`。每个键采集多次稳定电压并取中值；根据相邻中心值最小间距的 40% 计算窗口容差，容差下限为 200 mV，结果保存为五组 `min/max` 加空闲电压。

进入重标定：

- 标定页上的“重新标定”触摸按钮。
- 上电后前 1.5 秒内按住任意实体键。

采样任务建议 100 Hz，使用中值滤波、连续稳定次数判据和 25 ms 去抖。事件规则：

- 短按：按下确认后立即触发一次。
- 长按：持续超过 800 ms 触发。
- 连发：长按后每 150 ms 重复一次。

串口日志会节流输出原始校准电压、识别键号和事件类型。单路电阻梯只支持单键识别；组合按键落入未知窗口时报告“未知/无键”。

## 修改硬件参数

- GPIO：修改 `source/idf/ad_keys/` 中的 ADC 通道配置，以及 `source/idf/board/include/board_lcd_pins.h` 中的屏幕/触摸引脚。
- 衰减：修改 ADC 初始化处的 `ADC_ATTEN_DB_12`（或项目对应的 11/12 dB 枚举），同时重新执行标定。
- 按键数量：调整 `AD_KEYS_COUNT`、标定页控件数量、NVS 窗口数组和事件枚举；分压网络仍然只支持单键。

所有 LVGL 对象更新必须通过 `lvgl_port_call()` 或 LVGL 锁在 LVGL 任务上下文执行，ADC 任务只发布数据/事件，不直接触碰 UI。

## 交接状态

当前交接摘要见 [.agent-sync/current.md](.agent-sync/current.md)。可用以下命令复现构建：

```bash
./build_esp32.sh
```

模拟器不会链接 `game_wifi`、`diag_service` 或 OTA 组件；它继续只编译游戏逻辑和 UI。
