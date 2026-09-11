# 架构（独立娱乐仓，独占 Tab5）

本仓与 Octoooo / AID monitor **不是同一个固件**。游戏独占 MIPI-DSI、PSRAM framebuffer 和主循环。  
以后若要「一键切换」，另开任务做双 OTA + 重启，且必须改 Octoooo 分区表。现在不做。

```
原游戏数据  --(Mac makedata/mergepic)-->  SD /jinyong/
                                              │
                                              ▼
                 HeroesOfJinYong gameplay/core
                 (content / world / event / battle / scene-logic)
                                              │
                              Platform API（本仓实现）
                     Video  Input  Audio  FS  Timing  Log
                                              │
                              platform/tab5 + ESP-IDF 5.5.5
                                              │
                                         M5Stack Tab5
```

许可证：链接 HeroesOfJinYong 则本仓按 **GPLv3**。

---

## 1. 仓库地图

| 路径 | 阶段 | 作用 |
|---|---|---|
| `third_party/HeroesOfJinYong/` | M0 | submodule，SHA 见 `UPSTREAM_ANALYSIS.md` |
| `docs/` | M0 | 分析 / 计划 / 本文件 |
| `firmware/bringup/` | M1 | 独立 ESP-IDF 工程：屏 / I2C 键盘 / SD / 喇叭打桩 |
| `firmware/game/` | M2+ | 以后的游戏固件（本会话只占位） |
| `platform/tab5/` | M1 草稿 → M2 实现 | C API，core 不知道 ESP32 |
| `host/` | 以后 | 桌面 SDL simulator，用于先跑通 core |
| `tools/` | M1 包装 | host 预处理说明 |
| `scripts/` | M1 | 不写死别人机器路径的 IDF helper |
| `local/` | 永不提交 | 游戏数据、SD 镜像、本机产物 |

---

## 2. Platform API（core 只看见这些）

头文件在 `platform/tab5/`。概念对齐 upstream 已有的 `hojy::app::InputEvent`，但用 C，避免 scene 直接 `#include <SDL.h>`。

```
Game Core
├── Game Logic / Map / Battle / Save / Resource
└── Platform API
     ├── Tab5Video     低分辨率 game FB → 居中 nearest blit → 面板
     ├── Tab5Input     I2C 0x6D → InputEvent{key, pressed, released, timestamp}
     ├── Tab5Audio     先打桩；M4 播预转换 PCM
     ├── Tab5Filesystem  FAT /jinyong/* ，fopen 语义
     ├── Tab5Clock     µs 单调钟
     └── Tab5Log       ESP_LOG
```

硬规则：

- core 不准直接读 I2C、USB HID scancode、GPIO
- 第一优先键盘：**Tab5 Keyboard 套件**，STM32，I2C 地址 `0x6D`，线在 **G0/SDA、G1/SCL、G50/INT**（官方键盘文档）。这是 I2C 包，不是「键盘插在 USB-A 上」
- HID mode = I2C 上的 `{modifier, keycode}`；Normal mode = 1 字节 row/col；Character mode = 字符串。三种都是 I2C
- USB-A Host 外接键盘：第二阶段
- 不做触摸虚拟键
- 画面：原比例、黑边、nearest-neighbor；不默认 16:9 拉伸，不默认 bilinear
- 音频后置
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
  官方 BSP 的 MIPI-DSI DPI FB（RGB565 ≈ 1.8 MB，单缓冲优先）
```

禁止：多个 1280×720 RGBA、把 SDL2 当 ESP 渲染器、从 Octoooo 复制屏 init。

屏驱兼容（官方产品变更，不是本仓私货）：

| 世代 | 显示 | 触摸 | 探测 |
|---|---|---|---|
| 早期 | ILI9881C | GT911 @ 0x14 | I2C ACK 0x14 |
| 2025-10-14+ | ST7123 | 集成 @ 0x55 | 0x55 FW==3 |
| 2026-04-28+ | ST7121 | 集成 @ 0x55 | 0x55 FW==1 |

本机记录（检测逻辑仍要兼容其它屏）：ESP32-P4 rev v1.3，16 MB Flash，32 MB PSRAM，MAC `e8:f6:0a:e2:ec:7d`，机身 **ST7121**，刷写口 `/dev/cu.usbmodem1101`。

M1 用官方 `espressif/m5stack_tab5_noglib` **1.3.0**（无 LVGL）。Registry README 能力表没写 ST7121，但该版本源码已按 FW 探测 ST7121 并换 init/DPI（仍走 `esp_lcd_new_panel_st7123`）。bring-up 会先自己 probe 再调用 `bsp_display_new*`。真机失败才上官方 `esp_lcd_st7121`，不抄 Octoooo。

---

## 4. 输入所有权

```
Tab5 Keyboard MCU (STM32 @ I2C 0x6D)
        │  独立总线 G0/G1，不要用 BSP 内部 G31/G32
        ▼
platform/tab5 InputCollector
        │  译成 InputAction
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

M1 默认设 HID mode，把 I2C HID 包打到串口。这仍然不是 USB Host。

---

## 5. 存储与存档

运行时只在 SD，不进 Git。

```
/jinyong/
  data/     makedata 产物
  music/    后置；M4 可以是预转换 PCM
  save/     R/S/D + IDX/GRP
  config/   config.toml, options.toml, strings.toml
  fonts/    host 预生成 atlas，不是整份商用字体原件入库
```

存档语义跟 upstream：`SaveData::save/load` + `AtomicFile`。M4 加成功提示与可选 autosave，不改原 slot 行为。

---

## 6. 内存账本（持续更新）

| 池 | M1 bring-up | M2 目标 |
|---|---|---|
| 内部 SRAM | IDF + 任务栈 + 小 DMA | 音频环形缓冲优先放这里 |
| PSRAM | 面板 FB ~1.8 MB | + game FB + 图集 + 地图层；禁多份 720p RGBA |
| Flash | 小 bring-up app | 游戏固件；资源在 SD |
| Resource cache | 无 | 按需 GRP，不要启动时吞下全部 FIGHT |
| Audio | 打桩 | M4 PCM |
| Game heap | 无 core | PSRAM cap 分配器 |

每次里程碑更新这张表，用 `heap_caps_print_heap_info` 证据，不靠感觉。

---

## 7. 与 Octoooo 的隔离

- 不读、不拷 Octoooo 的 LVGL UI / WebSocket / Wi-Fi / 分区表进本仓
- 官方组件从 Espressif registry 拉，不 `override_path` 到其它仓库
- 刷本固件 = 换系统。默认脚本拒绝 flash
- 负向验收：本仓的工作不得改动独立的 Octoooo 仓库
