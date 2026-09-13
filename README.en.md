# tab5_jinyong

[![build](https://github.com/Octo-o-o-o/m5stack_tab5_jinyong/actions/workflows/build.yml/badge.svg)](https://github.com/Octo-o-o-o/m5stack_tab5_jinyong/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/Octo-o-o-o/m5stack_tab5_jinyong)](https://github.com/Octo-o-o-o/m5stack_tab5_jinyong/releases/latest)
[![license](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)

[中文](README.md) | English

A native port of the 1996 DOS RPG *Heroes of Jin Yong* (金庸群侠传) to the [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5) (ESP32-P4, 32 MB PSRAM, 720×1280 MIPI-DSI panel), built on [soarqin/HeroesOfJinYong](https://github.com/soarqin/HeroesOfJinYong), a complete desktop reimplementation of the game.

This repository leaves the gameplay alone. It adds what running it on a microcontroller takes: a thin SDL compatibility layer, an ESP32-P4 platform layer, and a set of *overlays* that replace the upstream code paths that are harmless on a desktop but fatal in 32 MB of PSRAM.

> **No game data is included.** Heroes of Jin Yong is a commercial game. You need your own copy of the original DOS game; the host scripts convert it into the tree the device reads from its SD card.

The in-depth documentation (architecture, memory budget, porting playbook) is in Chinese. This page covers what you need to get the game running.

## Hardware

- M5Stack Tab5 (ESP32-P4, 16 MB flash, 32 MB PSRAM). All three panel generations (ILI9881C, ST7123, ST7121) are detected at runtime by the official BSP.
- **Tab5 Keyboard kit** (I2C, on Ext.Port1). There are no touch controls.
- A FAT32 microSD card.

## Status

Title screen, new game, world and town maps, events, dialogue, menus, items, save/load, sound effects and music work on the device. Battles pass the host test bench but have not yet been played through on the device. The ending animation is not supported. Names can only be typed in ASCII; set a Chinese name with `default_name` in `config.toml`. See [CHANGELOG.md](CHANGELOG.md).

## Quick start

### 1. Get the source

```bash
git clone https://github.com/Octo-o-o-o/m5stack_tab5_jinyong.git
cd m5stack_tab5_jinyong
./setup.sh
```

`setup.sh` fetches the submodules the port uses and checks for ESP-IDF v5.5.5. ESP-IDF is only needed to build the firmware yourself.

### 2. Prepare the SD card

This needs Python 3, a C/C++ compiler and CMake, plus your original game directory:

```bash
./prepare_game_data.sh /path/to/original-game
```

It builds upstream's `makedata`, converts the game into `local/sd_image/jinyong`, applies the Tab5 settings, subsets a CJK font to the characters the game uses, and pre-renders the music to WAV (the device streams WAV; it does not synthesise MIDI). On macOS the system Heiti font is used; elsewhere pass a TTF/OTF/TTC as the third argument (the second is the output directory). `--no-bgm` skips the music.

Copy the **whole** output directory to the root of the card as `/jinyong/`.

### 3. Flash

**Prebuilt firmware, no ESP-IDF needed.** Download `tab5_jinyong-vX.Y.Z.bin` from [Releases](https://github.com/Octo-o-o-o/m5stack_tab5_jinyong/releases/latest) (check it against `SHA256SUMS`) and write it to address `0x0`:

- In Chrome or Edge, open [esptool-js](https://espressif.github.io/esptool-js/), click **Connect** and pick the Tab5's port, set **Flash Address** to `0x0`, choose the file and click **Program**.
- Or from a terminal:

  ```bash
  pip install --upgrade esptool
  esptool --chip esp32p4 -p <PORT> write-flash 0x0 tab5_jinyong-vX.Y.Z.bin
  ```

**From source:**

```bash
./build.sh
./flash.sh      # asks first; add --yes when not interactive
./monitor.sh
```

Flashing replaces whatever firmware is on the device. The port is the ESP32-P4's USB-Serial-JTAG (`303A:1001`), and its USB serial number is the chip's MAC address. `flash.sh` picks it automatically only when it is the one Espressif device attached; with several it lists them and asks you to pass the port.

## Controls

| Key | Action |
|---|---|
| Arrow keys | Walk, move in menus (hold to repeat) |
| Enter, Space | Confirm, talk |
| Esc | Cancel; opens the main menu on the map |
| Backspace | Delete while typing a name |

## Contributing

Issues and pull requests are welcome, in English or Chinese. Read [CONTRIBUTING.md](CONTRIBUTING.md) first; the ground rules are: never edit `third_party/HeroesOfJinYong` (change an overlay instead), never commit game data, and say which layer you verified — firmware, card data, or the screen. This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md). Report security issues privately as described in [SECURITY.md](SECURITY.md).

## License

GPL-3.0-or-later, the same as upstream HeroesOfJinYong ([LICENSE](LICENSE)). Overlays derived from upstream files keep the original author's copyright notice. The repository follows the [REUSE](https://reuse.software/) specification. Third-party components in the firmware and their licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

*Heroes of Jin Yong* © Heluo Studio / Soft-World International. The game data is neither in this repository nor distributed by this project.
