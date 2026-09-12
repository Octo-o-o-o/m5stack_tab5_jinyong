# platform/tab5

实现在 `firmware/game/components/tab5_platform/`。公开 C API 是 `tab5_platform.h`。

HOJY 的 scene/audio 仍然 `#include <SDL.h>`；设备上那是 `firmware/game/components/hojy_sdl`，不是桌面 SDL2。键盘 I2C HID 在 `SDL_PollEvent` 里变成 scancode。
