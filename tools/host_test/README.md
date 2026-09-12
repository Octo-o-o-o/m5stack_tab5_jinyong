# 本机无头测试台

在 Mac 上跑**这个移植自己的游戏逻辑**，不用设备、不开窗口、不出声。

```bash
tools/host_test/run.sh "/Volumes/NO NAME/jinyong"    # 直接对着卡
tools/host_test/run.sh local/sd_image/jinyong        # 对着本机镜像
```

## 它是什么

把 upstream HeroesOfJinYong 编到本机，**把 `firmware/game/components/hojy_core/` 下的每一个覆盖层替换进去**，
ESP-IDF 的头文件用 `shim/` 里的桩顶掉（`esp_log.h`、`tab5_platform.h`、`freertos/*`），
然后用真实数据跑一遍：新游戏 → 按真实地图 BFS 出来的路线走到门口 → 检查是否真的进了大地图。

顺带报告每个阶段要多少内存，**包括最大单块**——那是设备上必须存在的一整块连续 PSRAM。

它跑七件事：

0. **按键重复**：按住 1 秒，动作键必须恰好出 1 个事件，方向键必须 > 1（不依赖卡数据，最先跑）
1. **存档往返**：把卡上已有的第 1 槽读回来、用当前代码重写到第 2 槽，六个文件必须**逐字节相同**——存档写法改过之后，这是格式没变的证据（第一次跑时对比的是上一版代码留下的文件）
2. **新游戏 → 走到门口 → 进大地图**（路线由 `probe_reach.py` 在真实地图上 BFS 出来）
3. **菜单读档来回各一次**：大地图 → 子地图 → 大地图，报出各自峰值
4. **流式 BGM**：读过一整圈，确认正确循环且常驻是几十 KB 而不是 4 MB
5. **反复快速进出子地图 25 轮**：堆和纹理都不许增长
6. **进一次战斗**：确认 `Warfield` 能建起来且用量有界

```
>>> Window ctor (title)      new=4.95 MB  peak-live=4.40 MB  largest-block=2.31 MB
>>> SaveData::save           new=0.48 MB  peak-live=0.16 MB  largest-block=0.09 MB
   S1.GRP -> S2.GRP  4128768 vs 4128768 bytes  identical
>>> Window::newGame (SubMap) new=4.64 MB  peak-live=4.51 MB  largest-block=0.12 MB
>>> walk to door + exit      new=8.30 MB  peak-live=7.25 MB  largest-block=3.95 MB(申请)
>>> Window::loadGame         new=15.29 MB peak-live=4.84 MB  largest-block=3.95 MB
== bgm stream ==   produced 111.5 s of a 45 s track, resident 0.04 MB
== soak ==         25 cycles: heap +0.000 MB, textures +0.000 MB
== battle ==       enterWar(0)=1, textures +0.66 MB, heap +1.89 MB
RESULT: PASS
```

`new=` 是这一段里**累计申请**的量，`peak-live=` 才是同时活着的峰值；判断装不装得下看后者和 `largest-block`。

`[big alloc]` 行会把任何一次超过 2 MB 的分配连同调用栈打出来 —— 那就是设备上必须
存在的一整块连续 PSRAM。注意 macOS 的 `malloc_size` 对大块会往上报，**以「申请」那个
数为准**。

## 它**不**覆盖什么

平台层一概不测：PPA present、DPI 翻页、I2C 键盘、ES8388，以及**真实的 PSRAM 碎片**。
这些只能上机。本测试台回答的是「是不是数据/游戏逻辑的问题」和「这一步要多少内存」。

## 依赖

- Homebrew 的 `sdl2-compat`（或 `sdl2`）；用 `SDL2_PREFIX=` 覆盖
- cmake：走 `scripts/find_host_cmake.sh`，没装也能用 ESP-IDF 自带的那个
- 中间产物默认在 `$TMPDIR/tab5_jinyong_hosttest`，用 `HOST_TEST_WORK=` 改

## submodule 不被修改

upstream 有两处本机编不过、与本移植无关的问题，脚本在**拷贝出来的副本**上修：

1. `battle/engine.cc` 遍历 `const vector<bool>&` 拿到的是 libc++ 的代理引用，`&value` 不是 `const void*`（libstdc++ 下 `const_reference` 是 `bool`，所以设备上编得过）。
2. `scene/effect.cc` 要求 `index == dset.size()`；本数据包 `Z.DAT` 帧表合计 713 而 `EFT.IDX` 有 714，设备覆盖层已经容忍，本机副本同样放宽。

## 单独的数据探针

不编译也能查地图数据：

```bash
python3 tools/host_test/probe_exit.py  "/Volumes/NO NAME/jinyong"   # 起点/出口/是否被挡
python3 tools/host_test/probe_reach.py "/Volumes/NO NAME/jinyong"   # 连通性洪泛 + 出门路线
```

两者都按 `scene/submap.cc` 里 `SubMap::load` / `tryMove` 的**同一套阻挡规则**计算，
结构体布局取自 `world/submap.hh`、`content/factors.cc`。
