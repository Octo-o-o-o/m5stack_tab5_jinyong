# HeroesOfJinYong 源码分析

立项时对 submodule 的阅读笔记，不是移植进度表。当前能做什么见仓库根 README；设备上的覆盖层和内存账见 `ARCHITECTURE.md` / `PERF_PLAN.md`。

读过的对象是本仓 submodule，不是只复述 upstream README。

- 仓库：`third_party/HeroesOfJinYong`
- 检出 SHA：`2e65e97f756a9499ddf09eebe12443b94e07e275`（`2e65e97 fix(app): match map logic rate to BIOS tick`）
- 许可证：GPLv3（`LICENSE`）
- 默认构建：`USE_FREETYPE=OFF`（`stb_truetype`），`USE_SOXR=OFF`（`zita-resampler`），`BUILD_TOOLS=OFF`
- 语言：C++17，CMake
- 不改该 submodule 的源码；设备差异只放覆盖层

嵌套 submodule（随 HeroesOfJinYong 检出）：

| 路径 | SHA | 角色 |
|---|---|---|
| `deps/SDL2` | `5d249570393f7a37e037abf22cd6012a4cc56a71` | 桌面窗口 / 渲染 / 音频设备 / 事件（Windows 用 vendored；其它 OS 也可找系统 SDL2） |
| `deps/fmt` | `1be298e1bd68957e4cd352e1f676f00e07dcfb57` | 格式化日志与 UI 字符串 |
| `deps/libADLMIDI` | `2b350f9ef5fa7bafd90b8ce3beb2a77c1e87af25` | XMI/MIDI → OPL3 PCM（GPLv3） |
| `deps/soxr` | `945b592b70470e29f917f4de89b4281fbbd540c0` | 可选高质量重采样（LGPLv2.1）；默认不用 |

树内还有 `deps/zita-resampler`、`deps/SDL2_gfx`（非 git submodule，随主仓提供）。

---

## 1. 源码分层（实际目录，不是愿望图）

`src/CMakeLists.txt` 已经把代码切成静态库，但**平台边界并不干净**：`hojy_scene` / `hojy_app` 直接链 `SDL2` / `SDL2_gfx`。

```
src/main.cc
  → core::Config（toml++）
  → world::Strings
  → content::loadData()
  → app::Application(windowWidth, windowHeight, animationSpeed)

src/app/          主循环、输入队列、SDL 采集
src/core/         config.toml + ResourceMgr 文件扫描
src/content/      IDX/GRP、事件、战场、原子写文件
src/world/        存档结构、人物/物品/技能、背包
src/event/        事件 VM
src/battle/       战斗公式 / AI（相对最干净，几乎不碰 SDL）
src/scene/        地图 / 菜单 / 对话 / 渲染 / 字体（大量 SDL）
src/audio/        Mixer + WAV + MIDI（直接 SDL_OpenAudioDevice）
src/util/         File(FILE*)、编码转换、随机数
src/tools/        makedata / mergepic（host 预处理）
```

可直接复用、尽量不要改的：`content/`、`world/`、`event/`、`battle/`。  
必须换皮的：`app/application.cc` 的 SDL 时钟可以留，但 `scene/window.cc`、`scene/renderer.cc`、`scene/texture.cc`、`audio/mixer.cc`、`app/sdl_input.cc`。

---

## 2. 启动与主循环

`src/main.cc` 启动顺序（失败即退出，不会开窗）：

1. `core::config.load("config.toml")`
2. 若存在 `saveFilePath("options.toml")` 再 load 一次（音量 / 小地图开关）
3. `postLoad()`：扫描资源；缺必需文件则失败
4. `strings.toml`
5. `content::loadData()`：`Z.DAT` + `KDEF`/`TALK` + `WAR.STA`/`WARFLD`
6. `Application(w, h, animationSpeed).run()`

`app/application.cc` 的循环不是「一个 while + SDL_Delay」：

| 时钟 | 常量 | 作用 |
|---|---|---|
| 固定逻辑 tick | `FixedTickMicros = 16666`（约 60 Hz） | `window.updateFixed()`：音频 service + 地图/弹窗 `doUpdate()` |
| 兼容逻辑 | `LegacyLogicRateHz = 18.2065` × `animationSpeed` | 原版 BIOS PIT `0x046C`；`window.compatibilityUpdate()` → `map.advanceCompatibilityFrame()` |
| 渲染 | `window.render()` + `flush()` | `Renderer::canRender()` 用 `limit_fps`（`postLoad` 把 0 改成 60） |

输入：`SdlInputCollector` 填 `InputQueue`，按 simulation timestamp `drainThrough` 后再 `window.dispatchInput`。

这对 Tab5 是好事：游戏逻辑已经按固定步走，不依赖桌面 vsync。设备端目标稳定 30 fps 渲染即可，逻辑仍可 60 Hz / 18.2 Hz 两套。

---

## 3. 分辨率与 framebuffer

配置默认（`core/config.hh`）：`windowWidth_ = 640`，`windowHeight_ = 480`，`scale_ = {2, 1}`。  
仓库自带 `src/config.toml`：**1024×640**，`scale = 2.0`。

含义（来自 `scene/map.cc`）：

- 窗口像素 = `config.windowWidth/Height`
- 地图离屏缓冲 `auxWidth/Height = window * scale.second / scale.first`  
  即 scale=2 时，地形先画在一半逻辑分辨率，再放大贴到窗口
- `Renderer` 按窗口宽高算字体：`fontSize ≈ min(h, w*3/4) / 48 * 2`

纹理像素格式（`scene/texture.cc`）：**全部 `SDL_PIXELFORMAT_ARGB8888`**。  
`createAsTarget` 还会把宽高升到 2 的幂。

已知的大缓冲（按 ARGB8888 估算）：

| 对象 | 来源 | 粗算 |
|---|---|---|
| 桌面窗口自身 | SDL | 1024×640×4 ≈ 2.5 MB（本仓默认 toml） |
| 地图 `drawingTerrainTex_` | `map.cc` | auxW×auxH×4；1024×640 / scale2 → 512×320×4 ≈ 0.66 MB |
| 小面板 `miniPanelTex_` | `map.cc` | `createAsTarget(256,256)` → 256×256×4 = 256 KB |
| 世界小地图 `miniMapTex_` | `globalmap.cc` | 约 1919×960×4 ≈ **7.4 MB** |
| 第二张地形 `drawingTerrainTex2_` | `globalmap.cc` | 同 aux |
| 物品图集 | `window.cc` | `1024 / itemW` 列 × 200 格，ARGB8888 |
| 字体/头像 atlas | `rectpacker.hh` | 默认 **1024×1024** 一页，ARGB8888 = 4 MB / 页 |
| 结局缓存 | `endscreen.cc` | `createAsTarget(512, 4096)` → 512×4096×4 = 8 MB |
| Tab5 面板 FB | 官方 BSP RGB565 | 1280×720×2 ≈ **1.8 MB**（DPI 已占，不要再复制一份 RGBA） |

世界地图逻辑尺寸：`GlobalMapWidth/Height = 480`（`globalmap.cc`）。  
子地图 / 战场：64×64，多层（`content/constants.hh`）。  
地图层原始数据：`EARTH.002` 等，每层 `480×480×uint16` ≈ 450 KB，五层约 2.3 MB。

**Tab5 策略（已拍板，这里只记录源码含义）：**

- core 维持低分辨率（建议 640×480 或更接近原版的 640×400），**不要**用 1024×640 当设备默认
- 最后一步 nearest-neighbor 放大到 1280×720，保持 4:3，黑边，禁止默认 bilinear
- `Renderer::enableLinear(false)` 是游戏主体路径；头像加载曾短暂 `enableLinear(true)`，设备端不要学这个

官方 BSP 把面板写成 `BSP_LCD_H_RES=720`、`BSP_LCD_V_RES=1280`（肖像坐标）。Tab5 手持是 1280×720 横屏。M1 只画测试 pattern 并记录朝向，不在 bring-up 里猜旋转矩阵。

---

## 4. SDL2 真正提供的能力

不要把 SDL2 整包搬上 P4。按调用点拆：

### 4.1 Video（`scene/window.cc`, `scene/renderer.cc`, `scene/texture.cc`）

| SDL 能力 | 调用 | 设备端替代 |
|---|---|---|
| 建窗 | `SDL_CreateWindow` | 无；MIPI-DSI 面板已存在 |
| 加速渲染器 + render target | `SDL_CreateRenderer(..., ACCELERATED\|TARGETTEXTURE)` | 低分辨率 game FB + blit |
| present / clear / clip | `SDL_RenderPresent/Clear/Copy` | 自己的 compositor |
| ARGB8888 streaming/target 纹理 | `SDL_CreateTexture` | RGB565 图集 / 软件纹理 |
| blend / color mod | `SDL_SetTextureBlendMode/ColorMod` | 软件混合或 565 + 1-bit mask |
| 几何图元 | `SDL2_gfx`：`boxRGBA` / `roundedBoxRGBA` / `circleRGBA` | 自己画（菜单框、圆角） |
| 缩放质量 hint | `SDL_HINT_RENDER_SCALE_QUALITY` | 固定 nearest |
| HighDPI / IME | `SDL_WINDOW_ALLOW_HIGHDPI`, `SDL_HINT_IME_SHOW_UI` | 不要 |

`scene/` 几乎每个 UI 节点都通过 `Renderer*` 画。最薄的 compatibility layer 是：**保留 `Renderer`/`Texture` 的方法签名，换成 Tab5 实现**，而不是改 `battle/` 或事件脚本。

### 4.2 Input（`app/input.hh`, `app/sdl_input.cc`）

upstream **已经有平台无关事件**：

```c++
struct InputEvent {
    uint64_t timestamp;
    InputDevice device;   // Keyboard / Controller / Text / System
    InputAction action;   // Up Down Left Right Accept Cancel Space Backspace Text Quit
    int value;
    wstring text;
    uint64_t sequence;
};
```

`SdlInputCollector` 只是把 `SDL_SCANCODE_*` / 手柄按钮映射进这个队列，并做按住连发（`InputRepeater`）。  
core **不准**吃 I2C 或 USB HID scancode；Tab5 Keyboard 的 I2C HID 包要在平台层（`tab5_platform` + `hojy_sdl`）译成 `InputAction`。

键盘映射（桌面）：方向、小键盘、Enter=Accept、Esc/Delete=Cancel、Space、Backspace。  
第一版 Tab5 **不做触摸虚拟键**。USB-A Host 外接键盘是第二阶段。

### 4.3 Audio（`audio/mixer.cc`, `channelwav.cc`, `channelmidi.cc`）

- `SDL_InitSubSystem(SDL_INIT_AUDIO)` + `SDL_OpenAudioDevice`
- 默认 `zita-resampler` 路径强制 **F32 stereo**，callback 缓冲 2048 frames
- `config.toml` 写 `sample_rate = 48000`，`opl_emulator = "dosbox"`
- 3 条 Mixer 通道（`Window` 里 `gMixer.init(3)`）
- BGM：`GAME01.XMI`…`GAME24.XMI` → `libADLMIDI`（`adlmidi.h`）实时合成
- SFX：`ATK00.WAV`…、`E00.WAV`… 共约 77 个 WAV
- `Mixer::service()` 在固定 tick 里做 fade / 文件加载 / 通道清理（主线程，锁 `playMutex_`）

设备上不链 `libADLMIDI` / 不在 P4 上实时合成 XMI。BGM 是 host 预渲染的 WAV，流式播放。

### 4.4 Filesystem

- `util/file.cc`：`fopen` / `fread` / `fwrite`，可按候选路径列表打开。这一层适合接到 FAT。
- `core/resourcemgr.cc`：`std::filesystem::directory_iterator` **扫目录**，大小写不敏感匹配必需文件。ESP32 上应改成「预生成清单 + `stat`」，不要启动时扫几千个 FIGHT/SDX 名字。
- `content/atomic_file.hh`：先写 sibling 临时文件，flush+close 后再替换。存档已走 `AtomicFile::writePair`。
- toml++ 解析 `config.toml` / `strings.toml` / `options.toml`。

### 4.5 Timing / Logging

- 主循环墙钟：`std::chrono::steady_clock`
- 窗口内部：`SDL_GetPerformanceCounter` / `SDL_GetTicks64`
- 音频 fade：`SDL_GetTicks`
- 日志：`fmt::print`、`SDL_Log`

设备端用 `esp_timer_get_time()` + `ESP_LOG*` 即可。

### 4.6 字体

`scene/ttf.cc`：

- 默认把整个 OTF **读进 RAM**，`stbtt_InitFont`，按需 raster 进 1024 atlas
- `USE_FREETYPE` 才会链 FreeType（CMake 默认 OFF）
- 字号随窗口变；1024×640 大约 26px

设备端：host 按对话和人名做子集 OTF（约 2 MB），设备上仍用 stb 栅格进小 atlas。不要上完整 FreeType，也不要把 50 MB 系统字体整份读进 PSRAM。

---

## 5. 资源与存档格式

### 5.1 必需数据（缺则 `postLoad` 失败）

`ResourceMgr::init` 的 `dataFiles`：`strings.toml`、`ALLDEF`/`ALLSIN`/`RANGER` 的 IDX+GRP、`BUILDING/BUILDX/BUILDY/EARTH/SURFACE.002`、`CLOUD`、`DEAD.BIG`、`EFT`、`ENDCOL/ENDWORD/KEND`、`HDGRP`、`KDEF`、`MMAP.COL/GRP/IDX`、`TALK`、`TITLE.BIG/GRP/IDX`、`WAR.STA`、`WARFLD`、`Z.DAT`。

可选但游戏期望存在：合并后的 `SDX`/`SMP`/`WDX`/`WMP`（`makedata` 在 host 上合并）。音乐/音效缺了只进 `missingFilesOpt_`，不阻止启动。

### 5.2 存档

`world/savedata.cc`：

- slot 0 = 新游戏模板：`RANGER` + `ALLSIN` + `ALLDEF`
- slot N = `R{N}` / `S{N}` / `D{N}` 的 IDX+GRP
- `save()` 先 snapshot，再 `AtomicFile::writePair`（6 个文件一组）
- 失败保留原状态（先反序列化到临时 `SaveData loaded`）

设备上改成边序列化边落盘，slot 文件名与记录格式保持不变。可选 autosave 还没做，不能为了它改文件名。

### 5.3 Host 预处理

`tools/makedata.cc` 把原版目录拷成运行树，并合并子地图/战场图。  
`prepare_game_data.sh` 只在 host 跑；产物进 `local/`（gitignore），再由人拷到 SD：

```
/jinyong/config.toml
/jinyong/data/                 # makedata 产物；BGM 的 GAME*.WAV 也在这里
/jinyong/data/font/chinese.otf
/jinyong/save/
```

---

## 6. 依赖图（来自源码边，不是 README 想象）

```
                    config.toml / strings.toml / options.toml
                                    │
                                    ▼
src/main.cc ──► content::loadData (Z.DAT, KDEF/TALK, WAR.*)
                                    │
                                    ▼
                         app::Application
                    ┌───────────┼────────────┐
                    ▼           ▼            ▼
            SdlInputCollector  Window     Mixer
                    │           │            │
                    │           ├─ Renderer (SDL_Renderer + SDL2_gfx + TTF)
                    │           ├─ Texture  (ARGB8888 SDL_Texture)
                    │           ├─ GlobalMap / SubMap / Warfield
                    │           └─ menus / talkbox / title
                    │
                    ▼
              InputQueue ──► Window::dispatchInput
                    │
         world / event VM / battle  ◄── content::GrpData + util::File
                    │
                    ▼
              SaveData::save/load ──► AtomicFile (IDX/GRP)
                    │
         ChannelWAV / ChannelMIDI ──► SDL audio callback
                    │
              libADLMIDI + zita-resampler（默认）
```

`battle/` 与 `event/` 不直接 include SDL。`scene/warfield_*.cc` 会画，但战斗数值在 `battle/`。

---

## 7. 内存 / 性能初估（立项时，不要当现状）

已落地的数字和做法见 [PERF_PLAN.md](PERF_PLAN.md)。下面仍是当时的量级估算。

假设设备配置：游戏逻辑 640×480、RGB565、scale=1、面板 1280×720 RGB565 单缓冲、无 MIDI。

| 项 | 估 | 放哪 | 说明 |
|---|---|---|---|
| DPI / 面板 FB | 1.8 MB | PSRAM | 官方 BSP 已建；禁止再分配同等 RGBA |
| 游戏 FB 640×480 RGB565 | 0.60 MB | PSRAM | nearest 放大到居中 960×720（1.5x，非整数）或 1x 黑边 |
| 地图离屏 640×480 RGB565 | 0.60 MB | PSRAM | 不要 2 的幂 RGBA target |
| 世界层数据 480²×5×2 | ~2.3 MB | PSRAM | 当时按整图估 |
| 世界小地图全尺寸 ARGB | ~7.4 MB | — | **默认关掉或降采样**；`show_minimap` 可配 |
| 图集 2×512×512 RGB565 | 1.0 MB | PSRAM | 替代 1024 ARGB 页 |
| 字体 atlas（预生成） | 0.3–0.8 MB | PSRAM/Flash | host 做 |
| 音频 PCM 缓冲 | 16–64 KB | 内部 SRAM 优先 | 48 kHz 立体声后置 |
| ADLMIDI 实时 | 数百 KB + CPU | — | 不做；BGM 预渲染 WAV |
| C++ 堆 / fmt / toml | 0.5–2 MB | PSRAM | 需关 RTTI/例外或只在 host 用 toml |
| ESP-IDF + BSP + FAT | Flash 1–2 MB | Flash | bring-up 远小于 10 MB factory |

32 MB PSRAM **容量够**，真正危险的是：

1. 无脑复制 desktop 的 ARGB8888 + 2 的幂 target + 全尺寸小地图
2. 内部 SRAM 被 DMA 描述符 / 任务栈 / FAT 吃光
3. `std::filesystem` 扫盘 + 把整份 OTF / 整份 MMAP.GRP 常驻

性能目标：RPG 30 fps。后来 present 已交给 PPA，见 PERF_PLAN。

---

## 8. native port 最大三个风险（及验证）

### 风险 1 — ST7121 批次必须靠官方 BSP 运行时探测，README 能力表是过期的

M5Stack 文档：2025-10-14 ILI9881C+GT911 → ST7123；**2026-04-28 ST7123 → ST7121**。开发用的这台是 ST7121。

Registry 上 `m5stack_tab5_noglib` 1.3.0 的能力表仍只写 ili9881c+st7123。  
**实际下载的源码已经支持 ST7121**（`managed_components/espressif__m5stack_tab5_noglib/src/bsp_display.c`）：

- I2C `0x55` ACK 后读触摸 FW `0x0000`：`1` → ST7121，`3` → ST7123；`0x14` → ILI9881C+GT911
- ST7121 用独立 init 表 `disp_init_data_st7121.h` 和单独的 DPI timing
- 仍调用 `esp_lcd_new_panel_st7123()`，只换 vendor_config（没有链 `esp_lcd_st7121`）
- 两种 ST 都认不出时 `assert(NULL)`，探测失败会直接崩

验证：

1. bring-up 自己在内部 I2C（G31/G32）probe `0x14` / `0x55` 并打日志（不替代 BSP）
2. 刷机后看官方 BSP 的板本探测和测试 pattern
3. 失败再用官方 `espressif/esp_lcd_st7121`。禁止手写过期 init

### 风险 2 — 桌面 ARGB / 大缓存在 32 MB PSRAM 上「能塞下但会卡死」

数字见第 7 节。世界小地图一张就 ~7.4 MB ARGB；再加多份 1280×720 RGBA（每份 3.6 MB）会把带宽打爆。

验证：

1. host 上对 `Texture::create*` / `GlobalMap` 分配打日志（先改 compatibility 层计数，不改玩法）
2. 设备 `heap_caps_get_free_size(MALLOC_CAP_SPIRAM/INTERNAL)` 每阶段打印
3. 硬规则：不允许第二份 1280×720 RGBA；游戏 FB 用 RGB565

### 风险 3 — scene/audio 与 SDL / 桌面 C++ 运行时绑死

`Window` 构造函数里直接 `SDL_Init` + `SDL_CreateWindow` + Mixer。  
`ResourceMgr` 依赖 `std::filesystem`。`fmt`、宽字符、`std::wstring` IME 文本、toml++ 都偏桌面。  
`battle/` 干净，但没有「能显示的一帧」就走不到战斗。

验证：

1. `rg "SDL_|SDL2_gfx" src`（已做）：命中集中在 `app/sdl_input.cc`、`scene/window*.cc`、`scene/renderer.cc`、`scene/texture.cc`、`audio/mixer.cc`、`audio/channel*.cc`
2. 薄 SDL + 覆盖层已经能跑标题和地图，不链桌面 SDL
3. 若当时证明必须改玩法才能显示，再评估 Plan B（轻量 DOS 模拟）。**没有这个证据，不移植 DOSBox**

---

## 9. 对 Plan B 的态度

读完源码后：**native port 仍然合理**。

- 逻辑、存档、IDX/GRP、事件 VM、战斗都是自有 C++，不是 x86 blob
- 已有 `InputEvent` 和固定步主循环
- 主要工作是换 Video/Audio/FS/Font，而不是重写金庸规则

第一帧已经出来，Plan B 仍不打开。
