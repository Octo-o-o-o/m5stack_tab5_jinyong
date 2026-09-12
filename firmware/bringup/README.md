# firmware/bringup

独立的屏 / I2C 键盘 / SD 自检固件，**不是游戏**。游戏在 [`../game/`](../game/README.md)。

从仓库根：

```bash
TAB5_FIRMWARE=bringup ./build.sh
TAB5_FIRMWARE=bringup TAB5_ACCEPT_OVERWRITE_WORK_FIRMWARE=1 ESPPORT=/dev/cu.usbmodem1101 ./flash.sh
```

- 目标：`esp32p4`
- BSP：`espressif/m5stack_tab5_noglib==1.3.0`（官方 registry，无 LVGL）
- 键盘：I2C `0x6D`，G0/G1，不是 USB Host HID
- 刷写口：Espressif USB-JTAG（`303A:1001`）
