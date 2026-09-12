# 性能方案（Tab5 金庸群侠传）

面向「已经能跑、但不流畅」的阶段。逻辑分辨率 640×480、`scale=2.0`（内部地形 320×240）、`limit_fps=30`、面板 720×1280 竖屏、DPI 70 MHz（约 65 Hz 扫描）、ESP32-P4 rev v1.3、IDF 5.5.5。

只写**本仓能控制**的部分：平台层、薄 SDL 层、覆盖层、工程配置。不改 upstream 玩法。

---

## 1. 每帧到底在做什么（改前账本）

```
RLE 地形重建 (移动/脏时)   320×240 ARGB ×2   2×307 KB memset + 约 500 次 renderRLE
    ↓
SDL_RenderClear            640×480 ARGB      307 200 次 32 位写
SDL_RenderCopy ×2          320×240 → 640×480 2×307 200 次 blend_pixel
renderChar / 文字 / 弹窗   若干小矩形
    ↓
tab5_video_present_argb    640×480 → 旋转 90° + 1.5× → 960×720 RGB565
    ↓
esp_lcd_panel_draw_bitmap + 阻塞等扫描完成
```

| # | 位置 | 问题 | 为什么贵 |
|---|---|---|---|
| 1 | `present_640x480_tab5` | CPU 做旋转 + 1.5× 最近邻 | 源按**列**读：每个源列 480 次跨 2560 B 取样，每次一条独立 cache line。读 1.2 MB 的 buffer 实际从 PSRAM 拉约 19 MB，再写 691 200 个 16 位 |
| 2 | `SDL_RenderCopy` | 每像素一次运行期整数除法 + 每像素 4 次 colormod 除法 + 每像素越界判断 | `dx*src.w/d.w` 在 RISC-V 上约 20–30 cycle；colormod 在 255 恒等时照算。每帧两次全屏 307 200 像素 |
| 3 | `sdl_fill_argb`（半透明）| 每像素 `sdl_plot` → `sdl_pack` → `blend_pixel` | `Mask` 淡入淡出每帧全屏 307 200 次非内联调用 |
| 4 | `Application::run` | 渲染完才判断能不能 present | `canRender()` 为假时整帧渲染白做；渲染耗时略小于帧间隔时平均每 present 渲染 1.3+ 次 |
| 5 | `flip_drawn` | present 后立刻阻塞等扫描（最多约 15 ms），CPU 空转 |
| 6 | `tab5_input_poll` | 每次 `SDL_PollEvent` 发一次 I2C（100 kHz 约 0.4 ms），队列空时也发；INT 引脚配好没用 |
| 7 | `GrpData::readRecord` | 每条记录一次 `fseek`；newlib 的 seek 丢 stdio 读缓冲。SMP 4 MB / 约 2500 条都在这条路上 |

## 1b. 流程检查（进游戏 / 读档 / 切场景 / 对话 / 战斗 / 音乐）

| # | 流程 | 问题 |
|---|---|---|
| 8 | **音乐切换** | `Window::playMusic` → `Mixer::play(filename)` **同步**读 3.97 MB WAV 并解码，**而且全程持有 mixer 互斥锁**。每次进/出场景、战斗结束都冻结约 1 s，同时饿死音频任务 |
| 9 | **按方向键有时突然走好远** | `InputRepeater::emitRepeats` 把**错过的每个 20 ms 重复点全部补发**。一帧卡 300 ms 就一次塞 15 个方向事件 → 瞬移 15 格 |
| 10 | **按键可能卡住** | `sdl_events.c` 只记一个 `s_last_key`。两键重叠或丢一个释放包，`InputRepeater` 就永远重复那个方向 |
| 11 | **首次进战斗** | `Warfield` 构造函数把 `FIGHT000..109` 全部读进来：本数据包 92 个文件 / 6.1 MB / 约 5000 条记录 + 18 次找不到文件，且 6 MB 常驻 PSRAM。与 `ARCHITECTURE.md`「按需 GRP，不要启动时吞下全部 FIGHT」冲突 |
| 12 | **读档后没有音乐** | `newGame` 有 `gPendingEnterMusic` 复活点，`loadGame` 没有，标题曲会一直放到下次换场景 |
| 13 | **战斗每步重建** | `Warfield::render` 每次 `drawDirty_` 都新建并清零一个 4096 项 `vector`（16 KB） |

---

## 2. 已落地的改动

### P1 呈现路径交给硬件 —— `tab5_platform/tab5_video.c`

- **P1-1 PPA 硬件 blit**。ESP32-P4 的 PPA SRM 原生支持「ARGB8888 → 缩放（1/16 粒度）→ 旋转 → RGB565」，DMA 直接写目标画面里的一个 block。
  源 640×480，缩放 1.5（= 24/16），旋转 90° CCW，输出 block 720×960 落在 720×1280 FB 的 `(0,160)`。
  CPU 每帧 69 万次写和 19 MB 读放大整块消失。
- **P1-2 自动回落**。PPA 注册失败 / 缩放比不可表示 / 任何一次调用失败 → 走原来的 CPU 路径，两条路径的比例与黑边算法完全一致。
- **P1-3 cache 一致性**。黑边填充和 boot 文字是 CPU 写 FB、PPA 是 DMA 写 FB，填完补 `esp_cache_msync(C2M)`。
  改前靠「3.6 MB 填充自然把 128 KB cache 挤干净」，是巧合不是设计。
- **P1-4 等扫描后置**。从「present 之后立刻等」挪到「下一帧开画之前等」，扫描（约 15 ms）与游戏逻辑 + 软件渲染重叠。翻页语义不变。
- 顺手：全屏纯色填充改 32 位批量写。

用 **blocking** 模式：非阻塞要再加一份 1.2 MB 后台双缓冲才安全，收益不抵内存代价。

### P2 软件光栅热路径 —— `hojy_sdl/sdl_video.c`

- **P2-1 `SDL_RenderCopy`**：目标矩形一次裁剪到位；源坐标改误差累加器；colormod 恒等在循环外判一次；每像素三分支（`a==0` 跳过 / `a==255` 直写 / 其余走原公式）；1:1 复制走专用行循环（文字、弹窗缓存都在这条）。
- **P2-2 `sdl_fill_argb`**：`a==255` 批量写、`a==0` 直接返回、其余走专用混合行循环。直接受益：`Mask` 淡入淡出、`boxRGBA`、`SDL_RenderClear`。
- **语义逐位不变**，两点已在本机用穷举程序证明（见第 4 节）：
  - 误差累加器对每个 dx 与 `src.x + dx*src.w/d.w` 完全等值；
  - 新的 `put_plain/blend_over` 与旧 `blend_pixel`（恒等 colormod）逐位相同；
  - `a==0` 仍然**不写**目标 —— upstream 的 RLE 空洞依赖这个行为，不能改成 memcpy。

不动 `blend_pixel` 的数学本身：`/255` 已被编译器变成乘加移位，换近似公式只会换来颜色偏差（`P-02` 踩过）。

### P3 帧调度 —— `hojy_core/application.cc` + `window.cc`

- 判定提到渲染之前：`Application::run` 先问 `renderer->canRender()`，不能 present 就直接睡到下一帧，不渲染。
  `Window::flush()` 不再重复问，`canRender()` 每轮只推进一次帧计划。
- `Title::showEnterWait` 那种「显式渲染一帧」的路径不受影响（闸门在 Application，不在 `Window::render`）。

### P4 输入 —— `tab5_platform/tab5_input.c` + `hojy_sdl/sdl_events.c` + `hojy_core/input_repeat.cc`

- **P4-1 INT 快路径**：INT（G50，上拉，低有效）没拉低时最多 **30 ms** 才发一次 I2C。
  INT 没接或坏掉只退化成 30 ms 轮询，键盘不会变哑；一次轮询本来就排空 MCU FIFO（最多 32 条），不漏键。
- **P4-2 释放不再丢键**：记录所有按下的键码，收到释放包时把它们**全部**抬起。协议本身不说明是哪个键抬起，全抬是唯一不会残留的做法。
- **P4-3 重复不再补发**（解决「突然走好远」）：到期的键每次 drain 最多发 **1 个**重复，下一次重复从**当前时刻**重新计时。
  走路速度变成 `min(帧率, 50/s)`，一次卡顿只值一步，不再是一串。按下/释放语义不变。
- I2C 频率保持 100 kHz：没有官方文档证明这颗 STM32 支持 400 kHz，不猜。

### P5 音乐切换异步化 —— `hojy_core/window_audio.cc` + `channel.cc`

- 游戏线程只做两件事：让当前曲子开始淡出（500 ms）、把文件名交给低优先级 `hojy_bgm` 任务。
  读卡 + 解码在**锁外**完成，做好了再用 `Mixer::play(Channel*)` 装上并淡入。游戏循环不再阻塞。
- 请求带序号，**最新请求胜出**，读到一半被超越的直接丢弃。
- `playMusic(-1)`（`prepareNewGame` 为腾连续内存主动停曲）会**等 loader 放手**再返回，
  否则那 4 MB 还在 loader 手里，`P-04` 的连续洞又不够了。
- 没有 `GAME##.WAV` 只有 `.XMI` 时**仍走 upstream 同步路径**，MIDI/stub 的通道选择行为不变。
- `channel.cc` 的文件缓存加了互斥锁：现在游戏线程（音效）和 BGM 任务会同时碰它。

### P6 读档补上音乐复活点 —— `hojy_core/window.cc`

`loadGame` 进子地图时按 `enterMusic / exitMusic / 兜底 16` 设 `gPendingEnterMusic`，
和 `newGame` 一样排在第一帧 present 之后解码。回大地图分支不动（保持 upstream 行为）。

### P7 战斗资源按需 —— `hojy_core/warfield_load.cc`（新覆盖层）

`FIGHT###.GRP` 改成**首次有该 headId 的角色参战时**才加载，在 `putChars` 里按参战名单拉取。
首场战斗从「92 文件 / 6.1 MB / 约 5000 条记录」降到只读参战者用到的那几套，常驻 PSRAM 同步下降。
失败的编号记一次，不重复探测。战斗规则一行没动。

### P8 其它 —— `grpdata.cc` / `warfield_render.cc` / `sdkconfig.defaults` / CMake

- GRP 记录是连续的：位置已经对上就不 `fseek`，整个 GRP 退化成一次顺序读。
- 战斗的 `effectOverlay` 改成复用的静态缓冲，不再每次重建分配清零 16 KB。
- `CONFIG_SPIRAM_MEMTEST=n`：每次开机少走一遍 32 MB，只影响开机时间。
- `hojy_sdl` 与 `tab5_platform` 两个纯循环组件加 `-O3`，别的组件不动。

### P9 可观测（先量后调）

`tab5_prof`：累积 render / blit / vsync-wait 三段耗时与帧数，每 5 s 打一行
`perf fps=… render=…ms blit=…ms vsync=…ms frames=…`。
没有真机数字之前，后续任何调优都是猜。

### P10 走出子地图：切大地图的内存账（本机实测）

「走到门口不进大地图」不是数据也不是逻辑问题 —— `tools/host_test/run.sh` 在本机用卡上的数据
从新游戏一路走到门口，**PASS**，说明出口格 (45,29) 可达、`SubMap::tryMove` 的判定正确。
真正的瓶颈是 `ensureGlobalMap` 一次要的连续 PSRAM：

| 项 | 改前 | 改后 |
|---|---|---|
| 门口那一步的总分配 | 10.24 MB | **6.44 MB** |
| **最大单块**（决定能不能成） | 3.79 MB | **2.20 MB** |
| 其中 `cellInfo_`（480×480） | 3.52 MB | 2.20 MB |
| 建完后常驻 | — | 再释放 1.32 MB |

做了四件事：

1. `GlobalMap::CellInfo` 的 `int buildingDeltaY` → `std::int16_t`。
   4 字节字段加对齐填充在 230 400 个格子上是 1.32 MB 的**同一块**连续内存，
   而这个值从来不超过几百。加 `globalmap.hh` 覆盖层（只有本仓的 `globalmap.cc`/`window.cc` 用它，无 ODR 风险）。
2. `cellInfo_` 改为在五个 460 KB 的 `.002` 向量**之前**分配，让最大的那块面对最不碎的堆。
3. `buildx_`/`buildy_`/`building_` 参与的可行走判定是加载后就不变的纯函数，
   在 `GlobalMap::load()` 末尾折进 `canWalk`，然后释放这三张表（常驻 −1.32 MB）；`tryMove` 只看 `canWalk`。
4. 同一步还会解出场音乐（约 4 MB）。首次建大地图前先 `playMusic(-1)` 并等 loader 放手，
   建完把曲子登记到 `gPendingEnterMusic`，第一帧大地图 present 之后恢复（P-04/P-07 的老模式）。

顺带修了两处会让失败变成「什么都没发生」的地方：

- 物品图集（1.22 MB）分配失败**不再**让整次切图失败，只是没有物品图标；
- `ensureGlobalMap` 失败时弹 `GETTEXT(69)` 并打 `mem[globalmap_oom]`，不再静默。

还修了一个 upstream 的潜在越界：建筑 `deltaY` 偏移会在地图左上角算出
`cellInfo_[(j-deltaY)*mapWidth_+(i-deltaY)]` 的**负下标**（写到 vector 之前），
在设备上就是静默堆破坏、之后莫名其妙 `bad_alloc`。

### P11 让它能长期跑下去：常驻内存与健壮性（本机实测）

第一轮把「走到门口那一步」压下来之后，真正的上限是**稳态常驻**。用
`tools/host_test/run.sh` 逐项量出来并修掉：

| 项 | 改前 | 改后 | 做法 |
|---|---|---|---|
| BGM 常驻 | 3.97 MB | **0.04 MB** | 流式读卡（`channelwavstream.cc`），不再整曲解进 PSRAM |
| 纹理图集页 | 4.19 MB/页 × 4–5 个管理器 | **1.05 MB/页** | `TextureMgr` 图集从 1024² 改 512²（全数据集最大精灵 240 px，`texture.cc` 覆盖层） |
| `GlobalMap::cellInfo_` | 3.52 MB | **2.20 MB** | `CellInfo` 打包到 10 B |
| `buildx_/buildy_/building_` | 1.32 MB 常驻 | **0** | 折进 `canWalk` 后释放 |
| `miniPanelTex_` | 0.26 MB × 每张地图 | **0** | `show_map_mini_panel` 关闭时不分配（`map.cc` 覆盖层） |
| 战斗 FIGHT 集 | 跨场次累积到 6.1 MB | 只留本场 | 每次 `Warfield::load` 先释放上一场 |
| `music_volume = 0` | 仍解码 4 MB | **不加载** | 关音乐现在真的省内存 |

**实测结论**（卡上真实数据，`tools/host_test/run.sh`）：

```
walk to door + exit   峰值 8.2 MB，最大单块 2.20 MB（cellInfo_）
soak 25 轮 进/出子地图  heap +0.000 MB，textures +0.000 MB   ← 无泄漏
bgm stream            45 s 曲子读出 111.5 s（正确循环），常驻 0.04 MB，最大块 0.02 MB
battle                enterWar 成功，纹理 +0.66 MB，堆 +1.89 MB（有界）
```

仍然常驻的两块大头，暂不动：**字体文件 2.31 MB**（stb_truetype 需要整份 buffer 常驻）和
**SMP 贴图数据约 4 MB / ALLSIN 图层 4.13 MB**（后者是可变游戏状态，存档要写回）。

同一轮做的健壮性修复（都是「失败会变成看不见的怪现象」那一类）：

- `enterSubMap` 对 `subMapId` 越界/`subMapInfo` 为空/`SubMap::load` 失败全部有判定，
  否则事件脚本里一个坏 id 就是淡出中途空指针重启；失败弹框而不是卡在黑屏。
- `endscreen()` / `playerDie()` 不再无条件解引用 `subMap_`。
- 键盘 I2C 连续读失败 5 次就解绑设备并重新探测，**拔插后能自己回来**。
- 挂卡后创建 `/jinyong/save`，否则 FatFS 不会自己建目录，存档会静默失败。
- FatFS `max_files` 从 BSP 默认的 5 提到 10：流式 BGM 会长期占一个 FILE*（淡入淡出期间两个）。
- `TextureMgr` 打包失败时打日志说明是哪个 id、多大，不再只是「精灵看不见」。
- 图集页创建与 `GlobalMap` 前后都会打纹理总量，PSRAM 里有多少是像素一目了然。

查过但**不用改**的：`FixedTickAccumulator` 的 `maxCatchUpTicks = 8` 已经防住了
「长加载之后逻辑一次性快进」；`SDL_GetTicks()` 的 32 位回绕在 mixer 里是按差值算的，安全。

---

## 3. 明确**不做**的（反思：避免过度设计 / 避免无证据的赌）

| 想过但不做 | 原因 |
|---|---|
| **CPU 360 → 400 MHz** | 查过 IDF：P4 rev < 3.0 要 `CONFIG_ESP_FORCE_400MHZ_ON_REV_LESS_V3`，文档写明只对 Espressif 另行认证过的芯片有效，否则「不稳定或可靠性下降」。本机 **rev v1.3**，不开 |
| L2 cache 128 KB → 256/512 KB | 要从 576 KB DIRAM 再割 128/384 KB（静态已占 146 KB，hojy 任务栈 64 KB）。**没有真机 `heap_caps` 证据前不动**，改坏了是难查的 OOM。P1 之后 CPU 的 PSRAM 压力已大幅下降，收益也变小。配置行与回滚写在第 4 节 |
| `SDL_RenderCopy` 也走 PPA | 两层地形之间夹着 `renderChar`，PPA blend 不能同时缩放；小纹理（文字）每次调用的 cache 同步开销反而更大 |
| 后台缓冲双缓冲 + PPA 非阻塞 | 多 1.2 MB PSRAM 换约 5 ms 重叠，没有内存余量证据前不换 |
| 音频缓冲 2048 → 512/1024 帧 | 降的是**延迟**不是帧率，同时把「地图加载时 ready 缓冲的容错」从约 370 ms 砍一半。当前问题是帧率，不动 |
| Flash 40 → 80 MHz | `SPIRAM_FLASH_LOAD_TO_PSRAM` 下只影响约 0.1 s 开机拷贝 |
| 关 `COMPILER_OPTIMIZATION_ASSERTIONS` | 开发期把断言关掉，换那点驱动开销不值 |
| 地形重建改脏矩形 / 滚动 | 走路每步重建整张 320×240 确实占 CPU，但那是 upstream 的绘制模型。先拿 P9 的数字看它到底占多少，再决定是否单开任务 |
| `blend_pixel` 换近似除法 | 会引入颜色偏差 |
| 热函数搬进 IRAM | XIP-from-PSRAM + L1 I-cache 下这几个小循环本来就常驻 |

### P12 大地图与子地图贴图互斥（真机数字逼出来的结构改动）

上机报的数字是决定性的：**出城那一刻 PSRAM 只剩 5.74 MB，最大连续块 5.0 MB**，
而建大地图的峰值约 8.9 MB。不是碎片问题，是**总量真的不够** —— 这块 32 MB 上
「大地图（约 7.8 MB 常驻）」和「SDX/SMP 全套子地图贴图（约 5.1 MB）」装不下。

先排除过的省法：

- `subMapLayerInfo`（ALLSIN，4.13 MB）**不能**按需流式化 —— 事件脚本会跨地图写地块
  （`map_event_extended.cc` / `map_event_interaction.cc` / `map_event_character.cc`），是可变存档状态。
- 字体 2.31 MB（stb_truetype 要整份 buffer）、双 DPI FB 3.52 MB（去掉就撕屏）、
  HDGRP 头像图集 2.1 MB（对话要用）—— 都不能动。

所以改成**两者轮流持有内存**：

| 时刻 | 释放 | 加载 |
|---|---|---|
| 出城（`exitToGlobalMap`） | `SubMap::releaseTiles()`：SMP 贴图 + 精灵图集 + cellInfo（约 5.1 MB） | 建 GlobalMap（约 7.8 MB） |
| 进城（`enterSubMap` 来自大地图） | `delete globalMap_`（约 7.8 MB），位置先写回 `baseInfo` | 重读 SMP（约 4 MB） |
| 读档（`loadGame`） | 按存档所在的那一侧，释放另一侧 | — |

两个方向都去掉了淡入淡出（要淡的那张图正要被释放），改成先present一帧 `等待……`。
代价是每次进出城多一次加载；这是大地图能存在的前提。

配套的安全措施：

- `SubMap::render` / `tryMove` 在贴图被释放时直接返回，不再索引空的 `cellInfo_`。
- `Map::getOrLoadTexture` 加了 `texData_` 边界判定（upstream 无判定）。
- `submap.hh` 覆盖层加 `releaseTiles()`；`mainCharTex_` 指向被删图集，必须同时置空。
- 失败提示不再用 `GETTEXT(69)`（「讀檔失敗」，语义完全不对），改成
  `空間不足 <剩余>K / <最大连续块>K <原因>`，原因是 `oom` 还是 `MMAP header` 直接写在屏上。

**本机 25 轮进出城浸泡：堆 +0.000 MB、纹理 +0.000 MB**；战斗仍能进。
（中途量到每轮 24 KB 的「泄漏」是测试没按键关掉地名提示框造成的，不是代码问题。）

### P13 转场统一：长任务前先把「等待……」摆上屏

`showBusy()`（`window.cc`）在任何会看起来像死机的操作之前 present 一帧
「等待……」，覆盖：出城建大地图、进城重读 SMP、进战斗（首场建 `Warfield` +
每场约 1.6 MB 战场贴图）、存档（约 4.5 MB 图层写卡）。

判过的两个顾虑，都不成立：

- **派发过程中建/删节点**：`Node::doUpdate` / `doRender` 都是先
  `const auto children = children_;` 再遍历，`doHandleKeyInput` 在递归前取好单个指针。
  所以一个**刚创建、没有别处引用**的节点，建完即删不会扰动正在进行的遍历。
  这个模式 `Title::showEnterWait` 早就在用（从 `handleKeyInput` 里调），真机验证过。
- **等待框吃内存**：`MessageBox::layoutText` 结尾把 `width_/height_` 收缩到文字框，
  「等待……」的 cache 约 160×40×4 = 25 KB，不是一屏。

同时定下两条转场纪律：

1. **双缓冲下「显示一帧」= present 两次**。只写一块 buffer，另一块会在后续翻页时
   把旧内容闪出来（本项目闪的是开机的深蓝底）。开机/兜底填充色因此一律改黑。
2. **转场中间不插入内容不全的帧**。进城时子地图贴图已释放，就不要再 present
   一次黑屏——上一帧「大地图 + 等待……」留在屏上直到新场景淡入即可。

### 仍然存在、本轮**没有**处理的（已知缺口）

1. `enterWar` / `saveGame` 现在有等待帧，但**没有进度**；4.5 MB 存档在慢卡上仍是数秒静止。
2. 走路时地形重建的绝对开销未知，等 `perf` 行。

---

## 4. 验收

**已在本机验证**
- `idf.py build` 通过（IDF 5.5.5 / ESP32-P4），无新增告警；`tab5_jinyong.bin` 1 978 240 B（0x1e3180）。
- DIRAM 静态 146 338 B / 576 464 B，剩 430 126 B（与改前 146 058 B 基本持平）。
- 光栅等价性穷举：源坐标映射 9 409 6000 组全等、alpha 混合 3 932 160 组逐位相同（脚本见会话记录，不入库）。

**必须上真机才算数（未验）**
- **PPA 旋转方向**：`PPA_SRM_ROTATION_ANGLE_90` 是**逆时针**。按 CPU 路径的映射
  `panel(px,py) = (ly, land_w-1-lx)` 推导正好对上。若上机看到画面转了 180°，
  把 `tab5_video.c` 里的 `PPA_SRM_ROTATION_ANGLE_90` 改成 `..._270`。
- **PPA RGB565 通道序**：若整体偏色（红蓝互换），同一处 `cfg.rgb_swap = true`。
- **帧率 / 各段耗时**：看串口 `perf fps=…` 那行。
- 方向键长按手感、进出场景时的音乐淡入、首场战斗耗时。

**回滚点**
- PPA：`s_ppa` 注册失败会自动走老路；要强制关掉，注释掉 `tab5_video_start` 里的 `ppa_register_client`。
- 异步 BGM：`window_audio.cc` 里 `ensureMusicLoader()` 直接 `return false` 即回到同步路径。
- 按需 FIGHT：`warfield_load.cc` 从覆盖层列表里去掉即回到 upstream 全量预读。
- L2 cache（若以后要试）：`sdkconfig.defaults` 加 `CONFIG_CACHE_L2_CACHE_256KB=y`，开机看 `mem[boot] internal=`，低于约 150 KB 就退回。

---

## 5. 相关坑位

见 [HANDHELD_PORT_PLAYBOOK.md](HANDHELD_PORT_PLAYBOOK.md)：`P-05`/`P-06`（缩放与 present）、`P-07`（停音乐要登记复活点）、
`P-09`（谁把全屏标脏）、`P-10`（翻页后不能立刻写仍在扫描的 buffer）、`G-10`（大 IO 顺序读）、`G-11`/`G-12`。
