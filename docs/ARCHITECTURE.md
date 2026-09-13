# 架构

本仓是独立的 Tab5 游戏固件：独占 MIPI-DSI、PSRAM framebuffer 和主循环。

```
原游戏数据  --(host: makedata / 字体子集 / BGM 预渲染)-->  SD /jinyong/
                                              │
                                              ▼
                 HeroesOfJinYong gameplay/core
                 (content / world / event / battle / scene-logic)
                                              │
                              Platform API（本仓实现）
                     Video  Input  Audio  FS  Timing  Log
                                              │
                              tab5_platform + ESP-IDF 5.5.5
                                              │
                                         M5Stack Tab5
```

许可证：链接 HeroesOfJinYong 则本仓按 **GPLv3**。

---

## 1. 仓库地图

当前能做什么见仓库根 README。

| 路径 | 作用 |
|---|---|
| `third_party/HeroesOfJinYong/` | submodule，SHA 见 `UPSTREAM_ANALYSIS.md` |
| `firmware/game/` | 游戏固件：HOJY core + 覆盖层 + 自写 SDL 薄层 + 官方 BSP |
| `firmware/game/components/tab5_platform/` | 平台层；公开 C API 在 `include/tab5_platform.h` |
| `firmware/bringup/` | 独立 ESP-IDF 工程：屏 / I2C 键盘 / SD / 喇叭自检，不是游戏 |
| `tools/host_test/` | macOS 上的无头测试台 |
| `scripts/` | IDF 环境、host 工具构建、字体子集、BGM 预渲染 |
| `docs/` | 架构 / 上游分析 / 性能账本 / 踩坑手册 |
| `.github/workflows/` | CI（编译两个固件、检查脚本与许可证标注）；推送 `v*` tag 时发布预编译固件 |
| `LICENSES/`、`REUSE.toml` | 许可证全文与 REUSE 标注；第三方组件见根目录 `THIRD_PARTY_NOTICES.md` |
| `local/` | 永不提交：SD 镜像、host 工具、Python venv |

---

## 2. Platform API（core 只看见这些）

头文件在 `firmware/game/components/tab5_platform/include/tab5_platform.h`。HOJY scene 仍 `#include <SDL.h>`；设备上这是本仓 `firmware/game/components/hojy_sdl`，不是桌面 SDL2。I2C HID 在 `SDL_PollEvent` 里译成 `SDL_KEYDOWN/UP`，再进已有 `SdlInputCollector` → `InputAction`。

```
Game Core
├── Game Logic / Map / Battle / Save / Resource
└── Platform API
     ├── Tab5Video     低分辨率 game FB → PPA 旋转/缩放 → DPI 双缓冲
     ├── Tab5Input     I2C 0x6D → InputEvent{key, pressed, released, timestamp}
     ├── Tab5Audio     ES8388；SFX 走 SDL WAV，BGM 流式播 host 预渲染 WAV
     ├── Tab5Filesystem  FAT /sdcard/jinyong/* ，fopen 语义
     ├── Tab5Clock     µs 单调钟
     └── Tab5Log       ESP_LOG
```

硬规则：

- core 不准直接读 I2C、USB HID scancode、GPIO
- 第一优先键盘：**Tab5 Keyboard 套件**，STM32，I2C 地址 `0x6D`，线在 **G0/SDA、G1/SCL、G50/INT**（官方键盘文档）。这是 I2C 包，不是「键盘插在 USB-A 上」
- HID mode = I2C 上的 `{modifier, keycode}`；Normal mode = 1 字节 row/col；Character mode = 字符串。三种都是 I2C
- USB-A Host 外接键盘：以后再说
- 不做触摸虚拟键
- 画面：原比例、黑边、nearest-neighbor；不默认 16:9 拉伸，不默认 bilinear
- 不烧 eFuse，不刷 C6

---

## 3. 显示所有权

```
HeroesOfJinYong Renderer / Texture
        │  (低分辨率，优先 RGB565)
        ▼
  game framebuffer   例如 640×480 RGB565 ≈ 0.60 MB
        │  integer 或明确记录的 nearest
        ▼
  letterbox 到 1280×720
        │
        ▼
  PPA 做 90° / 缩放 / 格式转换，再交给官方 BSP 的 MIPI-DSI DPI 双缓冲（RGB565）
```

禁止：多个 1280×720 RGBA、把 SDL2 当 ESP 渲染器、手写过期屏 init。

屏驱兼容（官方产品变更，不是本仓私货）：

| 世代 | 显示 | 触摸 | 探测 |
|---|---|---|---|
| 早期 | ILI9881C | GT911 @ 0x14 | I2C ACK 0x14 |
| 2025-10-14+ | ST7123 | 集成 @ 0x55 | 0x55 FW==3 |
| 2026-04-28+ | ST7121 | 集成 @ 0x55 | 0x55 FW==1 |

检测逻辑要兼容上表三块屏。用官方 `espressif/m5stack_tab5_noglib` **1.3.0**（无 LVGL）。Registry README 能力表没写 ST7121，但该版本源码已按 FW 探测 ST7121 并换 init/DPI（仍走 `esp_lcd_new_panel_st7123`）。bring-up 会先自己 probe 再调用 `bsp_display_new*`。

---

## 4. 输入所有权

```
Tab5 Keyboard MCU (STM32 @ I2C 0x6D)
        │  独立总线 G0/G1，不要用 BSP 内部 G31/G32
        ▼
tab5_platform（tab5_input.c：HID 包、INT 引脚、热插拔重探）
        ▼
hojy_sdl（sdl_events.c：SDL_KEYDOWN/UP、repeat 标记、ASCII TEXTINPUT）
        ▼
hojy::app::InputQueue  （upstream 已有）
        ▼
Window / Menu / Map / Warfield
```

官方寄存器（`Tab5_Keyboard-I2C-Protocol-EN-V1.0`）：

| 地址 | 含义 |
|---|---|
| 0x00 | INT_CFG |
| 0x01 | INT_STA |
| 0x02 | EVENT_NUM（0–32） |
| 0x10 | 模式：0 Normal / 1 HID / 2 Character |
| 0x20 | Normal：`[7]=press, [6:4]=row, [3:0]=col`，空=0xFF |
| 0x30 | HID：modifier + keycode，空=0xFF |
| 0x40/0x50 | Character |

bring-up 默认设 HID mode，把 I2C HID 包打到串口。这仍然不是 USB Host。

---

## 5. 存储与存档

运行时只在 SD，不进 Git。

```
/jinyong/
  config.toml
  data/                 makedata 产物；BGM 的 GAMExx.WAV 也放这里
  data/font/chinese.otf 子集字体
  save/                 R/S/D + IDX/GRP
```

固件读 `/sdcard/jinyong/config.toml`。`config.toml` 指的字体读不到时，依次回退到 `fonts/chinese.otf`、`data/font/chinese.otf`。存档语义跟 upstream：`SaveData::save/load`，设备上改成边序列化边落盘，不改 slot 文件名。

---

## 6. 内存账本

数字和已落地项以 [PERF_PLAN.md](PERF_PLAN.md) 为准，不要用下面当预算目标。

| 池 | 现状 |
|---|---|
| 内部 SRAM | IDF + 任务栈 + 小 DMA；音频环形缓冲优先放这里 |
| PSRAM | 面板 FB + 游戏 backbuffer + 图集 + 当前一张地图；大地图与子地图互斥；禁多份 720p RGBA |
| Flash | 游戏固件；资源在 SD |
| Resource cache | 按需 GRP；战斗贴图按出场角色懒加载 |
| Audio | SFX：SDL WAV + ES8388；BGM：流式 WAV，不链 ADLMIDI |
| Game heap | `SDL_malloc` 优先 PSRAM |

---

## 7. 覆盖层

`firmware/game/components/hojy_core/` 里的每个 `.cc` 按**文件名**替换 upstream `src/<module>/` 下的同名文件，其余 upstream 源文件原样编译。规则写在该目录的 `CMakeLists.txt`，测试台 `tools/host_test/run.sh` 用同一规则。新增或删除覆盖层后要 `idf.py reconfigure`。

- 派生自 upstream 的文件保留原作者的版权声明，改动处写明原因
- 只覆盖 `.cc`；`globalmap.hh` / `submap.hh` / `talkbox.hh` 是核实过不破坏其它 TU 的例外（见 Playbook P-40）
- 覆盖层是 upstream 文件的拷贝加改动，继承了同样的告警，所以整个组件以 `-w` 编译

升级 submodule 时，先看被覆盖的文件在上游改了什么，再逐个合进覆盖层：

```bash
old=$(git rev-parse HEAD:third_party/HeroesOfJinYong)   # 升级前记下
# ……把 submodule 切到新版本之后：
git -C third_party/HeroesOfJinYong diff --stat "$old" HEAD -- \
  $(cd firmware/game/components/hojy_core && for f in *.cc *.hh; do printf ':(glob)**/%s ' "$f"; done)
```

---

## 8. 工程边界

- 官方组件从 Espressif registry 拉，不 `override_path` 到其它仓库
- 无 Wi-Fi、不刷 ESP32-C6、不烧 eFuse
- `flash.sh` 刷写前要求确认，非交互环境需 `--yes`
