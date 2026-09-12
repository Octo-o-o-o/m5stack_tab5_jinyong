# 掌机 PC 游戏移植 Playbook

本文件是 `tab5_jinyong` 的现场规范：把做成的对应、有效做法和踩过的坑写清楚，方便以后开新仓移植别的游戏。

不是进度日报，也不替代 `ARCHITECTURE.md` / `UPSTREAM_ANALYSIS.md`。当前能做什么以仓库根 README 为准。数字凡标「本项目实测」只适用于这套数据与这台 Tab5，新项目必须重测。刷机成功只证明固件进了设备，不证明观感已确认。刷固件不会更新卡上的字库。

---

## 0. 适用与红线

适用：把「DOS/PC 原版数据 + 现代 C++ remake core」迁到带 PSRAM 的掌机（本现场是 M5Stack Tab5 / ESP32-P4）。

不适用：一上来移植 DOSBox；改其它仓库。

本项目红线（可判定）：

- 不改 `third_party/HeroesOfJinYong` submodule；设备差异只放 `firmware/game/components/hojy_core/` 覆盖层
- 不把原版资源、存档、字库原件提交 Git
- 不烧 eFuse，不刷 C6，不用 Arduino
- 不默认 16:9 / bilinear；第一版不做触摸虚拟键
- 工具链钉死 ESP-IDF **v5.5.5**
- 默认不刷机；需 `TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE=1`

---

## 1. 三层对应（必须先画，再写代码）

现场路线：

```
DOS 原版数据（IDX/GRP/Z.DAT/XMI/WAV/Big5）
        ↓ host：makedata / 字体子集 / 可选预渲染 BGM
HeroesOfJinYong gameplay/core（桌面 SDL2 + 大堆 + 瞬间 IO）
        ↓ 本仓：薄 SDL + Platform API + 覆盖层
ESP32-P4 / Tab5（PSRAM 连续块、FatFS、MIPI-DSI DPI、I2C 键盘）
```

原版和桌面 HOJY **没有 loading 屏**。PC 读 4–5 MB 无感；掌机同步读 SD 会卡几秒。缺的体验要在设备层补，不要假装 upstream 已经有。

### 1.1 职责对应

| 层 | 管什么 | 不管什么 |
|---|---|---|
| 原版数据 | 地图、对话、存档、调色板、音乐编号 | 窗口、键盘驱动、DPI |
| remake core | 事件 VM、战斗、菜单逻辑、`InputAction`、固定步主循环 | I2C、USB、eFuse、分区表 |
| 薄 SDL | `SDL_Renderer` / `Texture` / `PollEvent` / WAV 设备符号 | 玩法、IDX 解析 |
| platform | 开屏、present、I2C HID、FatFS 挂载、ES8388、单调钟 | 改 core 公式 |
| 覆盖层 | 仅设备上会炸或会卡的那几处 | 把 submodule 改成「更干净」 |

### 1.2 画面对应

| 桌面 / remake | 掌机现场（本项目） |
|---|---|
| 窗口 1024×640 或任意 | 逻辑 640×480，`scale=2.0`（地形 aux 320×240） |
| SDL 加速渲染器 + render target | 软件 ARGB8888 backbuffer |
| 纹理 `0xAARRGGBB` | **保持**，present 时再拆成 RGB565 |
| 可 bilinear / HighDPI | 固定 nearest，禁止默认 16:9 |
| 桌面 vsync 交换链 | BSP 肖像 DPI `720×1280` RGB565；横握虚拟 `1280×720` |
| 整数倍放大即可 | 最大装下的 4:3：`960×720`，左右约各 160 px 黑边 |
| 单窗口 FB | 允许第二份 **RGB565** DPI FB（约 1.8 MB）；禁止第二份 1280×720 **RGBA** |

旋转约定（本机）：`landscape(lx,ly) → portrait(ly, land_w-1-lx)`。boot 字画在肖像坐标，游戏 present 按横握虚拟坐标再转。用户说「竖着两行 LOADING」不一定是转反了，先对崩溃日志。

### 1.3 输入对应

| 桌面 | 掌机现场 |
|---|---|
| USB / 系统键盘 scancode | Tab5 Keyboard 套件：STM32，**I2C `0x6D`**，线 **G0/SDA、G1/SCL、G50/INT** |
| 直接 `SDL_PollEvent` | I2C HID `{modifier,keycode}` → 薄 SDL `KEYDOWN/UP` → 已有 `SdlInputCollector` → `InputAction` |
| IME / 姓名输入 | 默认 `no_name_input=false`；设备没有输入法，中文名写 `config.toml` 的 `default_name`。缺字时确认菜单是吞键小空框 |
| 触摸虚拟键 | 第一版不做 |

这不是「键盘插在 USB-A 上」。USB Host 外接键盘是另一阶段。开机后插上的套件要 **hotplug 重探**，只在 `app_main` 探一次会丢键。

### 1.4 存储对应

| 桌面 | 掌机现场 |
|---|---|
| cwd + 相对路径 + `chdir` | IDF FatFS 常无 `chdir`；配置/资源用绝对根，例如 `/sdcard/<game>/` |
| `std::filesystem` 扫盘 | 能用 directory_iterator 就测；不可靠则改清单 + `stat` |
| 整包 `string` 读 GRP | 按 IDX 记录流式读；超大包优先一次顺序读 blob，失败再流式 |
| 栈上反序列化整层 | 大结构 `memcpy` 进堆；游戏任务栈按 **bytes** 计，本项目 `64*1024` |
| 瞬间读 5 MB | 同步读必须留最后一帧或自做等待 UI |

本项目 SD 树（不进 Git）：

```
/<game>/config.toml
/<game>/data/                 # GRP/IDX/Z.DAT、GAME*.WAV
/<game>/data/font/chinese.otf
/<game>/fonts/chinese.otf     # 同一份字体的副本
/<game>/save/
```

`makedata` 在输出根上写 `config.toml`，数据在 `data/`。缺卡时画「插入 SD」并空转，不假装游戏在跑。

### 1.5 音频对应

| 桌面 HOJY | 掌机现场 |
|---|---|
| `libADLMIDI` 实时 XMI | 不链 ADLMIDI；XMI 走静音 stub。BGM 播 host 预渲染的 `GAME##.WAV` |
| `GAME##.WAV` 可整曲进 cache | 流式 `Channel`，常驻约 0.04 MB；加载在独立任务上 |
| 标题曲与进图曲自然衔接 | 标题为腾连续洞会主动 `playMusic(-1)`；进图必须另排复活点 |
| fade / Mixer 在逻辑 tick | 超大 WAV 不要同步整曲解码挡第一帧 present |

本项目标题曲 `GAME17.WAV` 实测约 3.87 MB。文件副本 + PCM 同时活着会再吃一份。

### 1.6 字体与文本对应

| 来源 | 真实编码 | 做错时的症状 |
|---|---|---|
| `strings.toml` | UTF-8 | 子集缺 UI 字 |
| `TALK.GRP` | 逐字节取反后再当 Big5 | 对话缺字；TTF `makeCache` 失败就 `continue` 跳过 |
| 菜单确认 | 依赖已加载 TTF | 没字 = 小空框，且 `MenuYesNo` 可能 `currIndex_ == -1` |

系统里 50 MB+ 的华文黑体/宋体不能整份进 PSRAM。host 子集；有对话包时必须按**真实编码**扩字。刷固件**不会**更新卡上的 `.otf`。

### 1.7 时间对应

| 时钟 | 含义 | 设备策略 |
|---|---|---|
| 固定逻辑 tick ≈ 60 Hz | `updateFixed` | 可保留 |
| 兼容逻辑 18.2065 Hz | 原版 BIOS PIT | 可保留；不要用 `long double`/`llroundl` |
| `limit_fps` | 渲染封顶 | 本项目 30 |
| DPI 扫描完成 | 面板 DMA | 双缓冲 flip 后必须等扫描结束再写下一 buffer |

---

## 2. 做成的事（以后默认照做）

### G-01 独立仓 + 覆盖层，不叉 core、不沾邻仓

CMake `FILTER EXCLUDE` 掉设备必须改的 `.cc`，再 `APPEND` 覆盖层。fmt 的 RISC-V 补丁拷到 build 目录改头文件，不改 submodule。upstream 警告用 `-w`，不为过编译去改玩法。

### G-02 先读 core 再估内存，再决定不走 DOSBox

`battle/` / `event/` / 存档 / IDX-GRP 是自有 C++。真正要换的是 Video / Input / FS / Audio / Font。没有「第一帧都画不出」的证据，不打开 Plan B。

### G-03 先独立 bring-up，再链游戏

`firmware/bringup/` 只证明：官方 BSP 开屏、I2C 键盘、SD 挂载、喇叭路径。游戏工程另开。第一帧失败时能区分「屏没亮」和「core 崩了」。

### G-04 薄 SDL，不搬桌面 SDL2

只提供 core 真正调用的符号。像素打包跟 remake 调色板，不跟网上「ARGB 标准布局」的直觉。

### G-05 官方 BSP + 运行时探屏

Tab5 屏世代是 ILI9881C / ST7123 / ST7121。Registry README 能力表会过期，以下载到的 BSP 源码探测为准。失败再用官方 panel 组件，不手写过期 init。

### G-06 host 预处理，设备只吃瘦数据

Mac 上 `makedata` / 字体子集 / 可选 DosBox OPL 预渲染 `GAME##.WAV`。产物进 `local/`（gitignore），人拷 SD。固件保持小 factory。

### G-07 默认拒刷 + 授权句 + 指定口

无 `TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE=1` 时 `flash.sh` 退出码 2。本机口只认 USB JTAG（现场是 `/dev/cu.usbmodem1101`，`303A:1001`），不用看起来像串口的另一个 `usbmodem`。

### G-08 大块按「连续洞」调度，不按「总量还剩多少」

标题后 PSRAM 总量可能还有约 1 MB，但最大连续块只有几百 KB。`vector::resize(84) * 49 KB` 要的是 **4.1 MB 连续**。先停标题曲、清 1024×1024 标题图集，再分配图层。BGM 腾出的约 3.7 MB **不够**图层。

### G-09 推迟到真正需要才构造

标题阶段不建 `GlobalMap`（1919×960 ARGB 小地图约 7.4 MB）。`newGame` 只建 `SubMap`；回大地图再 `ensureGlobalMap`。`show_minimap=false` 时跳过全尺寸小地图纹理。

### G-10 大 IO 流式 / 一次顺序读，大结构不进栈

GRP 按 IDX 读记录。ALLSIN（本项目约 4.92 MB）优先一次顺序读，避免 84 次 FatFS seek。`SerializableStruct` 不要在 64 KB 任务栈上放 49 KB candidate。

### G-11 present 按源宽高比取最大 dest，再为热路径写专用 blit

整数倍会把 640×480 留在屏幕中央一小块。1.5× nearest 要按面板扫描行写，源列先转 RGB565 缓存，禁止每像素乱序摸 PSRAM。

### G-12 双 RGB565 FB + vsync，不要用稀疏 hash 丢帧

`CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2`。画背面，`esp_lcd_panel_draw_bitmap` 翻转，等 `on_frame_buf_complete`。主角只占画面一小块，按 17 像素抽样 hash 会把「没变」的新帧丢掉。

### G-13 设备体验用已有 UI 字符串补，不新造体系

进图等待复用 `MessageBox` + `GETTEXT(88)`「等待」。首局跳过 `fadeIn`（第一帧 alpha=255 是全黑）。对话期间 `currEventPaused_` 时停 NPC 循环动画，避免整张地形 `drawDirty_`。

### G-14 为腾内存停的 BGM，排在第一帧地图 present 之后再解码

`newGame` 成功立刻解码 3.7 MB WAV 会把「进去」再卡几秒。先让地图出现，再播 `enterMusic` / `exitMusic` / 兜底编号。

### G-15 静态构造与桌面 C++ 运行时按芯片世代拆

P4 rev &lt; v3：C++ 静态构造时高 DRAM 未就绪。大表进 `.rodata`，OpenCC 推迟到 `app_main` 之后。`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16`，让 STL 节点进 PSRAM。

### G-16 编译差异用宏和覆盖层消化

`ftello64=ftello`。`FMT_OS=0`、`FMT_USE_INT128=0`、`FMT_USE_CONSTEVAL=0`，并补 RISC-V 缺的 `uint128 operator~`。`RateScheduler` 不用 `llroundl`。游戏任务栈单位是 **bytes**。

### G-17 数据瑕疵当警告，不当致命

本数据包 `Z.DAT` 帧表合计 713、`EFT.IDX` 714；多 1 条 unused trailing 只打日志。存档 extra-slot 84/100 不一致则忽略多余项，不为对齐去改玩法。

### G-18 诊断先于改码

用户说「先检查每个可能点，确认真实存在再改」。现象（小方框、中间一块、缺字、闪一下）各自是不同层；不要用「转屏 / 再刷一次 / 加大缩放」包打天下。

---

## 3. 踩过的坑

写法：现象 → 当时容易错判 → 真因 → 处理 → 以后怎么防。编号与 skill `references/pitfalls.md` 对齐。

### P-01 开机淡蓝 → 黑 → 再亮 / LOADING 后重启

错判：横竖屏反了。  
真因：静态构造在低内部堆上建 107 KB Big5 / OpenCC，TLSF 被踩，随后 `LoadProhibited`。LOADING 画在肖像坐标是正常的 boot 字。  
处理：表进 `.rodata`，OpenCC 延迟；`ALWAYSINTERNAL=16`。  
防：P4 rev &lt; v3 把「第一个会分配的静态对象」当启动风险清单。

### P-02 颜色整体不对

错判：调色板文件坏了、屏驱错了。  
真因：薄 SDL 把 `ARGB8888` 按错字节序打包；或 `RGB565` 宏里 `(b)&0xF8` 没加括号。  
处理：跟 remake：`0xAARRGGBB`；present 取 `r=p>>16, g=p>>8, b=p`。  
防：先画已知色条，再接游戏像素。**不要改 core 调色板来迁就错误打包。**

### P-03 键盘插上没反应

错判：USB HID 没做、键位表错。  
真因：套件在独立 I2C，不是 USB-A；只在启动探一次会错过后插；总线误用 BSP 内部 G31/G32。  
处理：`0x6D` HID mode，hotplug 重试，日志打 `mod/key`。  
防：输入验收写「后插也能用」。

### P-04 回车开局只剩小方框 / 蓝屏

错判：确认菜单逻辑坏了、缺一张图。  
真因（叠在一起）：

1. 确认菜单是空框、回车没动作（看起来像卡死）。**后来查清楚有三层，不只是缺字**：
   (a) `prepareNewGame()` 为腾连续内存 `titleTextureMgr_.clear()`，而 `Title::update()` / `makeCache()` 当时用
       `titleTextureMgr_[0] == nullptr` 整个提前返回 —— 属性页根本不重绘，确认菜单也永远不创建；
   (b) `MenuYesNo::popupWithYesNo()` 初始下标 -1，没有高亮、回车 `onOK()` 直接 return；
   (c) 字库当时确实缺字。
   正解：按 mode 分别守卫（只有 mode 0/1 需要图集）、`popup({是,否}, 0)` 预选、把字补齐。
2. 标题曲约 3.7 MB + 标题图集约 4.19 MB 占着连续洞，`SubMapLayerInfo` × 84 ≈ 4.13 MB 分配失败。
3. `grpdata` 整包读、ALLSIN 约 4.92 MB、栈上 49 KB 反序列化、一开局就建 GlobalMap。

处理：当时用 `no_name_input=true` 绕过姓名框（后来默认改回 `false`，中文名走 `default_name`）；`prepareNewGame` 先停曲、清图集；流式 GRP；图层 `memcpy` 进堆；`newGame` 只建 SubMap。  
防：开局路径列一张「同时活着的大块」表，每一块都要有释放点。

### P-05 进去了但只有中间一小块

错判：分辨率配错、旋转错。  
真因：present 只做整数倍，640×480 在 1280×720 上是 1×。  
处理：按源宽高比取能装下的最大 dest（现场 960×720），黑边，nearest。  
防：逻辑分辨率和面板分辨率分开验收。

### P-06 放大后一卡一卡

错判：逻辑 60 Hz 太快、SD 太慢。  
真因：1.5× 每帧约 69 万次跨步写正在扫描的 DPI FB。  
处理：按扫描行写的热路径；源列先转 565。  
防：非整数倍缩放默认当性能事故设计，不要先「能显示就行」再指望编译器救。

### P-07 标题有 BGM，进图没有

错判：进图曲文件缺失、混音器坏了。  
真因：为腾内存主动 `playMusic(-1)`，成功进图后没有对等的恢复；过早解码又会挡住第一帧。  
处理：第一帧地图 present 之后再按 `enterMusic` / `exitMusic` / 兜底编号解码。  
防：每处「停音乐腾内存」必须登记复活点。

### P-08 进图闪一下 + 卡几秒 + 一帧帧揭开

错判：缺官方 loading；淡入是美术需要。  
真因：`fadeIn` 第一帧全黑；同步读 ALLSIN+SMP+WAV；单缓冲边扫边写。  
处理：首局跳过 fadeIn；标题最后一帧留在屏上并弹出「等待……」；ALLSIN 一次顺序读；双缓冲。  
防：桌面没有的 loading，设备要自己补；不要用 fade 遮丑。

### P-09 对话 / 进图仍一帧帧刷

错判：还要更高 FPS。  
真因：单缓冲撕屏；客栈 NPC `frameUpdate` 约 15 Hz 把整张地形标脏。  
处理：双 FB；对话暂停时跳过 NPC 循环动画。  
防：先问「谁每帧把全屏标脏」，再谈 blit。

### P-10 进图看不到主角

错判：角色贴图没加载、图层顺序错。  
真因：稀疏 hash 认为画面没变而 skip present；或 flip 后立刻写仍在扫描的 buffer。  
处理：去掉这类 skip；flip 后等扫描结束。  
防：**小物体可见性**不能用全图抽样 hash 做闸门。

### P-11 对话少字

错判：刷屏丢字、MessageBox 裁剪。  
真因：`TALK.GRP` 当明文 Big5 子集，现场缺约 1834 字；缺 glyph 就跳过。  
处理：`.grp` 先 `~c` 再解 Big5；重新子集后必须拷到卡上 `/<game>/data/font/`。  
防：子集脚本用与运行时相同的解码；缺字时留可见框（本项目 MessageBox overlay）。固件与字体是两条发布物。

### P-12 `llroundl` 把任务栈吃穿

错判：逻辑时钟算错。  
真因：设备 newlib 的 `long double` 路径会递归炸 canary。  
处理：覆盖层用 `double` + `llround`。  
防：嵌入式禁用 `long double` 数学。

### P-13 `chdir` 成功的幻觉

错判：cwd 已是游戏根。  
真因：FatFS `FF_FS_RPATH=0`，`chdir` 失败或无效。  
处理：绝对路径加载 `config.toml` 与资源。  
防：FS 验收包含「不依赖 cwd」。

### P-14 把 `cmake -S HeroesOfJinYong` 当固件

错判：upstream CMake 能出设备二进制。  
真因：那是桌面工程。  
处理：IDF 工程只引用源文件，自写 component。  
防：两条构建入口写进 README，互不准用。

### P-15 `xTaskCreate(..., 64, ...)` 按「字」估栈

错判：64 KB 已经很大。  
真因：ESP-IDF 这个参数是 **bytes**。传 64 会直接炸。本项目游戏任务 `64*1024`。  
防：注释写死单位；音频任务另给够（本项目 8192）。

### P-16 第二份 720p RGBA

错判：双缓冲就要两份游戏尺寸 RGBA。  
真因：1280×720×4 ≈ 3.6 MB，带宽和连续洞都会死。  
处理：面板只建 RGB565 FB；游戏保持低分辨率 ARGB。  
防：内存账本把「面板 FB」和「游戏 FB」分成两行。

### P-17 刷机成功、卡还在机里、本机新文件当已发布

错判：刚编的 `chinese.otf` 已经在游戏里。  
真因：SD 在 Tab5 上时电脑写不进去。  
处理：列出「固件 / 卡数据 / 用户看屏」三张清单，分别勾。  
防：宣布「缺字已修」之前必须有卡上文件尺寸/校验，或用户看屏。

### P-18 为 713/714、84/100 去改玩法

错判：数据包和代码必须严格相等。  
真因：原版包常见 trailing / extra-slot。  
处理：多的忽略并打日志。  
防：先证明「少了会玩不下去」，再改逻辑。

### P-19 fmt / RISC-V / consteval

错判：换更新的 fmt。  
真因：`uint128` 缺 `operator~`；consteval 在设备工具链上炸。  
处理：build 目录补丁 + `FMT_USE_CONSTEVAL=0`。  
防：第三方桌面库默认当「要隔离的编译单元」。

### P-20 从邻仓抄屏 init / 用错串口

错判：那台 Tab5 已经点亮过，拷过来更快。  
真因：邻仓分区、LVGL、Wi-Fi 会污染独立娱乐固件；错误 `usbmodem` 口会写到别的设备或写失败。  
处理：官方 registry BSP；刷写口用芯片 USB JTAG。  
防：负向验收「本任务 git status 不得出现邻仓路径」。

### P-21 按方向键偶尔一下走好远

错判：键盘抖动、I2C 丢包、逻辑 tick 太快。  
真因：`InputRepeater::emitRepeats` 把**错过的每一个重复点全部补发**（20 ms 一个）。一帧卡 300 ms 就一次性入队 15 个方向事件，同帧全部应用。桌面 60 fps 看不出来。  
处理：覆盖层里到期的键每次 drain 最多发 1 个重复，下一次从**当前时刻**重新计时。  
防：**任何按 wall-clock 补发的重复/定时队列，在慢设备上都要问「掉帧时会不会补一串」**。

### P-39 动作键也自动重复，一次按下走两个界面

错判：键盘抖动 / I2C 丢包 / 事件队列被重复派发。  
真因：`InputRepeater::press()` 给**每个**动作注册自动重复，`InitialDelayMicros = 180 ms`。物理键盘上一次郑重其事的按下轻松超过 180 ms，于是一次回车产生两个 `Accept`，按久一点每帧一个。单层界面看不出来（第二下落在同一个框上），**链式界面一按到底**：存档菜单一次按下既打开了档位列表又确认了第一档；新游戏一次回车既确认了姓名又答了属性页的「是」。  
还有一半在**方向键**上：即使只让方向键重复，180 ms 仍然短于一次郑重按下，菜单一按跳两格；而 20 ms 的重复间隔是 50 次/秒，`MapWithEvent::handleKeyInput` 一事件走一整格且自己不节流，于是按住方向键每秒穿几十格。  
处理：只有 `Up/Down/Left/Right` 注册重复状态，动作键一次物理按下只产生一个事件；重复参数重定为 **400 ms 初始延迟 / 100 ms 间隔**（约 10 次/秒）。参数写在覆盖层的 `.cc` 里而不是改 upstream 头文件——那个头文件还被没被覆盖的 TU 包含，改常量等于同一个类有两份定义。用直接单测锁住：300 ms 的一次按下，**任何键**都必须恰好 1 个事件；按住 1 秒，动作键必须是 1，方向键必须落在 4–14。  
防：**导航键重复、动作键边沿触发**，是手柄/掌机 UI 的标准划分，别让 upstream 的桌面默认值一视同仁；重复参数要按「一次按下有多长」和「一格有多大」重新算，不能沿用桌面值。排查「双击」先数事件，不要先怀疑硬件——同一个根因会在几乎所有界面上以不同面貌出现。

### P-22 释放包不说明是哪个键

错判：键位表错。  
真因：I2C HID 的释放是 `{modifier, 0x00}`，不带键码；只记一个 `last_key` 时，两键重叠或丢一个释放包就会留下一个永远「按住」的键，重复器无限重复。  
处理：记录所有按下的键码，收到释放包时全部抬起。  
防：设备协议不给「哪个键抬起」时，唯一安全解是全抬，不是猜。

### P-23 BGM 切换在游戏线程上读 4 MB

错判：SD 慢、解码慢。  
真因：`Mixer::play(filename)` 在**持有 mixer 互斥锁**的情况下同步读 3.97 MB WAV 并解码。每次进/出场景都冻结约 1 s，同时饿死音频任务。  
处理：低优先级 loader 任务在锁外读+解码，做好再 `Mixer::play(Channel*)` 装上；游戏线程只发请求 + 让旧曲淡出。请求带序号，最新的赢。  
防：**为腾内存主动停曲的地方（P-07）要等 loader 放手**，否则那 4 MB 还在别人手里，连续洞照样不够。

### P-24 战斗构造函数吞下全部 FIGHT

错判：首战慢是地图加载。  
真因：`Warfield` 构造把 `FIGHT000..109` 全读（本包 92 文件 / 6.1 MB / 约 5000 条记录 + 18 次找不到文件），且常驻。  
处理：`putChars` 里按参战名单的 `headId` 按需加载，失败编号只探一次。  
防：架构文档里写了「按需 GRP」就要真的去核对构造函数，upstream 的桌面写法不会自己遵守它。

### P-25 GRP 逐条 `fseek`

错判：FatFS 慢。  
真因：记录本来是连续的，但每条都 `fseek`，newlib 的 seek 会丢掉 stdio 读缓冲，4 MB 顺序读变成约 2500 次 FS 往返。  
处理：记住文件位置，已经对上就不 seek。  
防：**索引+数据**这类格式先看偏移是不是单调连续。

### P-26 「为腾内存释放资源」的守卫写得太粗

错判：属性确认页打不开是缺字。  
真因：释放标题图集后，用「图集为空就整个 `update()`/`makeCache()` 提前返回」来防悬垂指针，把**不依赖图集的那些界面一起关掉了**。  
处理：守卫下沉到真正用到该资源的分支（这里只有 mode 0/1 的菜单切片）。  
防：写「资源没了就别画」时，先列清楚**哪些界面真的用这份资源**，不要用一个总闸。

### P-27 薄 SDL 不产生 `SDL_TEXTINPUT`，姓名框永远打不进字

错判：`no_name_input` 改成 false 就能输入名字。  
真因：I2C 键盘只给 HID scancode，薄层只译成 KEYDOWN/KEYUP；upstream 的 `handleTextInput` 走的是 `SDL_TEXTINPUT`。  
处理：`SDL_StartTextInput/StopTextInput` 变成真开关，按 HID usage id + modifier 译 ASCII 再补发 `SDL_TEXTINPUT`。  
防：**设备没有 IME**，这类输入框只能 ASCII；中文名靠 `default_name`，空回车回退默认名。键盘小键盘区的 usage id 已经绑了方向/确认，不能同时当文本。


### P-33 提示框只认确认键，方向键被吞掉就是「卡死」

错判：进城后走不动是地图没载好 / 事件卡住。  
真因：地名提示框用了 `PressToCloseThis`，而 `MessageBox::handleKeyInput` 只处理 OK/Space/Cancel。玩家到了城里第一反应是按方向键——`Node::doHandleKeyInput` 把输入全交给 `children_.back()`，框不关、人不动。  
处理：提示框改回 upstream 的「`Normal` + 淡入结束时自动撤掉」，不需要按任何键；另外让 `PressToClose*` 也接受方向键。  
防：**任何会独占输入的模态框，都要问「玩家此刻最可能按什么键」**。只认确认键的框在手柄/方向键为主的设备上等于死锁。用「进场后只按方向键能不能走」写成回归用例。

### P-34 按住的键被当成新按下，一次回车触发两个界面

错判：菜单默认项选错了 / 菜单自己乱跳。  
真因：I2C 键盘对按住的键会重复上报，薄层把每一包都当 `SDL_KEYDOWN` 且 `repeat=0`。SDL 的约定是重复要标 `repeat=1`，`SdlInputCollector` 才会丢弃。于是确认姓名的那一次回车又打到了刚创建的属性确认菜单，游戏「自己」就开始了。  
处理：已经在按下集合里的键码，`KEYDOWN` 标 `repeat=1`。另外单向门式的确认菜单预选「否」，误触只会重掷。  
防：**自造 SDL 事件时把 `repeat` 语义补全**，否则所有「按一次键、连开两个界面」的怪象都从这里来。

### P-35 双缓冲只画一块，载入中会闪出旧帧

错判：显示驱动有问题 / DSI 带宽不够。  
真因：载入前只 present 了一帧「等待……」，另一块 DPI framebuffer 还留着更早的内容——本项目里那正是开机的深蓝底色，于是载入途中闪一下蓝光。  
处理：等待帧连续 present 两次，两块 buffer 都写到；开机/兜底填充色一律改黑，让任何残留帧都不再是「一道颜色」。转场中间不要插入内容不全的帧（子地图贴图已释放时别再 present 一次黑屏）。  
防：**双缓冲下「显示一帧」= present 两次**。凡是靠「留在屏上的最后一帧」撑过长任务的设计，都要保证两块 buffer 内容一致。

### P-36 存/读档在内存里装配整个归档，一点存档就重启

错判：等待帧动了节点树 / SD 卡写坏了 / 是转场那套改动的锅。  
真因：upstream 的 `SaveData::save` 要同时持有四份同样的数据——常驻的 `gSaveData`、`SaveData snapshot = *this` 整份深拷贝、每条记录一个 `std::string` 的 `DataSet`、再把它们拼成**一整块** `groupData`。本包 84 张子地图 × 49152 B，一次 4.5 MB 的存档瞬时要 13 MB，而设备只剩 5.7 MB。`snapshot` 那行和 `stageArchive` 都没有 `try`，`std::bad_alloc` 从菜单回调一路抛穿到 `std::terminate` → abort → 蓝屏重启。读档同样：`SaveData loaded` 是第二份 4.5 MB，外加 4.1 MB 的一次性读盘块，而且 `Window::loadGame` 是**先读档再释放地图**。  
处理：存档改成边序列化边落盘（只留 84×4 B 的偏移表，峰值 0.16 MB），偏移表和记录的算法保持不变，用「读回旧代码写的槽位再写一遍、六个文件逐字节比对」证明格式没变；临时文件全部写完再整体改名提交（FatFs 不允许改名覆盖，先把目标挪到 `.bak`）。读档把释放提到读之前，失败时用**未被替换的** `gSaveData` 把地图重建回来；大归档不再整块读进内存（顺序读本来就不 seek）。  
防：**存档和读档是和大地图同一量级的内存事件**，不是 I/O 小事。数据包的记录数 × 记录大小要在移植第一天就乘出来。任何在节点派发里调用、又可能 `bad_alloc` 的路径都要自己 `catch`——抛穿一个 UI 回调，在 MCU 上就是重启。菜单只要无条件显示「存檔完畢」，失败就必须由被调方自己弹框。

### P-37 分配失败返回的是「半成品对象」，不是空指针

错判：已经写了 `if (!tex) return;`，空指针都挡住了。  
真因：`Texture::create` 在 `SDL_CreateTexture` 失败时仍然返回一个合法的 `Texture*`，只是里面的 `data_` 是空的——所有 `if (!tex)` 全部通过。更糟的是 `Texture::loadFromRLE/loadFromRAW` 直接调 `SDL_LockTexture` 且**不看返回值**，锁失败时 `pixels`/`pitch` 仍是未初始化的局部变量，接着就往那个野指针里写整张精灵。内存越紧越容易触发，表现是随机重启或莫名其妙的花屏，和触发点毫无关系。  
处理：判定统一成 `!tex || !tex->data()`；每个 `SDL_LockTexture` 都检查返回值，失败就 `delete` 并返回 `nullptr`；图集页创建失败要立刻返回，不能把空页存进 `textureContainers_` 再去建切片。  
防：**用编译器和薄层的契约对齐**。薄 SDL 的每个函数对 NULL 都要么安全要么有返回值；调用方要么全信返回值，要么一个都别信。「构造函数不会失败」这个假设在 MCU 上不成立。

### P-38 任务入口没有兜底 catch，异常就是「自己重启了」

错判：设备莫名其妙重启，一定是硬件/电源/显示驱动。  
真因：ESP-IDF 下未捕获异常 → `std::terminate` → `abort()` → panic 打到没人接的串口 → 立刻重启（`CONFIG_ESP_SYSTEM_PANIC_REBOOT_DELAY_SECONDS=0`）。UI 回调、事件脚本、音频加载任务里任何一个 `bad_alloc` 都会这样消失掉，用户只看到「一点某个菜单就重启」。  
处理：每个 `xTaskCreate` 的入口函数外层包 `try/catch(const std::exception&)/catch(...)`，把 `what()` 打到屏上并停住，而不是让它 abort。顺带用 `-Wframe-larger-than=` 扫一遍全部源码：这类代码库里 `std::initializer_list` 写的大表、`SerializableStruct` 里的 `T candidate{}`，动辄是几十 KB 的栈帧，而任务栈只有 64 KB——栈溢出的现象和 abort 一模一样。  
防：**「自己重启了」要先排除软件 abort，再怀疑硬件。** 没有串口的手持设备，兜底 catch 就是唯一的错误信息通道。

### P-40 覆盖了 `.cc`，又去动它的 `.hh`

错判：覆盖层能整文件替换，那改个头文件里的常量也一样。  
真因：引号包含按**包含者所在目录**优先解析，`hojy_core/` 里的头文件只对 `hojy_core/` 自己的 TU 生效。upstream 那些没被覆盖的 TU（`window_menu.cc`、`talkbox.cc` 等）仍然看到原版头文件——同一个类两份布局，链接器不报错，运行时随机崩。本轮两次撞到边上：给 `Window` 加辅助成员（改用捕获 `this` 的 lambda 绕开）、调 `InputRepeater` 的重复常量（改成写在覆盖的 `.cc` 里）。  
防：**覆盖层只换 `.cc`**。要调的常量、要加的辅助函数写在覆盖的 `.cc` 里。确实要覆盖头文件时，先证明没有未覆盖的 TU 也包含它。现有的 `globalmap.hh` / `submap.hh` / `talkbox.hh` 覆盖属于已验证的例外，不是可以照抄的模式。

### P-41 调用方不看返回值，被调方就得自己喊

错判：`return false` 就算把失败报告出去了。  
真因：`window_menu.cc` 无条件弹「存檔完畢」，根本不看 `saveGame()` 的返回值；而那个文件**没有被覆盖**，改不到调用方。于是一次失败的存档在用户那里是「显示成功、数据没了」。  
防：覆盖层里凡是会失败的操作，自己把失败画到屏上，不要指望调用方。静默的数据丢失比崩溃更糟。

### P-32 两张大地图想同时驻留，32 MB 装不下

错判：再挤挤就够了 / 是碎片问题。  
真因：真机报 `空間不足 5743K/5120K` —— 最大连续块 5 MB，够放任何单块，**总量**却比建大地图要的 8.9 MB 少 3 MB。挤字段、换图集页大小这类优化一共只能省 1–2 MB，填不上结构性的坑。  
处理：让世界地图和子地图贴图**轮流持有内存**，进出城各付一次加载，两边都去掉淡入淡出（要淡的那张正要被释放），改成先 present 一帧「等待……」。释放侧要同时清掉指向已删图集的缓存指针（`mainCharTex_`），并给 `render`/`tryMove`/`getOrLoadTexture` 补空判定。  
防：**先让设备报数字再动刀**。「剩余」和「最大连续块」两个数分开看：前者小是结构问题，后者小才是碎片问题，两种病的药完全不同。

### P-30 图集页按桌面尺寸开，一页 4 MB 全是空的

错判：纹理总量应该不大，精灵都很小。  
真因：`TextureMgr` 用 `RectPackWidthDefault = 1024` 开 ARGB 图集页，**一页 4.19 MB**。头像、标题、云、每张地图的人物各有一个管理器，四五页常驻，装的像素合计一两 MB。标题图集「4.19 MB」正是一页。  
处理：先量全数据集的最大精灵（本包 240 px），再把页改成 512²（1.05 MB）。打包失败要打日志，不能只是精灵不见。  
防：**图集页大小要由数据决定**，不是抄桌面默认值。`sizeof × 格子数`、`页大小 × 页数` 这两个乘法在移植第一天就该算。

### P-31 整曲解进内存的 BGM 就是一张大地图

错判：音乐是小事，先放着。  
真因：22050 Hz 立体声 45 秒 = 3.97 MB 常驻，和 `GlobalMap` 要的量级一样。音量设 0 时照样解码、照样占着。  
处理：流式 `Channel` 子类，按 8 KB 从卡上读，常驻降到 0.04 MB；不可流式的格式回落到整曲通道；音量 0 直接不加载。注意 FatFS 的 `max_files`——流式会长期占一个 FILE*。  
防：算「一首曲子多少字节」，别算「一首曲子多少秒」。

### P-28 走到门口不进大地图，屏幕上什么都没发生

错判：出口坐标错了 / 门被挡住 / 数据包不对。  
真因：`ensureGlobalMap` 抛 `bad_alloc` 被吃掉，只打了一行日志就 `return`。`GlobalMap` 一次要约 10 MB，其中 `cellInfo_`（480×480×16 B）是**一整块 3.5 MB 连续内存**，同一步还在解 4 MB 的出场音乐。  
处理：先用无头测试台证明数据与逻辑没问题（`tools/host_test/run.sh`），再压内存：`CellInfo` 打包到 10 B、最大块优先分配、把三张 0.44 MB 查表折进 `canWalk` 后释放、建图前先腾掉 BGM；物品图集失败不再致命；失败弹框而不是静默。  
防：**「什么都没发生」永远是 bug 而不是现象**。每个 `catch (bad_alloc) { log; return false; }` 都要问「用户看得见吗」。大结构先算 `sizeof × 格子数`，再决定字段类型。

### P-29 本机没法复现就只能靠上机试

错判：ESP32 的东西只能在板子上测。  
真因：游戏逻辑、数据、内存量级都与平台无关，只有 present / 输入 / 音频后端相关。  
处理：`tools/host_test/run.sh` —— 把 upstream 编到本机、**替换进本仓所有覆盖层**、桩掉 ESP 头文件，用真实卡数据跑「新游戏 → 按真实地图 BFS 走到门口 → 是否进了大地图」，并报告每阶段的最大单块分配。  
防：submodule 不改，upstream 的本机编译缺陷（libc++ `vector<bool>` 代理、`effect.cc` 帧表严格相等）在**拷贝出来的副本**上修。

---

## 4. 本项目现场数字（重测，勿当常数）

| 对象 | 本项目实测 / 配置 | 用途 |
|---|---|---|
| `ALLSIN.GRP` | 4 915 200 B | 新局图层 |
| `ALLDEF.GRP` | 440 000 B | 新局 |
| 子地图 SMP | ≈ 4 013 969 B | 进第一张图 |
| `MMAP.GRP` | 1 572 545 B | 大地图 |
| `GAME17.WAV` | 3 865 896 B | 标题曲 |
| `sizeof(SubMapLayerInfo)` | 49 152 | ×84 ≈ 4.13 MB **连续** |
| 标题图集 | 1024×1024 ARGB ≈ 4.19 MB | 进图前必须释放 |
| 世界小地图 | 1919×960 ARGB ≈ 7.4 MB | 默认关 |
| DPI FB | 720×1280 RGB565 ≈ 1.84 MB | 可 ×2，不可改 RGBA |
| 游戏 FB | 640×480 ARGB ≈ 1.23 MB | 软件渲染 |
| 标题后剩余（曾测） | 总量 ≈ 1.15 MB，最大连续 ≈ 576 KB | 解释小方框卡死 |
| 首版字体子集 | ≈ 1190 字 / 910 KB | 对话不够 |
| XOR 解码后子集 | ≈ 2 368 060 B | 须拷卡后才生效 |
| EFT | 使用 713，IDX 714 | 忽略 trailing |
| 逻辑分辨率 | 640×480，`scale=2.0`，30 fps | 设备 `config.toml` |
| 屏上画面 | 960×720 居中 | 4:3 |

芯片现场（只描述这台机器）：ESP32-P4 rev v1.3，16 MB Flash，32 MB PSRAM，屏 ST7121。检测逻辑仍要兼容其它屏。

---

## 5. 新项目开工顺序

1. 独立仓。写红线：不改邻仓、不提交原版资源、默认不刷机。
2. 读 remake/core：主循环、像素格式、输入是否已有抽象、哪些库能留在 host。
3. 列大块表：每张大纹理、每份整包资源、每条 BGM、静态表、任务栈。标「连续还是总量」。
4. 画三层对应：数据 / core / platform。标桌面没有、设备必须补的体验（loading、绝对路径、停曲复活）。
5. bring-up：屏、键、卡、喇叭。官方 BSP，运行时探硬件世代。
6. 薄平台层 + 覆盖层策略。第一帧：标题或一张已知图，颜色、比例、键、SD。
7. 开局路径做内存洞调度，再谈战斗和 MIDI。
8. host 预处理：数据转换、字体按**运行时编码**子集、可选预渲染音频。
9. 验收拆成固件 / 卡数据 / 看屏。缺哪一层就说哪一层，不把 flash 成功写成可玩。

检查清单的 agent 版见 skill `references/new-project-checklist.md`。

---

## 6. 验收纪律

- 编译成功 ≠ 能玩。刷机成功 ≠ 颜色对、键能用、能开局。
- 本机日志不能代替用户看屏。用户照片是现象，根因仍要串口 + 堆信息。
- `heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)` 与 `free_size` 一起看。
- 改像素格式、键盘热插拔、EFT 条数、`llroundl`、`chdir`、确认菜单跳过、extra-slot 之前，先证明与现场症状有关。本项目这些点在修完后默认冻结，除非新授权。
- 未运行的测试、未拷卡的字体、未看屏的流畅度，分别写「未验」。

---

## 7. 不要再做的「顺手」

- 为过 `-Werror` 去改 submodule 玩法
- 为「更像桌面」开 HighDPI、IME、bilinear、全尺寸小地图
- 为「更流畅」上 720p RGBA 交换链或 PPA，在颜色/比例/开局还没稳之前
- 把 Wi-Fi、个人绝对路径、secret 写进会提交的文件
- 在授权句之外刷机，或刷 C6 / 烧 eFuse
