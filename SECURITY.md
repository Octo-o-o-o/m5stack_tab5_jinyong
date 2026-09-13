# 安全策略

## 支持的版本

只维护最新的 Release 和 `main` 分支。

## 范围

固件不使用任何无线功能（ESP32-P4 本身没有射频，板上的 ESP32-C6 不启用），攻击面主要在：

- 被恶意构造的 SD 卡数据：固件解析 GRP / IDX / 存档 / 字体 / WAV 时的越界、崩溃
- host 端脚本处理不可信输入：`prepare_game_data.sh`、字体子集、`makedata`、BGM 渲染
- 构建与发布流程：CI 工作流、依赖与发布产物

改存档、游戏内作弊不属于安全问题。

## 报告漏洞

请**不要**开公开 issue。在仓库的 [Security → Report a vulnerability](https://github.com/Octo-o-o-o/m5stack_tab5_jinyong/security/advisories/new) 私密提交，写清受影响的版本或提交、复现步骤和影响。维护者会在同一页面跟进，修复发布后再公开细节。
