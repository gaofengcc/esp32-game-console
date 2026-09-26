# ESP32 游戏机 — 实时状态看板

> 自动刷新（每 30 秒）· 最后更新 **2026-09-26 11:33:00**（北京时间）

## 当前任务
- 状态：⚪ 空闲（没有 codex 任务在跑）

## 最近提交
- `b99ab4a` 09-26 09:57 — polish(p4): 儿童向 UI 打磨 — 网格隐化/边界描边/蛇头眼睛随方向/键位提示/暂停遮罩/结束文案
- `c3a32cd` 09-26 00:53 — fix(p3): 模拟器改为共编译真实设备 UI(game_ui.c) + 状态栏独立占位 + 儿童向 UI
- `ec0e66a` 09-26 00:19 — feat(p2): 贪吃蛇游戏(逻辑层 + UI) + PC LVGL/SDL2 模拟器 + 逻辑自测
- `a121fd5` 09-25 23:59 — feat(p1): ILI9488 横屏 + XPT2046 触摸 + AD 单路五键(自动标定/NVS/短按长按连发) + 按键测试页
- `2c65778` 09-25 23:18 — chore: init esp32-game-console skeleton

## 工作区
- 未提交改动：**7** 个文件
- 产物：21568 11:25 output/bootloader.bin
- 产物：690160 11:25 output/esp32_game_console.bin
- 产物：755696 11:25 output/esp32_game_console_merged.bin
- 产物：3072 11:25 output/partition-table.bin
- 交接文档 current.md 最后写入：2026-09-26 11:27:56.230642459 +0800 15611

## 最近截图（output/sim/）
- output/sim/apple_ingame.png
- output/sim/apple_candidates.png
- output/sim/apple_C_simple.png
- output/sim/apple_C_simple_1x.png
- output/sim/apple_B_bright.png
- output/sim/apple_B_bright_1x.png
- output/sim/apple_A_classic.png
- output/sim/apple_A_classic_1x.png

## 事件流（最近 8 条）
- `2026-09-26 11:19:24` [hermes/fix] 发现并修正：P1 交接文档 SHA256 是旧构建值(文档23:52写/产物23:57重建)，已按实测更正
- `2026-09-26 11:19:24` [hermes/dispatch] P2 派发：贪吃蛇(穿墙默认开) + PC LVGL/SDL2 模拟器
- `2026-09-26 11:19:24` [hermes/verify] P2 验收：逻辑自测11项 PASS(hermes重跑)；发现关键缺口=模拟器未链接 game_ui.c，截图为模拟器自绘UI
- `2026-09-26 11:19:24` [hermes/fix] P3 派发：修缺口(模拟器共编译真实设备UI) + 状态栏独立占位(棋盘30x20->30x18) + 儿童向UI
- `2026-09-26 11:19:24` [hermes/verify] P3 验收通过：符号证据 game_ui_* 进入 snake_sim；UI冒烟9项 PASS；merged 753664B 7bf3a84f
- `2026-09-26 11:19:24` [hermes/dispatch] P4 派发：儿童向UI打磨(网格隐化/边界描边/蛇头眼睛随方向/键位提示/暂停遮罩/结束文案)
- `2026-09-26 11:19:24` [hermes/verify] P4 验收通过：自测20项(逻辑11+UI9) hermes重跑全PASS；merged 755088B f71dfdb7；面板工程未被改
- `2026-09-26 11:19:24` [hermes/dispatch] P5 派发：食物改16x16像素苹果(lv_image)，出3个候选风格对比图供用户挑
