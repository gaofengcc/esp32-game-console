# AD 按键组件

本组件针对 ESP32-S3 `GPIO1 / ADC1_CH0` 的单路五键电阻分压模块。

当前 `AD_KEYS_ENABLE_RUNTIME_SAMPLING=0`: 不自动/开机采集键值, 识别走 NVS 合法窗口或出厂实测值. 专用采样页就绪后再打开该宏.

## 使用流程

```c
ad_keys_config_t cfg;
ad_keys_config_default(&cfg);
cfg.event_cb = on_ad_key_event;       // 回调运行在采样任务上下文
ad_keys_init(&cfg);                   // 应用需先完成 nvs_flash_init()
ad_keys_start();
ad_keys_boot_force_calibration_check(1500); // 可选：开机 1.5s 强制标定
```

没有有效 NVS 标定时，组件在建立 20 次启动基线（约 200ms）后自动进入标定。启动基线
按“按下电压下降、空闲电压最高”的物理特性取样本高水位，只向更高电压方向更新；
即使上电瞬间按住按键，释放后也会继续收敛到真实空闲电压。UI 轮询
`ad_keys_get_status()`，根据 `calibrating` 与 `calibration_index` 显示“请按 K1…K5”，
实体键按下后组件依次保存五个中心电压并计算窗口。

页面上的“重新标定”按钮调用 `ad_keys_request_calibration()`。事件回调只应投递到队列，
LVGL 对象必须在 LVGL 任务中更新。

开机强制标定检测建议在 `ad_keys_start()` 后立即调用
`ad_keys_boot_force_calibration_check(1500)`；强制窗口会等待真实启动基线建立，
不会使用固定 3.3V 猜值。

## 默认参数

- 采样周期：10ms（约 100Hz）
- 中值滤波：默认 5 个历史样本（可配置 3/5/7，最多 7 个）
- 去抖：25ms
- 长按：800ms；连发：150ms
- 标定有效偏离：相对空闲电压至少 160mV（配置值低于该安全下限时仍按 160mV）
- 标定窗口：中心值 ± `max(相邻中心最小间距 * 40%, 200mV)`
- 周期 ADC 日志：默认 100ms，电压变化至少 100mV、键状态变化或事件时打印
- 静默心跳：默认 30000ms；定义 `AD_KEYS_LOG_HEARTBEAT_MS=0` 可关闭

标定数据保存在 NVS namespace `adkeys`，blob 版本为 1，包含空闲电压、五个中心电压
及五组 `min_mv/max_mv`。超出所有窗口的电压报告为未知/无键（键号 0）。
