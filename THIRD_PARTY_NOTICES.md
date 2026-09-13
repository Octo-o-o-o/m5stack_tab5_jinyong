# 第三方组件与许可证 / Third-party notices

本仓库自己的代码以 GPL-3.0-or-later 发布，见 [LICENSE](LICENSE)。下面列出**编进发布固件**的第三方代码。每个 Release 附带的 `tab5_jinyong-vX.Y.Z-licenses.zip` 收齐了下列许可证全文（由 `scripts/collect_licenses.sh` 生成）。

The project's own code is GPL-3.0-or-later. The tables below list the third-party code linked into the released firmware; every release ships the full license texts as `tab5_jinyong-vX.Y.Z-licenses.zip`.

## 随上游 submodule 编进固件

| 组件 | 用途 | 许可证 | 版权 |
|---|---|---|---|
| [HeroesOfJinYong](https://github.com/soarqin/HeroesOfJinYong) | 玩法内核（`hojy_core`，包括本仓覆盖层） | GPL-3.0-or-later | © 2021 Soar Qin |
| [{fmt}](https://github.com/fmtlib/fmt) 12.2.0 | 格式化（`hojy_fmt`） | MIT | © 2012–present Victor Zverovich and {fmt} contributors |
| [zita-resampler](https://kokkinizita.linuxaudio.org/linuxaudio/) | 音频重采样（`hojy_zita`） | GPL-3.0-or-later | © 2006–2023 Fons Adriaensen |
| [toml++](https://github.com/marzer/tomlplusplus) | 解析 `config.toml`（上游 `src/external/toml.hpp`） | MIT | © Mark Gillard |
| [stb_truetype、stb_rect_pack](https://github.com/nothings/stb) | 字体栅格化与装箱（上游 `src/external/`） | MIT 或 Unlicense，任选 | © 2017 Sean Barrett |

## ESP-IDF 与托管组件

| 组件 | 许可证 |
|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) v5.5.5 | Apache-2.0；其中 FreeRTOS、newlib、FatFs 等第三方部分见 [ESP-IDF 版权说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/COPYRIGHT.html) |
| `espressif/m5stack_tab5_noglib`（Tab5 BSP） | Apache-2.0 |
| `espressif/esp_codec_dev` | Apache-2.0 |
| `espressif/esp_lcd_ili9881c`、`esp_lcd_st7123`、`esp_lcd_touch`、`esp_lcd_touch_gt911`、`esp_lcd_touch_st7123` | Apache-2.0 |
| `espressif/esp_io_expander`、`esp_io_expander_pi4ioe5v6408`、`bmi270` | Apache-2.0 |
| `espressif/esp_ipa` | Espressif MIT（只允许用于 Espressif 的芯片） |

托管组件的确切版本以 `firmware/game/dependencies.lock` 为准。BSP 还会下载 `esp_video`、`usb` 等组件，但它们没有被链接进固件；许可证包里仍然一并收录。

## 只在 host 上使用，不进固件

| 组件 | 用途 | 许可证 |
|---|---|---|
| [libADLMIDI](https://github.com/Wohlstand/libADLMIDI) | 把 XMI 预渲染成 WAV（`scripts/render_bgm.sh`） | LGPL-2.1-or-later / GPL-3.0-or-later |
| [fontTools](https://github.com/fonttools/fonttools) | 中文字体子集（`scripts/subset_cjk_font.py`） | MIT |
| [SDL2](https://www.libsdl.org/) | 本机测试台 | Zlib |

子集化用的字体来自你自己系统里的字体文件，不在仓库和 Release 中分发。

---

## toml++ — MIT License

```
Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## stb_truetype / stb_rect_pack — MIT License (Alternative A)

stb 允许在 MIT 与 Unlicense 之间任选；本项目按 MIT 使用。

```
Copyright (c) 2017 Sean Barrett

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
