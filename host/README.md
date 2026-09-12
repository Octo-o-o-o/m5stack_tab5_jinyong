# host/

`prepare_game_data.sh` 会在这里编上游的 `makedata` / `mergepic`（产物在 gitignored 的 `local/` 和 `host/build/`）。

桌面 SDL simulator 还没做。要在没有 Tab5 时验本移植的游戏逻辑，用 [tools/host_test](../tools/host_test/README.md)。

不要把游戏数据放进这个目录的 Git 跟踪文件。
