# 更新日志

格式参照 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。1.0 之前，次版本号升级可能带来卡数据或配置上的不兼容变化，会在对应条目里写明。

## [Unreleased]

## [0.1.0] - 2026-09-13

首个公开版本：DOS 原版《金庸群侠传》在 M5Stack Tab5（ESP32-P4）上可玩。需要自备原版游戏数据。

### 可用

- 标题、新游戏、大地图与子地图行走、事件、对话、菜单、物品、存读档（真机验证）
- 音效，以及流式播放 host 预渲染 WAV 的背景音乐
- PPA 硬件旋转缩放、DPI 双缓冲；Tab5 键盘套件（I2C）支持热插拔
- 为 32 MB PSRAM 做的内存调度：大地图与子地图互斥、512² 图集、流式存档与读档等，见 `docs/PERF_PLAN.md`
- `prepare_game_data.sh` 一步生成 SD 卡数据：makedata、Tab5 配置、按游戏实际用字子集化的中文字体、BGM 预渲染
- macOS 上的无头测试台 `tools/host_test`
- 预编译的游戏固件与屏 / 键盘 / SD 自检固件

### 已知限制

- 战斗已在测试台验证，真机尚未确认打完一整场
- 结局动画不可用
- 姓名不能输入中文（用 `config.toml` 的 `default_name`）
- 没有触摸操作，必须接键盘套件

[Unreleased]: https://github.com/Octo-o-o-o/m5stack_tab5_jinyong/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Octo-o-o-o/m5stack_tab5_jinyong/releases/tag/v0.1.0
