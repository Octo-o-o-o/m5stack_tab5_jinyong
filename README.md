# tab5_jinyong

把 DOS 原版《金庸群侠传》经 [HeroesOfJinYong](https://github.com/soarqin/HeroesOfJinYong) 的 gameplay/core，移植到 M5Stack Tab5。这是独立娱乐仓，**不是** Octoooo / AID monitor 的附加模式。

当前进度：**M0 文档 + M1 硬件 bring-up 已在本机编过**。还不能玩金庸本体（M2–M4 未做）。默认只编译，不刷机。

```sh
git clone --recurse-submodules https://github.com/Octo-o-o-o/m5stack_tab5_jinyong.git
cd m5stack_tab5_jinyong
./setup.sh
./build.sh
```

## 工具链

| 项 | 值 |
|---|---|
| ESP-IDF | **v5.5.5**（不要用 Arduino） |
| export | `$IDF_EXPORT` 或 `$HOME/.espressif/esp-idf-v5.5.5/export.sh` |
| 目标芯片 | ESP32-P4 |
| 官方 BSP | `espressif/m5stack_tab5_noglib` **1.3.0**（无 LVGL） |
| 语言 | bring-up 用 C；游戏 core 是 C++17（M2+） |
| 许可证 | GPLv3（链接 HeroesOfJinYong / 默认还链 GPLv3 音频库） |

依赖写在 `firmware/bringup/main/idf_component.yml`，版本钉死。`firmware/bringup/dependencies.lock` 随仓库提交。

## 本机 Tab5 已知事实（检测逻辑仍兼容其它屏）

这些只描述**这台**机器，不是「所有 Tab5」：

- ESP32-P4 rev v1.3，16 MB Flash，32 MB PSRAM
- MAC `e8:f6:0a:e2:ec:7d`
- 机身屏驱 **ST7121**（官方还存在 ILI9881C+GT911、ST7123）
- 刷写口只许 `/dev/cu.usbmodem1101`（Espressif `303A:1001` USB JTAG）
- 不要用 `/dev/cu.usbmodem01`
- Secure Boot / Flash Encryption 关
- 键盘套件：STM32 经 **I2C 0x6D**（G0 SDA / G1 SCL / G50 INT），不是 USB-A HID

官方 1.3.0 Registry 能力表没写 ST7121，但该版本源码已按触摸 FW 探测 ST7121。M1 用这份官方 BSP 编译；真机是否一次点亮见 `docs/UPSTREAM_ANALYSIS.md` 风险 1。

## 上游

```
third_party/HeroesOfJinYong  @ 2e65e97f756a9499ddf09eebe12443b94e07e275
```

完整分析：`docs/UPSTREAM_ANALYSIS.md`  
架构：`docs/ARCHITECTURE.md`  
计划：`docs/PORTING_PLAN.md`

## 构建（不刷机）

```sh
./setup.sh
./build.sh
```

产物与完整日志：`firmware/bringup/build/`、`firmware/bringup/last-build.log`。

## 刷机（默认拒绝）

刷本项目会**覆盖** Tab5 上正在跑的工作固件。必须先在对话里明确授权，再：

```sh
TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE=1 ESPPORT=/dev/cu.usbmodem1101 ./flash.sh
./monitor.sh
```

进入下载模式：按住 Reset 约 2 秒，内部绿灯快闪后松开。不要刷 ESP32-C6，不要烧 eFuse。

## microSD 运行时布局（不进 Git）

```
/jinyong/data/
/jinyong/music/
/jinyong/save/
/jinyong/config/
/jinyong/fonts/
```

在 Mac 上预处理：

```sh
./prepare_game_data.sh /path/to/original-game ./local/sd_image/jinyong /path/to/font.otf
```

再把 `local/sd_image/jinyong` 拷到卡上。M1 会尝试读 `/sdcard/jinyong/config/bringup.txt`。

## 画面与输入（已拍板）

- 游戏保持低分辨率；最后 nearest-neighbor 放大
- 原比例、黑边；不默认拉成 16:9，不默认 bilinear
- 第一版不做触摸虚拟键
- 键盘先走 I2C 0x6D

## Troubleshooting

| 现象 | 处理 |
|---|---|
| `export.sh` 找不到 | 安装 IDF 5.5.5，或设 `IDF_EXPORT` |
| `set-target` 与环境不一致 | `unset IDF_TARGET` 后重跑 `./build.sh` |
| 组件下载失败 | 需要能访问 Espressif component registry |
| 刷写口不对 | 只用 `cu.usbmodem1101` |
| 官方 BSP 点不亮 ST7121 | 先看串口 probe 日志；下一步用官方 `esp_lcd_st7121`，不要从其它仓拷 BSP |
| 键盘无事件 | 确认套件插在 Ext.Port1；总线是 G0/G1 不是 G31/G32 |
| SD 失败 | FAT32、接线/卡是否插入；无卡也应继续跑屏和键盘 |
| 想切回工作固件 | 重新刷 Octoooo；本仓不提供双模式热切换 |

## 禁止

不要提交原版/merge 后的游戏资源、存档、字库原件。不要把 Wi-Fi 密码或个人绝对路径写进会提交的文件。

## 现在能做什么 / 还不能做什么

| 现在 | 还不行 |
|---|---|
| clone 后用 IDF 5.5.5 编出 bring-up 固件 | 进标题画面、走地图、战斗、存档 |
| 授权后刷机，看色条/棋盘/4:3 框、I2C 键盘日志、读 SD | 把 Octoooo monitor 和游戏做进同一固件 |
| 在 Mac 上 `prepare_game_data.sh` 准备 SD 树 | 把原版 `.GRP/.IDX/SDX` 推进 Git |

登机前若只带 Tab5 + 键盘 + 已拷数据的卡：**还不能开玩金庸**。缺的是 M2 第一帧 → M3 可玩 → M4 存档/声音，以及一次明确的刷机授权。本仓已经把路线、SHA、脚本和可编译脚手架对齐到 GitHub，下一步是移植而不是再论证架构。
