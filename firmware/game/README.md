# firmware/game

游戏固件（ESP-IDF，目标 `esp32p4`）。当前能做什么、卡上要放什么，见仓库根 [README](../../README.md)。

从仓库根：

```bash
./build.sh
./flash.sh
./monitor.sh
```

产物 `build/tab5_jinyong.bin`，完整日志 `last-build.log`。

| 组件 | 作用 |
|---|---|
| `main` | `app_main`：初始化硬件、起游戏任务、兜底错误屏 |
| `tab5_platform` | 显示（PPA）、I2C 键盘、SD、ES8388、时钟、性能计数；公开头文件 `include/tab5_platform.h` |
| `hojy_sdl` | 薄 SDL 兼容层 |
| `hojy_core` | upstream + 覆盖层：目录里的 `.cc` 按文件名替换 upstream 同名文件 |
| `hojy_fmt` / `hojy_zita` | 上游依赖的精简构建 |

屏 / 键盘自检在 [`../bringup/`](../bringup/README.md)。
