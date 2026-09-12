# 早期脚手架计划（历史）

立项时按 M0–M5 拆过。**当前能做什么以仓库根 [README](../README.md) 为准**，不要用下面的条目当进度，也不要再用「还不能进标题 / 缺 M2 第一帧 → M3 → M4」那种表。

工具链仍是 ESP-IDF **v5.5.5**（不要用 Arduino）。`export.sh` 由 `IDF_EXPORT` 或 `$HOME/.espressif/esp-idf-v5.5.5/export.sh` 提供。刷机默认拒绝，需 `TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE=1`。口必须是 Espressif USB-JTAG（`303A:1001`）。

## 当时的拆分

| 当时叫法 | 当时想验的事 | 现在 |
|---|---|---|
| M0 | 文档、上游 SHA、内存初估 | 分析还在 `UPSTREAM_ANALYSIS.md`；数字可能过期 |
| M1 | `firmware/bringup/`：色条、I2C 键盘、SD | 自检固件仍在，不是游戏 |
| M2 | 第一帧游戏画面 | 已过；标题已在真机跑过 |
| M3 | 开局、走路、对话、菜单 | 已过；真机可用 |
| M4 | 存读档、SFX/BGM | 存读档真机可用；BGM 是 host 预渲染 WAV 流式播放，不链 ADLMIDI。战斗测试台已验，真机待确认 |
| M5 | launcher / 电量 / 亮度 / 安全关机 | 多数还没做；这是游戏固件自己的事 |

`firmware/bringup/` 排查屏和键盘：

```sh
TAB5_FIRMWARE=bringup ./build.sh
```

编游戏、备卡、刷机步骤见根 README，不要再把「拷卡 / 刷机」写成还没做过的下一步。
