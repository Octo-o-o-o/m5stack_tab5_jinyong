# firmware/bringup

独立的屏 / I2C 键盘 / SD / 喇叭自检固件，**不是游戏**。游戏在 [`../game/`](../game/README.md)。

从仓库根：

```bash
TAB5_FIRMWARE=bringup ./build.sh
TAB5_FIRMWARE=bringup ./flash.sh
TAB5_FIRMWARE=bringup ./monitor.sh
```

- 目标：`esp32p4`
- BSP：`espressif/m5stack_tab5_noglib==1.3.0`（官方 registry，无 LVGL）
- 屏：先在内部 I2C 上探测面板型号并打日志，再由官方 BSP 初始化并画测试图
- 键盘：I2C `0x6D`，G0/G1，HID 模式，按键打到串口；不是 USB Host HID
- SD：挂载后读 `/jinyong/config.toml` 第一行
- 刷写 / 串口：ESP32-P4 的 USB-Serial-JTAG（`303A:1001`）
