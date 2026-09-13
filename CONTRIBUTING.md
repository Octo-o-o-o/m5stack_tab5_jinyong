# 参与贡献

欢迎 issue 和 PR。动手之前请读一遍本文件和 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)；做同类移植或想知道某个改动的来龙去脉，看 [docs/HANDHELD_PORT_PLAYBOOK.md](docs/HANDHELD_PORT_PLAYBOOK.md)。

参与即表示同意遵守[行为准则](CODE_OF_CONDUCT.md)。安全问题请按 [SECURITY.md](SECURITY.md) 私密报告，不要开公开 issue。

## 开发环境

- ESP-IDF v5.5.5；`./setup.sh` 拉 submodule 并检查 IDF
- 编译：`./build.sh`（游戏）、`TAB5_FIRMWARE=bringup ./build.sh`（自检）
- 本机测试台（macOS）：Homebrew 的 `sdl2-compat`，见 [tools/host_test/README.md](tools/host_test/README.md)
- 没有原版游戏数据也能改固件和脚本；跑测试台和上机需要你自备的数据

## 红线

1. **不修改 `third_party/HeroesOfJinYong`。** 要改上游行为，就在 `firmware/game/components/hojy_core/` 加或改覆盖层：同名 `.cc` 替换上游文件，新增或删除后 `idf.py reconfigure`，并在改动处写清「上游怎么做、为什么在这里不行、这里怎么改」。
2. **不提交游戏资源、存档、字体原件或固件二进制。** `.gitignore` 挡了大部分，提交前请再看一眼 `git status`。
3. 不把个人路径、串口名、密钥写进仓库。

## 提交前自查

| 改了什么 | 至少要做到 |
|---|---|
| 固件的任意部分 | 游戏和自检两个固件都能编过 |
| 内存、存档、输入、覆盖层 | `tools/host_test/run.sh` 通过（它会改写目标目录的 1–3 号存档槽，请对着副本跑）；新行为写成断言加进 `porttest.cc` |
| 平台层：显示、音频、键盘、SD | 上机，在 PR 里贴出对应的串口日志 |
| 脚本 | `shellcheck -x` 通过 |
| 新增文件 | 带 SPDX 文件头（见下），`reuse lint` 通过 |
| 用法或行为变了 | README / docs 同步；在 `CHANGELOG.md` 的 Unreleased 下记一笔 |

验收请分清**固件 / 卡数据 / 看屏**三层：只编译过的写「编译通过」，没上机的写「未上机」。

## 许可证与文件头

本仓库以 GPL-3.0-or-later 发布，贡献的代码按同一许可证授权。仓库遵循 [REUSE](https://reuse.software/) 规范：

- 新的 C / C++ 源文件开头：

  ```c
  /*
   * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
   * SPDX-License-Identifier: GPL-3.0-or-later
   */
  ```

- Shell、Python、CMake 用 `#` 注释写同样两行（放在 shebang 之后）
- 派生自上游的文件：保留上游原有的版权声明，另加一行 `SPDX-FileCopyrightText: 2021 Soar Qin <soarchin@gmail.com>`
- 文档、配置、锁文件由 `REUSE.toml` 统一声明，不用加文件头
- 检查：`pipx run reuse lint`

## Pull Request

- 一个 PR 做一件事，标题说清做了什么
- 描述里写动机、改动和验证到了哪一层；模板里的清单照实勾
- CI 会编译两个固件，并检查脚本和许可证标注
- 升级上游 submodule 时，先按 ARCHITECTURE.md「覆盖层」一节核对被覆盖文件在上游的改动

## 发布（维护者）

1. 把 `CHANGELOG.md` 里 Unreleased 的内容移到新版本号下，底部补上比较链接
2. `git tag vX.Y.Z && git push origin vX.Y.Z`
3. `release` 工作流编译两个固件，把镜像、ELF、第三方许可证和 `SHA256SUMS` 发布到 GitHub Releases
