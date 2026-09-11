# 移植计划

工具链钉死：ESP-IDF **v5.5.5**，`export.sh` 由 `IDF_EXPORT` 或 `$HOME/.espressif/esp-idf-v5.5.5/export.sh` 提供。不用 Arduino。

默认 **不刷机**。真机口：`/dev/cu.usbmodem1101`。

---

## 本会话（必须）

### M0 Research — 完成标准

- [x] 官方 Tab5 文档：<https://docs.m5stack.com/en/core/Tab5>
- [x] 官方键盘：<https://docs.m5stack.com/en/tab5/Tab5_Keyboard> + I2C 协议 PDF
- [x] 官方 BSP：`espressif/m5stack_tab5` / `_noglib` 1.3.0
- [x] submodule HeroesOfJinYong + 嵌套 deps，SHA 写入 `UPSTREAM_ANALYSIS.md`
- [x] 依赖图、内存初估、三个风险与验证方法

### M1 Bring-up scaffolding — 完成标准

独立工程 `firmware/bringup/`（不是游戏 core，不是 Octoooo）：

1. boot 日志（芯片、Flash、PSRAM、复位原因）
2. 官方 BSP 开屏 + 测试 pattern（色条 / 棋盘 / 4:3 安全框）
3. 键盘：I2C `0x6D` @ G0/G1，事件打串口（默认 HID mode 包）
4. microSD mount，尝试读 `/sdcard/jinyong/config/bringup.txt`
5. 喇叭：能编译的 ES8388 路径；失败则打桩日志，不假装有声

脚本：`./setup.sh` `./build.sh` `./flash.sh` `./monitor.sh` `./prepare_game_data.sh`  
`flash.sh` 无 `TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE=1` 时退出码 2。

---

## 后续（本会话只建目录和计划）

### M2 第一帧游戏画面

目标：能 init、能从 SD 加载真实资源、能显示一帧（title 或地图），无音频/存档也可。

步骤：

1. host 上 `BUILD_TOOLS=ON` 跑 `makedata`（需要原版目录 + 字体，调用方提供）
2. 实现 `Tab5Filesystem`：把 `util::File` 指到 `/sdcard/jinyong/data`
3. 用清单文件替代 `ResourceMgr` 的 `directory_iterator`（compatibility，尽量不改玩法）
4. 实现 `Tab5Video`：RGB565 game FB，640×480，nearest + 黑边
5. 替换 `Renderer`/`Texture` 的 SDL 体，保留方法
6. 关掉或降采样 `GlobalMap` 全尺寸小地图
7. host 预生成字体 atlas；设备不链 FreeType / 不解析整份 OTF
8. 验收：串口无缺文件；屏上能认出 title 或地图一角

### M3 可玩

主菜单、新游戏、地图移动、对话、菜单、战斗。键盘 → 已有 `InputAction`。  
事件 VM / 战斗公式不改，除非发现对齐或字节序问题。

### M4 存档 + 声音

- 沿用 `SaveData` + `AtomicFile`，SD 上 rename
- 明确「保存成功」UI
- 不频繁写卡；autosave 另槽
- SFX 先于 BGM；BGM 用预处理 PCM，不搬 ADLMIDI

### M5 产品层

launcher / splash / 电量 / 亮度 / 安全关机 / 性能计数 / crash log。  
仍是游戏固件自己的事，不和 monitor 热切换。

---

## 每步最小可验证

| 步 | 输入 | 成功长什么样 | 失败就停 |
|---|---|---|---|
| M1 编译 | IDF 5.5.5 | `idf.py build` 出 elf，或留下 `firmware/bringup/last-build.log` | 删工程装成功 |
| M1 屏 | 授权刷机 | 色条+棋盘+4:3 框 | 抄 Octoooo BSP |
| M1 键 | 键盘套件 | 串口出现 I2C 事件 | 写 USB Host 并声称套件就是那样 |
| M1 SD | FAT 卡 | mount 成功或明确无卡 | 把游戏数据提交进 Git |
| M2 一帧 | 真实 `/jinyong/data` | 能认出游戏像素 | 拉伸 16:9 / bilinear |

---

## 需要你开口才做的最少步骤

**1. 提供原版游戏目录（M2 才挡路）**

```sh
./prepare_game_data.sh /path/to/original-jinyong ./local/sd_image/jinyong /path/to/chinese.otf
# 把 ./local/sd_image/jinyong 拷到微 SD 的 /jinyong/
# 另放一个文本：/jinyong/config/bringup.txt （任意一行，供 M1 读）
```

**2. 授权刷机（会盖掉 Tab5 上的 Octoooo monitor）**

在本会话明确说：`可以刷、我接受暂时覆盖 Tab5 上的工作固件`  
然后：

```sh
# 按住 Reset 约 2s，绿灯快闪后松开 = download mode
TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE=1 ESPPORT=/dev/cu.usbmodem1101 ./flash.sh
./monitor.sh
```

不要用 `/dev/cu.usbmodem01`。不要刷 C6。不要烧 eFuse。

**3. 插卡**

FAT32 microSD，目录见上。没卡时 M1 仍应启动并在串口报 mount 失败。
