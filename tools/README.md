# tools/

Host 预处理入口是仓库根上的 `./prepare_game_data.sh`。它会在 `host/build/` 里编译 HeroesOfJinYong 的 `makedata` / `mergepic`，输出到 gitignored 的 `local/`。

不要让 ESP32 做 mergepic / 扫一千个 SDX 分片。
