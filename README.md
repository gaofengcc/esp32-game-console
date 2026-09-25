# ESP32-S3 游戏机 P1 固件

本工程面向 `ESP32-S3 N16R8`，P1 交付范围为：

- ILI9488 横屏显示：`480x320`，SPI 40 MHz。
- XPT2046 电阻触摸：软件 SPI，保留校准流程。
- GPIO1（ADC1_CH0）单路 AD 五键：单键识别、自动标定、NVS 持久化、短按/长按/连发事件。
- LVGL v9 按键测试页和标定页。

本阶段不包含游戏逻辑、WiFi、OTA、`diag_service`，也不执行任何烧录或实机操作。

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
3. 仅合并以下三个区间，生成不含 `otadata` 的单文件包：
   - `0x0000`：bootloader
   - `0x8000`：partition-table
   - `0x10000`：factory app
4. 输出每个 bin 的绝对路径、字节数和 SHA256，并写入 `output/manifest.json`。

脚本会拒绝发布覆盖到 NVS 起始地址 `0x500000` 的 merged bin。这样可以避免 `esptool merge_bin` 对未指定区间填充 `0xFF` 时擦除 NVS。**不要把 `otadata`、空白区间或旧的双 OTA 镜像加入合并命令。**

## 分区

`partitions_game.csv` 使用单 factory app：

| 分区 | 偏移 | 大小 | 用途 |
|---|---:|---:|---|
| `factory` | `0x10000` | `0x4F0000` | P1 固件 |
| `nvs` | `0x500000` | `0x6000` | AD 按键标定、后续游戏存档 |
| `phy_init` | `0x506000` | `0x1000` | PHY 初始化数据 |
| `storage` | `0x510000` | `0xAF0000` | 后续资源/文件系统预留 |

P1 不使用 `otadata`，也不使用 OTA 双 app 分区。NVS 放在 app 之后，且 merged 包最高地址受脚本保护。

## 烧录命令（仅供后续阶段）

本阶段禁止烧录。后续在确认硬件和串口后，可使用独立文件烧录：

```bash
idf.py -p PORT flash
```

或者使用单文件包，从 `0x0` 开始：

```bash
python -m esptool --chip esp32s3 --port PORT write_flash 0x0 output/esp32_game_console_merged.bin
```

不要调用 Win10 串口桥的 `/api/flash`、`/api/reboot`，不要访问 `192.168.100.12`。

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

当前已完成编译和发布包生成；最终产物的绝对路径、文件大小和 SHA256 记录在 `.agent-sync/current.md` 与 `output/manifest.json`。当前不做实机烧录。
