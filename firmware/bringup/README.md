# M1 bring-up firmware

独立 ESP-IDF 工程，验证 Tab5 屏 / I2C 键盘 / SD / 喇叭打桩。  
从仓库根运行 `./build.sh`。默认不刷机。

- 目标：`esp32p4`
- BSP：`espressif/m5stack_tab5_noglib==1.3.0`（官方 registry，无 LVGL）
- 键盘：I2C `0x6D` on G0/G1，不是 USB Host HID
- 刷写口：`/dev/cu.usbmodem1101`
