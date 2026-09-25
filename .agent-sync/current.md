# ESP32-S3 游戏机 P1 交接摘要

更新时间：2026-09-25（codex 交付 + Hermes 实测验收）

## 已完成

- 发布脚本 `build_esp32.sh` 默认选择 `/home/gaofeng/esp/esp-idf-v5.3.5`，支持 `IDF_PATH` 覆盖。
- 新增 `partitions_game.csv`：单 `factory` app，保留 `nvs`，不定义 `otadata`。
- NVS 放在 `0x500000`，位于 app 末端之后；脚本拒绝生成达到该地址的 merged bin，防止 `merge_bin` 的 `0xFF` 填充擦除 NVS。
- merged bin 只合并 bootloader、partition-table、factory app，不包含 `ota_data_initial.bin`，也不写入 `otadata` 分区区间。
- `output/manifest.json` 记录各产物绝对路径、字节数和 SHA256。
- README 已记录构建/后续烧录命令、引脚、AD 标定、事件参数、可调 GPIO/衰减/键数，以及本阶段禁止实机操作的约束。
- 已在 ESP-IDF v5.3.5 / ESP32-S3 完成总构建与 merged 导出。

## 最终产物（2026-09-25 23:57 实测核对，正确值以本节为准）

- `/home/gaofeng/code/esp32-game-console/output/bootloader.bin`：21568 bytes，SHA256 `695d3e6143833ac238eb9973b0e9c3b257a9c6ee20aa64e4a48bc3236e6abad4`
- `/home/gaofeng/code/esp32-game-console/output/partition-table.bin`：3072 bytes，SHA256 `258033a541f09c71f12b0456b6083f5ee86efd340c1d0211a369eb708c863935`
- `/home/gaofeng/code/esp32-game-console/output/esp32_game_console.bin`：684256 bytes，SHA256 `cdc4d9ab7bad06e1da5ea13856b6345e4be49132327f3c0e999cfe86677c72f7`
- `/home/gaofeng/code/esp32-game-console/output/esp32_game_console_merged.bin`：749792 bytes，SHA256 `4881483d477a87e89337278e5ab3f20cffd658224f624bb18daf234416f6d8b3`

> ⚠️ 本文档早先版本记录的 SHA256（bootloader `dcb76e98…`、app `3a663000…`、merged `3d127ffd…`）来自 23:52 之前的一轮构建，23:57 产物被重新生成后已失效，已按实测值更正。

## Hermes 实测验收（2026-09-25）

| 检查项 | 结果 |
|---|---|
| 分区表反解析（`gen_esp32part.py output/partition-table.bin`） | ✅ factory/app@0x10000 5056K、nvs@0x500000 24K、phy_init@0x506000 4K、storage/spiffs@0x510000 11200K，**无 otadata** |
| 烧录地址（`flasher_args.json`） | ✅ 只含 bootloader@0x0、partition-table@0x8000、app@0x10000 |
| merged 包大小 | ✅ 749792 B ≪ 0x500000，不会波及 NVS |
| 屏幕分辨率 | ✅ `DISP_H_RES 480 / DISP_V_RES 320`（横屏） |
| 初始化顺序（main.c） | ✅ nvs → ad_keys → lvgl_port → game_ui |
| AD 按键参数 | ✅ `AD_KEYS_COUNT 5`、`AD_KEYS_GPIO 1`(ADC1_CH0)、中值窗 5、去抖 25ms |
| 参考工程保护 | ✅ `~/code/Nas-assistant` 今日 20:00 后无任何文件改动 |
| 固件 SHA256 一致性 | ✅ manifest.json 与实测 sha256sum 一致 |

## 待处理（P2 一并做）

- `source/idf/lvgl_port/` 仍保留 NAS 遗留入口：`s_deferred_ui_init = nas_ui_create_dashboard`（现为最小首页桩，仅显示标题）+ 兼容函数 `nas_ui_update_data()`。P2 应改名为 game 侧首页入口，清掉 NAS 命名。
- 本阶段未烧录、未接串口桥，显示/触摸/AD 标定均**未经实机验证**（用户决定先走 PC 模拟器验证画面与功能）。
- P2 目标：贪吃蛇（默认穿墙）+ PC 端 LVGL 模拟器（SDL2，LVGL 9.5.0 commit 85aa60d）共用同一份游戏/UI 代码。

## 遗留风险

- merged 包不应在 app 二进制异常膨胀到 `0x500000` 以上时发布；脚本会直接失败。
- 本分区表为单 factory 方案，后续若引入 OTA 必须重新设计分区和发布流程，不能直接恢复旧的 `otadata` 合并逻辑。
- codex 本次执行被 2400s 超时终止（exit 124），代码与构建已完成，收尾（提交、文档哈希更正）由 Hermes 接管。

## P2 贪吃蛇交接摘要（2026-09-26）

### 做了什么

- 新增纯 C 逻辑层 `source/game/snake_logic.c/.h`：30x20 棋盘、方向/暂停输入、反向忽略、吃食物计分、每 5 个食物提速 10ms（最低 100ms）、穿墙开关、撞墙/撞自己结束、暂停和确定性食物生成。
- 最高分采用函数指针存储抽象：设备端 `game_ui.c` 使用 NVS namespace `game` / key `high_score`；PC 模拟器使用本地文件。
- 重写 `source/idf/game_ui/game_ui.c` 为菜单、游戏、暂停、结束页；游戏中只消费按键事件，LVGL 更新通过 `lvgl_port_call()` 在 LVGL 任务上下文执行；棋盘格状态只更新变化格子。
- 清理 `lvgl_port` 的 NAS 遗留入口，改为 `game_home_create()`，同步更新 CMake 和头文件。
- 新增 `simulator/` CMake 工程，使用本地复制的 LVGL 9.5.0、SDL2 和共享逻辑；支持 `--scene menu|game|end`、`--keys`、`--steps`、`--shot`、`--selftest`，`SDL_VIDEODRIVER=dummy` 可无头运行。

### 实测证据

- 逻辑层：`cc -std=c11 -Wall -Wextra -Werror -Isource/game -c source/game/snake_logic.c` 通过。
- 模拟器：`cmake -S simulator -B simulator/build ... && cmake --build simulator/build -j2` 通过。
- `SDL_VIDEODRIVER=dummy simulator/build/snake_sim --selftest` 全部 PASS：前进、反向输入、强制食物、吃食物计分、每 5 个提速、暂停、穿墙开/关、撞自己、最高分写入/读取。
- 截图：
  - `/home/gaofeng/code/esp32-game-console/output/sim/menu.png`
  - `/home/gaofeng/code/esp32-game-console/output/sim/game.png`
  - `/home/gaofeng/code/esp32-game-console/output/sim/end.png`
- 固件：`./build_esp32.sh` 通过，且 merged 包仍不含 `otadata`，大小小于 NVS 起始地址 `0x500000`。
- 本轮固件产物：
  - `/home/gaofeng/code/esp32-game-console/output/esp32_game_console.bin`：671712 bytes，SHA256 `d5f6e0d8e8fa117c48b3a066c6ffe17402abfa3c53b0b427f2b7b16b8c85db7e`
  - `/home/gaofeng/code/esp32-game-console/output/esp32_game_console_merged.bin`：737248 bytes，SHA256 `8474b8ca7205c08795804a50891b81a9aa2ab05a061dd4493e1cb6a3eda1bb17`

### 遗留风险与下一步

- 尚未烧录或连接真实设备，触摸校准、AD 阈值和实际 LCD 局部刷新吞吐仍需后续台架验证。
- PC 模拟器使用软件 framebuffer 截图，设备端仍通过 LCD35 驱动输出。
- 若后续修改棋盘宏或格子像素，需要同时保证棋盘尺寸与 `480x320` 屏幕匹配。
