## 做了什么

## 为什么

## 怎么验证的

- [ ] 两个固件都能编过（`./build.sh`、`TAB5_FIRMWARE=bringup ./build.sh`）
- [ ] 改到内存 / 存档 / 输入 / 覆盖层：`tools/host_test/run.sh` 通过
- [ ] 上机：写明看了什么（串口日志 / 屏幕 / 键盘 / 声音），或注明「未上机」
- [ ] 新增文件带 SPDX 文件头，`reuse lint` 通过

## 自查

- [ ] 没有修改 `third_party/HeroesOfJinYong`
- [ ] 没有提交游戏数据、存档、字体或固件二进制
- [ ] 用法或行为变了的地方，README / docs 已同步，`CHANGELOG.md` 已记录
