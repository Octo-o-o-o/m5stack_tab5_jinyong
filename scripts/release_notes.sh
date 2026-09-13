#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Print the GitHub release notes for a tag: its CHANGELOG.md section plus how
# to flash the attached images. Fails when CHANGELOG.md has no such section.
#
#   scripts/release_notes.sh <vX.Y.Z>

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
tag="${1:?usage: scripts/release_notes.sh <vX.Y.Z>}"
version="${tag#v}"

section="$(awk -v v="${version}" '
    /^## \[/ { p = index($0, "[" v "]") > 0; next }
    /^\[[^]]+\]: / { p = 0 }
    p
' "${root}/CHANGELOG.md")"

if [[ -z "${section//[[:space:]]/}" ]]; then
    echo "CHANGELOG.md has no entry for [${version}]" >&2
    exit 1
fi

printf '%s\n' "${section}"
cat <<EOF

## 刷写 / Flashing

\`tab5_jinyong-${tag}.bin\` 是游戏固件，\`tab5_jinyong_bringup-${tag}.bin\` 是屏 / 键盘 / SD 自检固件。两者都是完整镜像，**烧到地址 \`0x0\`**：

- 浏览器（Chrome / Edge）：打开 [esptool-js](https://espressif.github.io/esptool-js/)，点 **Connect** 选 Tab5 的串口，**Flash Address** 填 \`0x0\`，选择文件后点 **Program**
- 命令行：\`pip install --upgrade esptool\`，然后 \`esptool --chip esp32p4 -p <PORT> write-flash 0x0 tab5_jinyong-${tag}.bin\`

游戏数据不随固件发布，SD 卡需要用源码里的 \`prepare_game_data.sh\` 从你自己的原版游戏生成，见 [README](https://github.com/Octo-o-o-o/m5stack_tab5_jinyong#快速开始)。\`SHA256SUMS\` 用于校验下载。
EOF
