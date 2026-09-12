# tab5_jinyong

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

工具链：**ESP-IDF v5.5.5**（不要用 Arduino）、官方 BSP `espressif/m5stack_tab5_noglib` 1.3.0（无 LVGL）、C++17。

## 当前状态

标题、地图、对话、存读档已在真机跑过。不是完成移植，也不再用「M2 第一帧 → M3 → M4」当进度。

| 功能 | 状态 |
|---|---|
| 标题、新游戏（输姓名、选初始属性） | 真机可用 |
| 大地图 / 子地图行走与进出 | 真机可用；两者内存互斥，切换时有一次显式加载 |
| 事件、对话、菜单、物品、人物 | 真机可用 |
| 战斗 | 本机测试台已验（能进入、内存有界）；**真机还没确认打完一仗**。首战要读约 1.6 MB 战场贴图，有一次可见停顿 |
| 存档 / 读档 | 真机可用；写卡约 4.5 MB，数秒 |
| BGM / 音效 | 真机可用；BGM 流式播放 host 预渲染的 WAV（没有对应 WAV 的曲目静音） |
| 结局动画 | **不可用**，见[已知限制](#已知限制) |

## 快速开始

### 1. 取源码

```bash
git clone --recurse-submodules https://github.com/Octo-o-o-o/m5stack_tab5_jinyong.git
cd m5stack_tab5_jinyong
./setup.sh
```

`setup.sh` 只初始化 submodule 并检查 IDF，不装全局工具链、不刷机。
IDF 的 `export.sh` 默认在 `$HOME/.espressif/esp-idf-v5.5.5/`，可用 `IDF_EXPORT` 覆盖。

### 2. 编译

```bash
./build.sh
```

产物 `firmware/game/build/tab5_jinyong.bin`，完整日志 `firmware/game/last-build.log`。

### 3. 准备 microSD

这一步需要你自备的 DOS 原版目录（含 `*.GRP` / `*.IDX` / `Z.DAT` 等）。脚本会：

1. 编上游 `makedata`，把原版目录转成运行时 `data/`
2. 改 `config.toml` 为 Tab5 窗口/UI/音频（640×480、`scale=2.0`、关小地图、存档目录 `save/`）
3. 从系统中文字体按**对话 + 人名/道具/武功/地图名**做子集（约 2 MB）。整份华文黑体有 50 MB+，塞不进 PSRAM

```bash
./prepare_game_data.sh /path/to/original-game ./local/sd_image/jinyong
```

macOS 默认用系统华文黑体。Linux / 其它字体把第三个参数指到一份 TTF/OTF/TTC：

```bash
./prepare_game_data.sh /path/to/original-game ./local/sd_image/jinyong /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc
```

还没有原版目录时，可以先只生成字体和空目录：

```bash
./prepare_game_data.sh --skeleton
```

产物全部在 `local/`（已 gitignore）。把 `local/sd_image/jinyong` **整个**拷到卡上的 `/jinyong/`，不要只拷 `data/`：

```
/jinyong/config.toml
/jinyong/data/                 makedata 产物：GRP/IDX/Z.DAT/strings.toml、GAME*.XMI 等
/jinyong/data/font/chinese.otf 子集字体（必须）
/jinyong/fonts/chinese.otf     同一份字体的副本（必须；固件会按这两个路径回退）
/jinyong/save/                 可空；固件也会自己建
```

FAT32。卡根目录名字就是 `jinyong`，不要再套一层。拷完后最少要能看到这两个文件，否则屏上只有 INSERT SD：

- `/jinyong/config.toml`
- `/jinyong/data/Z.DAT`

`prepare_game_data.sh` 写好的 `config.toml` 已经能开局。可选再改 `[ui]`：

```toml
[ui]
no_name_input = false          # true 则跳过姓名框，直接用下面的名字
# default_name = "徐小俠"      # 可选；最多 4 个汉字，且必须在子集字体里
show_minimap = false
show_map_mini_panel = false
scale = 2.0
```

**不要**把原版目录整份拷上卡，也不要提交 `local/` 或任何 `.GRP` / `.IDX` / `.otf`。

**背景音乐是可选的额外一步。** 设备上不链 ADLMIDI，播的是 host 预渲染的 `GAMExx.WAV`；没有对应 WAV 的曲目静音。`makedata` 已经把 `GAME*.XMI` 放进 `data/`。转换工具是 `scripts/xmi_to_wav.c`，目前没有接进 `prepare_game_data.sh`。先跑一次测试台编出 `libADLMIDI.a`：

```bash
tools/host_test/run.sh ./local/sd_image/jinyong   # 至少跑到开始编 host 工具即可
cc -O2 -I third_party/HeroesOfJinYong/deps/libADLMIDI/include \
   scripts/xmi_to_wav.c -o /tmp/xmi_to_wav \
   "${TMPDIR:-/tmp}/tab5_jinyong_hosttest/hostbuild/libADLMIDI.a" -lstdc++ -lm

# 产物必须和 XMI 放在同一目录 data/（config 的 music_path = "data"）
for xmi in ./local/sd_image/jinyong/data/GAME*.XMI; do
  /tmp/xmi_to_wav "$xmi" "${xmi%.XMI}.WAV" 22050 45
done
```

然后再把更新过的 `data/*.WAV` 拷回卡。没有 WAV 也能玩，只是没 BGM。

### 4. 刷机

刷机默认拒绝，需要显式确认环境变量——这是为了防止误刷覆盖板子上已有的固件：

```bash
# macOS 常见口；Linux 常见 /dev/ttyACM0。必须是 USB VID:PID 303A:1001
TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE=1 ESPPORT=/dev/cu.usbmodem1101 ./flash.sh
./monitor.sh
```

刷写口必须是 Espressif USB-JTAG（`303A:1001`）。不要刷 ESP32-C6，不要烧 eFuse。进下载模式：按住 Reset 约 2 秒，内部绿灯快闪后松开。

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

姓名框会用它预填，直接回车即可。用到的字必须在子集字体里（`prepare_game_data.sh` 会带上 `strings.toml` 和对话用字）。

## 移植是怎么做的

三层，自下而上：

```
firmware/game/components/tab5_platform/   ESP32-P4 平台层（C）
    显示（PPA 硬件旋转 + 缩放，DPI 双缓冲）、I2C 键盘、SD、ES8388、时钟、内存与性能计数

firmware/game/components/hojy_sdl/        薄 SDL 兼容层（C）
    upstream 只用到的那一小块 SDL2：软件光栅、事件、音频回调。不是 SDL 移植，是重新实现

firmware/game/components/hojy_core/       覆盖层（C++）
    逐文件替换 upstream 中在 MCU 上行不通的实现
```

**覆盖层机制**：`CMakeLists.txt` 用 `list(FILTER ... EXCLUDE REGEX ...)` 把 upstream 的同名 `.cc` 从编译列表里剔掉，再把本仓的版本加回去。于是：

- submodule **一行都不改**，随时可以跟上游同步
- 每个覆盖文件顶部都写清楚「upstream 怎么做的、为什么在这台机器上不行、这里怎么改的」
- 没被覆盖的文件仍然是 upstream 原版，玩法逻辑完全一致

目前覆盖了 27 个 `.cc` 和 6 个头文件，集中在三类：一次性把整份资源读进内存的、按桌面尺寸开缓冲的、以及在桌面上无所谓而在这里会崩的失败路径。

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

不用设备、不开窗口、不出声，在 Mac 上跑**这个移植自己的游戏逻辑**：

```bash
tools/host_test/run.sh /path/to/jinyong          # 对着本机镜像
tools/host_test/run.sh "/Volumes/NO NAME/jinyong" # 或者直接对着卡
```

它把 upstream 编到本机、替换进全部覆盖层、用 `shim/` 顶掉 ESP-IDF 头文件，然后用真实数据跑：按键重复、存档往返字节比对、新游戏走到门口进大地图、菜单读档来回、流式 BGM 循环、25 轮快速进出子地图、进一次战斗。每一步报内存**峰值**和**最大单块**——后者决定它能不能落进一整块连续 PSRAM。

它不覆盖平台层（PPA、DPI 翻页、I2C 键盘、ES8388）和真实的 PSRAM 碎片，那些只能上机。详见 [tools/host_test/README.md](tools/host_test/README.md)。

## 已知限制

- **结局动画放不出来。** `KEND.GRP` 是 14.1 MB / 221 帧 320×200，展开成纹理要上百 MB，这台机器结构性装不下。目前的行为是安全退化：只滚字幕、不放图、不崩。要支持得改成逐帧流式读。
- **姓名不能输中文**，设备上没有输入法。用 `config.toml` 的 `default_name` 绕开。
- **大地图与子地图互斥**，进出城各付一次加载（约一两秒，屏上有等待帧）。这不是可以调优掉的，是 32 MB 的硬约束。
- **存档写卡数秒**，有等待帧但没有进度条。
- **没有触摸操作**，必须接键盘套件。
- **刷机会替换**板子上现有的固件。

## 项目结构

```
firmware/game/           游戏固件（IDF 工程）
  components/tab5_platform/  ESP32-P4 平台层
  components/hojy_sdl/       薄 SDL 兼容层
  components/hojy_core/      upstream 覆盖层
firmware/bringup/        屏 / 键盘 / SD 自检固件（不是游戏）
third_party/HeroesOfJinYong/   上游 submodule（不修改）
tools/host_test/         本机无头测试台
scripts/                 字体子集、host 工具构建、XMI→WAV
docs/                    架构、性能账本、踩坑手册
```

文档入口：[docs/README.md](docs/README.md)。当前能做什么以本 README 为准；`docs/PORTING_PLAN.md` 只是立项时的拆分，不是进度表。

## 故障排查

| 现象 | 处理 |
|---|---|
| 找不到 `export.sh` | 装 ESP-IDF v5.5.5，或设 `IDF_EXPORT` 指到它的 `export.sh` |
| 组件下载失败 | 需要能访问 Espressif component registry |
| 屏上只有 INSERT SD | 卡上还没有 `/jinyong/config.toml` 和 `data/Z.DAT` |
| 键盘无反应 | 套件要插 Ext.Port1；总线是 G0/G1，不是 G31/G32 |
| 屏上出现 GAME STOPPED 和一行英文 | 游戏抛了异常并被兜底接住，那行就是原因；接串口跑 `./monitor.sh` 看完整日志 |
| 设备自己重启 | 先怀疑软件 abort（内存不足、栈溢出），不是硬件。`./monitor.sh` 会打出 panic 回溯 |

## 贡献

欢迎 issue 和 PR。几条本仓的约定：

1. **不要修改 `third_party/HeroesOfJinYong`**。需要改上游行为就加/改覆盖层，并在文件顶部写清楚原因。
2. **不要提交游戏资源、存档、字库原件**，`.gitignore` 已经挡了大部分，但请自己再看一眼 `git status`。
3. 改到内存或输入的，请让 `tools/host_test/run.sh` 通过，并把新行为写成断言加进去。
4. 验收请分清**固件 / 卡数据 / 看屏**三层，没上机的就写「未验」。

## 许可证与致谢

本仓库以 **GPL-3.0** 发布（见 [LICENSE](LICENSE)）——它派生自并链接 HeroesOfJinYong，后者是 GPL-3.0。

- [soarqin/HeroesOfJinYong](https://github.com/soarqin/HeroesOfJinYong) —— 完整的桌面端重制，本项目的玩法内核全部来自它。
- 《金庸群侠传》原作 © 河洛工作室 / 智冠科技。**游戏数据不在本仓库内，也不由本项目分发。**
- M5Stack 的 Tab5 官方 BSP。

字体：`prepare_game_data.sh` 从**你本机系统里的字体**做子集，产物不进仓库。用于分发前请自行确认该字体的授权。
