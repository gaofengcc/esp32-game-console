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

## P2 关键缺口修复（2026-09-26）

### 本次改动

- `source/idf/game_ui/include/game_ui_port.h` 收敛日志、NVS 最高分、LVGL 调度、触摸校准、周期任务和 `ad_keys_event_t` 回调入口。
- `source/idf/game_ui/game_ui_port_esp.c` 转发到 `esp_log`、NVS、`lvgl_port_call`、FreeRTOS 和 `ad_keys_set_event_callback`。
- `simulator/sim_port.c/.h` 提供本地最高分文件、printf 日志、直接 LVGL 调用、无头时间推进和按键注入。
- `simulator/CMakeLists.txt` 共编译真实设备 UI：`../source/idf/game_ui/game_ui.c`、`sim_port.c`、`../source/game/snake_logic.c`、`lv_font_cjk.c`、`lv_font_cjk_ui.c`。
- `game_ui.c` 不含 `SIMULATOR` 分支；模拟器和设备渲染同一份 UI。本地 Source Han Sans SC 子集生成真实 `lv_font_cjk_20`/`lv_font_cjk_28`，正文/标题不做像素缩放。
- 修复按钮 label 更新对象错误，并加入背景、边框、按下状态反馈。

### 布局取舍

- 顶部新增独立 32px 状态栏，棋盘移到 `y=32`，尺寸为 `480x288`，即 `30x18` 格、每格 16px。
- 代价是从原 `30x20` 减少 2 行棋盘；这是为避免状态栏遮挡棋子、保证 480x320 像素对齐做的可玩性取舍，后续如确认必须保留 20 行需重新讨论屏幕布局。

### 验证证据

- 模拟器可执行文件：
  - `/home/gaofeng/code/esp32-game-console/simulator/build/snake_sim`
  - `/home/gaofeng/code/esp32-game-console/simulator/build/snake_test`
- `SDL_VIDEODRIVER=dummy ./simulator/build/snake_test`：
  `PASS menu_visible`、`PASS menu_to_start`、`PASS pause`、`PASS resume`、`PASS force_food`、`PASS eat_food_score`、`PASS self_collision_game_over`、`PASS retry`、`PASS end_to_menu`。
- `nm -C simulator/build/snake_sim | grep game_ui` 可见 `game_ui_init`、`game_ui_update`、`game_ui_render_menu/game/game_end` 及 `game_ui_port_*` 符号，证明真实 UI 已进入链接。
- 五张 480x320 截图：
  - `/home/gaofeng/code/esp32-game-console/output/sim/p2_01_menu.png`：首页菜单、28px 标题、20px 按钮/正文和可见按钮样式。
  - `/home/gaofeng/code/esp32-game-console/output/sim/p2_02_game.png`：游戏进行中，顶部状态栏与 30x18 棋盘分离。
  - `/home/gaofeng/code/esp32-game-console/output/sim/p2_03_ate.png`：吃到食物后分数显示 10。
  - `/home/gaofeng/code/esp32-game-console/output/sim/p2_04_paused.png`：暂停提示 `已暂停 K5继续`。
  - `/home/gaofeng/code/esp32-game-console/output/sim/p2_05_end.png`：结束页、分数/最高分和两个可见按钮。
- 固件构建：`IDF_PATH=/home/gaofeng/esp/esp-idf-v5.3.5 ./build_esp32.sh` 通过。
  - `/home/gaofeng/code/esp32-game-console/output/esp32_game_console.bin`：688128 bytes，SHA256 `4dabfb507051e7047024ccc571b5da181d956af3b07a4b786809d015aec88eac`
  - `/home/gaofeng/code/esp32-game-console/output/esp32_game_console_merged.bin`：753664 bytes，SHA256 `7bf3a84ff97caa826252e55ba3516373c6868c05f68b917d8ce4ed798468c600`
  - merged 仅含 bootloader、partition-table、factory app，无 `otadata`，大小远小于 NVS 起始地址 `0x500000`。

### 遗留风险

- 未烧录、未接 Win10 串口桥，LCD 实机刷新、触摸校准和 AD 电压阈值仍未做硬件验证。
- UI 字库是本地 Source Han Sans SC 的文案子集；后续新增中文文案必须同步扩展字库生成脚本/资源。
- `game_ui_force_food`、`game_ui_force_self_collision` 是仿真/冒烟测试辅助接口，若后续要收紧设备公共 API，可迁移到独立测试适配层。

## P2 儿童向 UI 最终验收（2026-09-26）

### 模拟器与测试

- 棋盘保持 `30x18`、每格 `16px`，屏幕保持 `480x320`。
- `cmake -S simulator -B simulator/build -DCONFIG_LV_BUILD_DEMOS=OFF -DCONFIG_LV_BUILD_EXAMPLES=OFF`
- `cmake --build simulator/build -j2`：通过。
- `SDL_VIDEODRIVER=dummy ./simulator/build/snake_test --selftest`：20 项全 PASS（逻辑 11 项 + UI 冒烟 9 项）。
- 完整输出记录：`/home/gaofeng/code/esp32-game-console/output/sim/snake_test_final.log`。
- 逻辑测试覆盖：前进、反向忽略、强制食物、吃食物计分、每 5 个食物提速、暂停、穿墙开、穿墙关、撞墙、撞自己、最高分读写。
- 方向验收场景：`head_right`、`head_up`、`head_down`、`head_left`；左转场景按 `START→K1→推进1步→K3→推进1步` 注入。

### 真实模拟器截图（均为 480x320）

- `/home/gaofeng/code/esp32-game-console/output/sim/accept_menu.png`
- `/home/gaofeng/code/esp32-game-console/output/sim/accept_game.png`
- `/home/gaofeng/code/esp32-game-console/output/sim/accept_ate.png`
- `/home/gaofeng/code/esp32-game-console/output/sim/accept_paused.png`
- `/home/gaofeng/code/esp32-game-console/output/sim/accept_end_self.png`
- `/home/gaofeng/code/esp32-game-console/output/sim/accept_head_directions.png`

### 最终设备构建产物

- `/home/gaofeng/code/esp32-game-console/output/esp32_game_console.bin`：689552 bytes，SHA256 `5d6a71719847ee863559ff449c22eeadfbc10f7faafb21ff4fdb83874c587056`
- `/home/gaofeng/code/esp32-game-console/output/esp32_game_console_merged.bin`：755088 bytes，SHA256 `f71dfdb7733f00095c54f06ef6d5738b286fa3c144ff11c4e1a62db01bb7e798`
- merged 仅包含 bootloader、partition-table、factory app，不包含 `otadata`，大小远小于 NVS 起始地址 `0x500000`。

### 保护约束

- 未烧录、未调用 Win10 串口桥，未修改 `Nas-assistant`、`esp32-lab-bridge` 或 `LCD_Drivers`。

### P3 视觉与刷新补充

| 颜色宏 | 修改前 | 修改后 |
|---|---:|---:|
| `COLOR_GRID` | `0x1D3038` | `0x141E26` |
| `COLOR_SNAKE_HEAD` | `0x66BB6A` | `0xAEEA00` |
| `COLOR_SNAKE_BODY` | `0x2E8B57` | `0x2E8B57` |
| `COLOR_FOOD` | `0xEF5350` | `0xEF5350` |
| 新增 `COLOR_EYE` | - | `0x18332B` |
| 新增 `COLOR_FOOD_GLINT` | - | `0xFFF8E1` |

- 键位提示宏位于 `source/idf/game_ui/game_ui.c` 顶部：`GAME_UI_KEY1_TEXT`、`GAME_UI_KEY2_TEXT`、`GAME_UI_KEY3_TEXT`、`GAME_UI_KEY5_TEXT`。
- `game_ui.c` 保持局部刷新：运行中只比较并重绘变化格子；状态栏分数/最高分仅在数值变化时更新；新局/重试才做一次棋盘全量失效。
- 吃食物动画只作用于单个得分标签，时长 `130ms + 130ms = 260ms`，没有整屏动画。

## P5 像素苹果食物交接（2026-09-26）

### 实现

- `source/idf/game_ui/game_ui.c` 使用单个 `lv_image_create()` 食物对象，替换原纯红格子 + 独立高光对象。
- 食物资源为 `lv_image_dsc_t` / `LV_COLOR_FORMAT_RGB565`：
  - `w=16`、`h=16`、`stride=32`、`data_size=512`
  - `source/idf/game_ui/assets/apple_16x16_a.h`：A 经典红苹果（默认）
  - `source/idf/game_ui/assets/apple_16x16_b.h`：B 卡通亮眼版
  - `source/idf/game_ui/assets/apple_16x16_c.h`：C 简洁版
- 食物对象只在坐标变化时调用 `lv_obj_set_pos()`；食物格状态仍走原有局部状态比较，底格回到 `COLOR_BG`，没有逐帧/整屏重绘。
- `lv_image_set_antialias(..., false)` 保证像素边缘不被平滑。

### 生成脚本与切换

- 脚本：`/home/gaofeng/code/esp32-game-console/tools/generate_apples.py`
- 用法：`python3 tools/generate_apples.py`
- 默认宏位于 `source/idf/game_ui/game_ui.c`：
  - `GAME_UI_APPLE_STYLE=0`：A（默认）
  - `GAME_UI_APPLE_STYLE=1`：B
  - `GAME_UI_APPLE_STYLE=2`：C
  - 例如构建时追加 `-DGAME_UI_APPLE_STYLE=1` 即切换到 B。

### RGB565 字节序

- 脚本按 native RGB565 little-endian 输出每个像素（低字节在前），头文件使用 `LV_COLOR_FORMAT_RGB565`，不是 `RGB565_SWAPPED`。
- `source/components/lv_conf.h` 和模拟器 LVGL 配置均未启用 `LV_COLOR_16_SWAP`；SDL 模拟器截图实测苹果为正常红色、绿色叶片和棕色果柄，没有红绿/红蓝颠倒。

### 交付截图

- `/home/gaofeng/code/esp32-game-console/output/sim/apple_candidates.png`：480x320，A/B/C 三列，每个 6x 最近邻放大并附 1:1 16x16 版本。
- `/home/gaofeng/code/esp32-game-console/output/sim/apple_ingame.png`：480x320 真实 `game` 场景，包含状态栏、30x18 棋盘、蛇和默认 A 苹果。
- 单候选预览：`output/sim/apple_A_classic_1x.png`、`output/sim/apple_B_bright_1x.png`、`output/sim/apple_C_simple_1x.png`。

### 验收

- `cmake --build simulator/build -j2`：通过。
- `SDL_VIDEODRIVER=dummy ./simulator/build/snake_test --selftest`：20 项全 PASS（逻辑 11 + UI 9），完整输出见 `/home/gaofeng/code/esp32-game-console/output/sim/snake_test_final.log`。
- `./build_esp32.sh`：通过（ESP-IDF `/home/gaofeng/esp/esp-idf-v5.3.5`，本地工具链 13.2.0）。
- 固件产物：
  - `/home/gaofeng/code/esp32-game-console/output/esp32_game_console.bin`：690160 bytes，SHA256 `dcfc04edfabe024259c76f4778682f3db50d8203e1c844a0b57ccfc0745dd1d2`
  - `/home/gaofeng/code/esp32-game-console/output/esp32_game_console_merged.bin`：755696 bytes，SHA256 `c29b2af6354da66eb4756636314ba4e1415c0ef77b0d62939f0f73a2fc5be89c`
  - `/home/gaofeng/code/esp32-game-console/output/bootloader.bin`：21568 bytes，SHA256 `17a72d10a871ec289b456e2c51d237123c7eb65e9afe2d2ba94ae43e9579a784`
  - `/home/gaofeng/code/esp32-game-console/output/partition-table.bin`：3072 bytes，SHA256 `258033a541f09c71f12b0456b6083f5ee86efd340c1d0211a369eb708c863935`
- 未烧录、未调用 Win10 串口桥，未修改 `Nas-assistant`、`esp32-lab-bridge` 或 `LCD_Drivers`。

## P8 验收（Hermes 补记，2026-09-26 14:20）

> codex 本轮跑到 2700s 上限被系统杀掉（exit 124），未及写交接，本节由 Hermes 实测补记。

### 提交（7 笔，分笔落实）
```
4c3ae24 feat: add wiggle amplitude comparison assets
fa86977 fix: prefer psram for screenshots
0a9d20a fix: close screenshot work item race
31cd5f6 close snake tail connection seam
59bcc55 fix snake seam evidence path
603f02d fix snake seam-free segment assets
063ce84 fix: screenshot thread safety
```

### 1) 截图线程安全修复（Hermes 实测通过）
- 新增 `lvgl_port_capture_bmp(uint8_t **, size_t *, uint32_t timeout_ms)`（`lvgl_port.h:73` / `lvgl_port.c:1101`）
- 机制：当前任务 == LVGL 任务 → **直接执行**（避免自投递死锁）；否则 `lvgl_port_call_internal()` 投递 + 超时 + cleanup 回调
- `main.c:195` 改为调用新接口（timeout 1500ms）；**原先那句虚假注释已删除**
- 截图缓冲区优先 PSRAM（`fa86977`）
- ⚠️ **缺口：P8 任务书要求的"新增截图自测"未交付**（simulator 自测仍为 22 项，无 BMP 校验项）。即"跨任务投递路径"目前只有代码审查证据，**无自动化验证**。真机首次验证时请优先测 `/api/screenshot.bmp`。

### 2) 蛇身节间细缝修复
- 体节图左右不留描边，相邻节无缝；尾部衔接一并处理（`31cd5f6`）
- 证据：`output/sim/snake_seam_zoom.png`（放大 4 倍拼接特写，Hermes 目视确认无 1px 断裂）

### 3) 扭动幅度三档实测数据（关键结论）
| 振幅 | 平均重绘/帧 | 屏面积占比 |
|---|---|---|
| 2px | 8,252 px | 5.37% |
| 3px | 8,272 px | 5.39% |
| 4px | 8,296 px | 5.40% |

**结论：振幅对重绘开销几乎无影响**（开销由 LVGL flush 分块 ~8192px 主导，不是蛇身尺寸决定）。
40MHz SPI 下每帧 ≈16.6KB ≈ **3.3ms**，20Hz 扭动更新占空比 ≈6.6% → **选 4px 也完全安全**。
→ 振幅选择**纯属观感问题，不是性能问题**。默认值仍为 2px，等用户选定后一行宏即可切换。

### 构建（Hermes 自行重跑）
```
IDF_PATH=/home/gaofeng/esp/esp-idf-v5.3.5 ./build_esp32.sh   # 通过
esp32_game_console.bin 1362544 bytes  sha256=625d816aa20862d650383855f5f5b7bc9e6f3d1a2751439a99bf7808c9e38e55
partition-table.bin    3072 bytes     sha256=6bc0d8697425bf7d469976984c9a127f85b0b80983dd77fc27f47445756f9906
bootloader.bin         21632 bytes    sha256=a66c6b2bc83778f4a61dc9e4ef8a008fc9352463d0dc8c5c72189a0fec10e123
ota_data_initial.bin   8192 bytes     sha256=7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f
```
模拟器自测 22 项全 PASS（未破坏游戏逻辑）。
