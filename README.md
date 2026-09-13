# tab5_jinyong

[![build](https://github.com/Octo-o-o-o/m5stack_tab5_jinyong/actions/workflows/build.yml/badge.svg)](https://github.com/Octo-o-o-o/m5stack_tab5_jinyong/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/Octo-o-o-o/m5stack_tab5_jinyong)](https://github.com/Octo-o-o-o/m5stack_tab5_jinyong/releases/latest)
[![license](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)

中文 | [English](README.en.md)

把 DOS 原版《金庸群侠传》移植到 [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5)（ESP32-P4，32 MB PSRAM，720×1280 竖屏）。

玩法内核来自 [soarqin/HeroesOfJinYong](https://github.com/soarqin/HeroesOfJinYong)——一个完整的桌面端重制。本仓不改玩法，只做把它塞进一块单片机所需要的那部分：薄 SDL 兼容层、ESP32-P4 平台层，以及一组**覆盖层**，用来替换掉桌面上无所谓、在 32 MB PSRAM 上会致命的实现。

> **本仓不包含任何游戏数据。** 《金庸群侠传》是商业作品，`.GRP` / `.IDX` / `Z.DAT` 等资源需要你自备 DOS 原版目录，由本机脚本转换后拷到 SD 卡。仓库也不会接受这些文件的提交（见 `.gitignore`）。

---

## 目录

- [硬件要求](#硬件要求)
- [当前状态](#当前状态)
- [快速开始](#快速开始)
- [操作](#操作)
- [移植是怎么做的](#移植是怎么做的)
- [为 32 MB PSRAM 做的事](#为-32-mb-psram-做的事)
- [本机测试台](#本机测试台)
- [已知限制](#已知限制)
- [项目结构](#项目结构)
- [故障排查](#故障排查)
- [参与贡献](#参与贡献)
- [许可证与致谢](#许可证与致谢)

---

## 硬件要求

| 项 | 值 |
|---|---|
| 主板 | M5Stack Tab5（ESP32-P4，16 MB Flash，32 MB PSRAM） |
| 屏幕 | 机身自带 MIPI-DSI 720×1280 竖屏；官方 BSP 按触摸固件探测型号（ILI9881C / ST7123 / ST7121） |
| 输入 | **Tab5 键盘套件**（STM32 经 I2C `0x6D`，接 Ext.Port1）。目前没有触摸虚拟键，没有键盘就只能看不能玩 |
| 存储 | FAT32 microSD |
| 声音 | 机身 ES8388 |

- 准备卡数据：Python 3、C/C++ 编译器、CMake（渲染背景音乐用）
- 自己编译固件：**ESP-IDF v5.5.5**（不要用 Arduino）、官方 BSP `espressif/m5stack_tab5_noglib` 1.3.0（无 LVGL）、C++17。只刷预编译固件不需要 ESP-IDF

## 当前状态

标题、地图、对话、存读档已在真机跑过。版本变化见 [CHANGELOG.md](CHANGELOG.md)。

| 功能 | 状态 |
|---|---|
| 标题、新游戏（输姓名、选初始属性） | 真机可用 |
| 大地图 / 子地图行走与进出 | 真机可用；两者内存互斥，切换时有一次显式加载 |
| 事件、对话、菜单、物品、人物 | 真机可用 |
| 战斗 | 本机测试台已验（能进入、内存有界）；**真机还没确认打完一仗**。首战要读约 1.6 MB 战场贴图，有一次可见停顿 |
| 存档 / 读档 | 真机可用；写卡约 4.5 MB，数秒 |
| BGM / 音效 | 真机可用；BGM 流式播放 host 预渲染的 WAV |
| 结局动画 | **不可用**，见[已知限制](#已知限制) |

## 快速开始

### 1. 取源码

```bash
git clone https://github.com/Octo-o-o-o/m5stack_tab5_jinyong.git
cd m5stack_tab5_jinyong
./setup.sh
```

`setup.sh` 拉 submodule（只取固件和 BGM 用到的 `fmt` / `libADLMIDI`，不拉上游的 SDL2 / soxr）并检查 ESP-IDF；不装工具链、不刷机。没装 ESP-IDF 也能继续准备卡数据、刷预编译固件。当前 shell 已经 export 过 IDF 就直接用，否则默认找 `$HOME/.espressif/esp-idf-v5.5.5/export.sh`，可用 `IDF_EXPORT` 覆盖。

### 2. 准备 microSD

这一步需要你自备的 DOS 原版目录（含 `*.GRP` / `*.IDX` / `Z.DAT` 等）。脚本会：

1. 编上游 `makedata`，把原版目录转成运行时 `data/`
2. 改 `config.toml` 为 Tab5 设置（640×480、`scale=2.0`、关小地图、存档目录 `save/`）
3. 从系统中文字体按**界面文字 + 对话 + 人名/道具/武功/地图名**做子集（约 2.4 MB）。整份华文黑体有 50 MB+，塞不进 PSRAM
4. 用 libADLMIDI（DOSBox OPL3）把 `GAME*.XMI` 预渲染成 `GAME*.WAV`。设备上不合成 MIDI，只流式播放这些 WAV

```bash
./prepare_game_data.sh /path/to/original-game
```

默认输出到 `local/sd_image/jinyong`（已 gitignore）；第二个参数可以换输出目录。macOS 默认用系统华文黑体，其它系统把一份 TTF/OTF/TTC 作为第三个参数：

```bash
./prepare_game_data.sh /path/to/original-game ./local/sd_image/jinyong /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc
```

不要背景音乐加 `--no-bgm`。还没有原版目录时，`./prepare_game_data.sh --skeleton` 只生成配置和字体。

把输出目录**整个**拷到卡上的 `/jinyong/`：

```
/jinyong/config.toml
/jinyong/data/                 makedata 产物（GRP/IDX/Z.DAT/strings.toml…）和 GAME*.WAV
/jinyong/data/font/chinese.otf 子集字体
/jinyong/save/                 可空；固件也会自己建
```

FAT32，卡根目录下直接是 `jinyong/`，不要再套一层。至少要有 `/jinyong/config.toml` 和 `/jinyong/data/Z.DAT`，否则屏上显示 NO GAME DATA ON SD。

`prepare_game_data.sh` 写好的 `config.toml` 已经能开局。可选再改 `[ui]`：

```toml
[ui]
no_name_input = false          # true 则跳过姓名框，直接用下面的名字
# default_name = "徐小俠"      # 可选；最多 4 个汉字，且必须在子集字体里
show_minimap = false
show_map_mini_panel = false
scale = 2.0
```

**不要**把原版目录整份拷上卡，也不要提交 `local/` 或任何 `.GRP` / `.IDX` / `.otf`。刷固件不会更新卡上的字体和数据，改了卡数据要重新拷。

### 3. 刷固件

刷机会替换板子上现有的固件。刷写口是 ESP32-P4 的 USB-Serial-JTAG（`303A:1001`），USB 序列号就是芯片 MAC，可以用来分辨板子。不要烧 eFuse。需要手动进下载模式时：按住 Reset 约 2 秒，内部绿灯快闪后松开。

**方式 A：预编译固件（不用装 ESP-IDF）**

从 [Releases](https://github.com/Octo-o-o-o/m5stack_tab5_jinyong/releases/latest) 下载 `tab5_jinyong-vX.Y.Z.bin`（可用同页的 `SHA256SUMS` 校验）。它是完整镜像，**烧到地址 `0x0`**：

- 浏览器：用 Chrome / Edge 打开 Espressif 官方的 [esptool-js](https://espressif.github.io/esptool-js/)，点 **Connect** 选 Tab5 的串口，**Flash Address** 填 `0x0`，选择文件后点 **Program**
- 命令行：

  ```bash
  pip install --upgrade esptool
  esptool --chip esp32p4 -p <端口> write-flash 0x0 tab5_jinyong-vX.Y.Z.bin
  ```

屏 / 键盘 / SD 自检固件 `tab5_jinyong_bringup-vX.Y.Z.bin` 用法相同。

**方式 B：自己编译**

```bash
./build.sh      # 产物 firmware/game/build/tab5_jinyong.bin，日志 firmware/game/last-build.log
./flash.sh      # 会先确认；非交互环境加 --yes
./monitor.sh
```

只连着一个 Espressif 设备时脚本自动选它；连着多块 ESP 板子时会列出来并拒绝猜，这时用 `./flash.sh <端口>` 或设 `ESPPORT` 指定。自检固件用 `TAB5_FIRMWARE=bringup ./build.sh` / `./flash.sh`。

## 操作

| 键 | 作用 |
|---|---|
| 方向键 | 走路 / 菜单移动（按住连续，初始延迟 400 ms，约 10 次/秒） |
| Enter | 确认 / 对话 |
| Esc | 取消；在地图上打开主菜单 |
| Space | 同确认 |
| Backspace | 输入姓名时退格 |

姓名只能输 ASCII——设备上没有输入法。想用中文名，在卡上的 `config.toml` 里写：

```toml
[ui]
default_name = "令狐冲"
```

姓名框会用它预填，直接回车即可。用到的字必须在子集字体里（`prepare_game_data.sh` 会带上 `strings.toml` 和对话、人名用字）。

## 移植是怎么做的

三层，自下而上：

```
firmware/game/components/tab5_platform/   ESP32-P4 平台层（C）
    显示（PPA 硬件旋转 + 缩放，DPI 双缓冲）、I2C 键盘、SD、ES8388、时钟、内存与性能计数

firmware/game/components/hojy_sdl/        薄 SDL 兼容层（C）
    upstream 只用到的那一小块 SDL2：软件光栅、事件、音频回调。不是 SDL 移植，是重新实现

firmware/game/components/hojy_core/       upstream + 覆盖层（C++）
    逐文件替换 upstream 中在 MCU 上行不通的实现
```

**覆盖层机制**：`hojy_core/` 里的每个 `.cc` 按文件名替换 upstream `src/` 下的同名文件，其余 upstream 源文件原样编译（规则在该目录的 `CMakeLists.txt`，测试台用同一规则）。于是：

- submodule **一行都不改**，随时可以跟上游同步（升级流程见 [ARCHITECTURE.md](docs/ARCHITECTURE.md#7-覆盖层)）
- 派生自上游的覆盖层保留原作者的版权声明，改动处写明「upstream 怎么做的、为什么在这台机器上不行、这里怎么改的」
- 没被覆盖的文件仍然是 upstream 原版，玩法逻辑完全一致

目前 25 个上游 `.cc` 被覆盖，另有固件入口 `main.cc` 和新增的流式 BGM 通道；另外覆盖了 3 个上游头文件。集中在三类：一次性把整份资源读进内存的、按桌面尺寸开缓冲的、以及在桌面上无所谓而在这里会崩的失败路径。

架构细节见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)，上游分层分析见 [docs/UPSTREAM_ANALYSIS.md](docs/UPSTREAM_ANALYSIS.md)。

## 为 32 MB PSRAM 做的事

移植这类老游戏，真正的工作量不在图形也不在输入，在**内存账**。几个有代表性的：

| 项 | 改前 | 改后 |
|---|---|---|
| 呈现路径 | CPU 按列采样做 90° 旋转 + 1.5× 缩放，读 1.2 MB 实际从 PSRAM 拉约 19 MB | ESP32-P4 **PPA** 硬件做旋转/缩放/格式转换，DMA 驱动 |
| BGM | 整曲解进内存，22 kHz 立体声 45 秒 = 3.97 MB 常驻 | 流式 `Channel`，常驻 0.04 MB；加载在独立任务上，不卡主循环 |
| 纹理图集 | 每页 1024² ARGB = 4.19 MB，常驻四五页 | 按数据实测最大精灵定为 512²（1.05 MB） |
| 大地图 `CellInfo` | 480×480×16 B = 3.52 MB | 字段打包到 10 B = 2.20 MB；三张 0.44 MB 查表折进 `canWalk` 后释放 |
| 大地图 vs 子地图 | 想同时驻留，合计超出可用总量 | **互斥**：进出各付一次加载，前面摆一帧「等待……」 |
| 战斗贴图 | 构造时一次读入全部 FIGHT | 按出场角色 headId 懒加载 |
| 存档 | 整份深拷贝 + 每条一个 string + 拼成一整块，4.5 MB 的存档瞬时要约 13 MB | 边序列化边落盘，峰值 0.16 MB |

完整账本、已落地项、以及**明确不做**的项见 [docs/PERF_PLAN.md](docs/PERF_PLAN.md)。

踩过的坑按「错判 / 真因 / 处理 / 防」整理在 [docs/HANDHELD_PORT_PLAYBOOK.md](docs/HANDHELD_PORT_PLAYBOOK.md)——那份是写给下一个做同类移植的人的，比本 README 具体得多。

## 本机测试台

不用设备、不开窗口、不出声，在 macOS 上跑**这个移植自己的游戏逻辑**：

```bash
tools/host_test/run.sh local/sd_image/jinyong      # 对着本机镜像
tools/host_test/run.sh "/Volumes/NO NAME/jinyong"  # 或者直接对着卡
```

它把 upstream 编到本机、替换进全部覆盖层、用 `shim/` 顶掉 ESP-IDF 头文件，然后用真实数据跑：按键重复、存档往返字节比对、新游戏走到门口进大地图、菜单读档来回、流式 BGM 循环、25 轮快速进出子地图、进一次战斗。每一步报内存**峰值**和**最大单块**——后者决定它能不能落进一整块连续 PSRAM。

**它会改写目标目录里 1–3 号存档槽**，有要保留的存档先对着副本跑。它不覆盖平台层（PPA、DPI 翻页、I2C 键盘、ES8388）和真实的 PSRAM 碎片，那些只能上机。详见 [tools/host_test/README.md](tools/host_test/README.md)。

## 已知限制

- **结局动画放不出来。** `KEND.GRP` 是 14.1 MB / 221 帧 320×200，展开成纹理要上百 MB，这台机器结构性装不下。目前的行为是安全退化：只滚字幕、不放图、不崩。要支持得改成逐帧流式读。
- **姓名不能输中文**，设备上没有输入法。用 `config.toml` 的 `default_name` 绕开。
- **大地图与子地图互斥**，进出城各付一次加载（约一两秒，屏上有等待帧）。这不是可以调优掉的，是 32 MB 的硬约束。
- **存档写卡数秒**，有等待帧但没有进度条。
- **没有触摸操作**，必须接键盘套件。
- 固件按 ESP32-P4 rev < v3.0 编译（目前的 Tab5 都是 rev v1.x）。
- 本机测试台目前只支持 macOS。

## 项目结构

```
firmware/game/                      游戏固件（ESP-IDF 工程）
  main/                             app_main、兜底错误屏
  components/tab5_platform/         ESP32-P4 平台层（公开头文件在 include/）
  components/hojy_sdl/              薄 SDL 兼容层
  components/hojy_core/             upstream + 覆盖层
  components/hojy_fmt, hojy_zita/   上游依赖的精简构建
firmware/bringup/                   屏 / 键盘 / SD / 喇叭自检固件（不是游戏）
third_party/HeroesOfJinYong/        上游 submodule（不修改）
tools/host_test/                    macOS 无头测试台
scripts/                            IDF 环境、host 工具、字体子集、BGM 预渲染、发布打包
docs/                               架构、性能账本、踩坑手册
.github/                            CI、发布工作流、issue / PR 模板
LICENSES/, REUSE.toml               许可证全文与 REUSE 标注
```

文档入口：[docs/README.md](docs/README.md)。当前能做什么以本 README 为准。

## 故障排查

| 现象 | 处理 |
|---|---|
| 找不到 `export.sh` | 装 ESP-IDF v5.5.5 并在 shell 里 export，或设 `IDF_EXPORT` 指到它的 `export.sh`；只刷预编译固件可以不装 |
| 组件下载失败 | 需要能访问 Espressif component registry |
| 屏上显示 NO GAME DATA ON SD | 卡上没有 `/jinyong/config.toml` / `/jinyong/data/Z.DAT`；检查是不是多套了一层目录 |
| 屏上显示 HOJY MAIN EXITED | 卡数据不全（缺必需文件或字体）；`./monitor.sh` 看串口日志缺了什么 |
| 键盘无反应 | 套件要插 Ext.Port1；总线是 G0/G1，不是 G31/G32 |
| 对话或名字显示空白 | 字体子集缺字：重跑 `prepare_game_data.sh`，把 `data/font/chinese.otf` 拷回卡 |
| 没有背景音乐 | 卡上 `data/` 缺 `GAME*.WAV`：`scripts/render_bgm.sh <jinyong 目录>` 后拷回卡 |
| `flash.sh` 找不到串口或提示有多块设备 | `./flash.sh <端口>`（macOS 形如 `/dev/cu.usbmodem*`，Linux 常见 `/dev/ttyACM0`），或设 `ESPPORT` |
| 屏上出现 GAME STOPPED 和一行英文 | 游戏抛了异常并被兜底接住，那行就是原因；`./monitor.sh` 看完整日志 |
| 设备自己重启 | 先怀疑软件 abort（内存不足、栈溢出），不是硬件。`./monitor.sh` 会打出 panic 回溯 |

仍然解决不了，请按 issue 模板提交，附上固件版本和串口日志。

## 参与贡献

欢迎 issue 和 PR，动手前先读 [CONTRIBUTING.md](CONTRIBUTING.md)。几条红线：不改 `third_party/HeroesOfJinYong`（改覆盖层）、不提交游戏数据、验收分清**固件 / 卡数据 / 看屏**三层。参与即表示同意[行为准则](CODE_OF_CONDUCT.md)；安全问题请按 [SECURITY.md](SECURITY.md) 私密报告。

## 许可证与致谢

本仓库以 **GPL-3.0-or-later** 发布（见 [LICENSE](LICENSE)），与上游 HeroesOfJinYong 一致；派生自上游源文件的覆盖层保留原作者的版权声明。仓库遵循 [REUSE](https://reuse.software/) 规范：源文件带 SPDX 标识，其余文件由 `REUSE.toml` 声明。发布固件里包含的第三方组件及其许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

- [soarqin/HeroesOfJinYong](https://github.com/soarqin/HeroesOfJinYong) —— 完整的桌面端重制，本项目的玩法内核全部来自它。
- 《金庸群侠传》原作 © 河洛工作室 / 智冠科技。**游戏数据不在本仓库内，也不由本项目分发。**
- M5Stack Tab5 官方 BSP 与 ESP-IDF（Espressif）。

字体：`prepare_game_data.sh` 从**你本机系统里的字体**做子集，产物不进仓库。用于分发前请自行确认该字体的授权。
