# firmware/game

默认要编、要刷的游戏固件（ESP-IDF，目标 `esp32p4`）。当前能做什么、卡上要放什么，见仓库根 [README](../../README.md)。

从仓库根：

```bash
./build.sh
TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE=1 ESPPORT=/dev/cu.usbmodem1101 ./flash.sh
```

产物 `build/tab5_jinyong.bin`，完整日志 `last-build.log`。

| 组件 | 作用 |
|---|---|
| `tab5_platform` | 显示、I2C 键盘、SD、ES8388、时钟 |
| `hojy_sdl` | 薄 SDL 兼容层 |
| `hojy_core` | 覆盖层：替换 upstream 里在 32 MB PSRAM 上行不通的实现 |
| `hojy_fmt` / `hojy_zita` | 上游依赖的精简构建 |

屏 / 键盘自检在 [`../bringup/`](../bringup/README.md)，`TAB5_FIRMWARE=bringup ./build.sh`。
