# V1 候选实板验收 001

## 结论

- 候选固件必做实板功能验收（hardware checklist 1/3/4/5/6）：**PASS**。
- 书包正常活动约 30 分钟（hardware checklist 2）：按清单允许规则记为有理由 `SKIP`，不影响 mandatory 1/3/4/5/6，也不记为 PASS 或 FAIL。
- 不需要补做真实路测。敲击/掉落用于端到端功能验收；人工动作不保证精确对应仅用于“不作为分类准确率”，不削弱端到端验收。
- 本证据不声称 AI 泛化准确率或低功耗续航/电流量化。

## 实板前 provenance、身份、备份与刷写

- 日期：2026-08-25。
- `main` 基线：`58477d38b700e3edad3a7d144a6916f5f574ffce`。
- 候选工作树：`LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\v1-release-candidate-20260824`。
- 候选分支：`feature/v1-release-candidate-20260824`。
- 候选工作树 pre-flash `git status`：clean（无未提交变更）。
- 被测候选代码提交 / firmware revision：`46a131af376033c26b0ebf59f724c1fbcfeb0004`。
- ST-Link：`DEVICE_SERIAL_REDACTED__`，V2J46S7。
- 目标：STM32H7xx Rev V，2 MiB internal Flash，连接时 3.24 V。
- 刷写前应用 revision：`6ae13131ed02201b006b28e6e4ac0b5cb78e532e`。
- 刷写前双份 internal Flash 备份：
  - `C:\v1-hw-backup-20260825-001\internal-flash-a.bin`
  - `C:\v1-hw-backup-20260825-001\internal-flash-b.bin`
  - 两份均为 2,097,152 B，SHA-256 均为 `D9D3F1B98888E5D65309BE6218E3D7BF185969F4B9B1BBCA0D55C44FB6B5948E`。
- 刷写镜像：Release BIN 135,760 B，SHA-256 `C610DDE842B8F6358AC2DA686221DAA042A7C78D434ABCAD174595ED30E997CF`。
- 仅写入 application `0x08020000`；CubeProgrammer 报告 `Download verified successfully`，未格式化 QSPI。
- 刷写后 `sysinfo`：revision `46a131af376033c26b0ebf59f724c1fbcfeb0004`，serial `recorder-001`。

## 自动实板门禁

- TERP COM13、UART1/FinSH COM12：PASS。
- storage ready、model valid、无 pending install：PASS。
- 10 分钟静置，21 次每 30 s 采样均保持 event 179，无误触发。
- 静置结束时 storage/event export error 均为 0。
- WFI `attempts/entries/wakes` 持续递增；连续两次 `power status` blocker holds 全 0；`stop_compiled=0`、`stop_allowed=0`。
- 既有 event 179 可下载并通过 EV03/CRC/2400-sample 严格解析；它的旧二类 AI sidecar 保持向后可读。
- hardware checklist 映射：1 PASS；2（书包 30 分钟）`SKIP`；3 PASS；4 PASS；5 PASS；6 PASS。

## 用户动作与新增事件

用户报告按约 35 s 间隔完成正常拿放、10 次外壳敲击和 5 次软垫低高度掉落，但不保证每个动作细节完全一致。该不确定性只限制人工动作与类别的对应关系，不把动作分类比例作为分类准确率；它不削弱事件保存、下载、CRC 和 AI 结果的端到端验证。

- 首次接回后立即列出 event 180–195，共 16 条，恰好对应 1 次正常拿放、10 次敲击和 5 次掉落的计划动作数量。
- 按计划顺序，最后 5 次掉落对应 event 191–195；五条全部由四类模型判为 `drop`，confidence 依次为 0.8080、0.7498、0.9967、0.9966、0.9966。
- event 180–195 分类合计：impact 6、background 2、drop 8。因为用户明确表示动作细节不完全确定，不把敲击分类比例作为准确率指标。
- 接线、移动和下载期间又产生 event 196–202；这些不属于计划动作，但一并下载校验。
- event 180–202 共 23 条，ID 连续，无缺口。

## 数据完整性与 AI

event 180–202 全部满足：

- EV03；
- 2400 samples，pretrigger + posttrigger = 2400；
- 设备整条记录 CRC 和 EV03 payload CRC 均通过；
- subtrigger = 0；
- lost samples = 0；
- IMU transport / DMA / pool backpressure / storage / export error 均为 0；
- AI status = 1、class_count = 4、AI sample_count = 2400、failure_reason = 0。

23 条四类输出合计：impact 9、background 3、drop 11；本轮没有 continuous_vibration 动作，因此未要求出现该类别。

权威汇总：`C:\v1-hw-backup-20260825-001\v1-hardware-acceptance-final.json`，SHA-256 `9532CD6EBFB51E8F99308511574B49C0A51D18A01C950220E8E0024D506B508D`。原始事件、逐条 readback 和机器汇总均保存在同一外部目录；未纳入 Git。

完整 QSPI 原始证据（format 前）记录在外部目录 `C:\v1-hw-backup-20260825-001\full-qspi-event-backup\event-001..204.terp-event`：共 204 个文件，event ID 连续，全部 CRC 通过，decode errors 为 0。该目录的 `summary.json` SHA-256 为 `E24A44B1C9C6B8DF4E119CC23E35CA7C80034263683E076BF106F2A0A0392228`。raw 按项目政策不入 Git。

## 容量与停机状态

- 原 event 1–204 已在清理前完整归档到上述外部目录；归档完成后，用户单独授权执行 QSPI format 并清除旧日志。
- format/重启瞬态产生新的 event 1；清理后的状态为 `storage_ready=true`、storage/export errors 为 0，可用容量约 203 条事件。
- 新 event 1 和约 203 条可用容量是已知 maintenance 限制；不能把该状态描述为空盘，也不能暗示原 event 1–204 仍留在已格式化 QSPI 中。

## 发布判定

V1 候选的启动、自然触发、固定事件长度、QSPI 持久化、TERP 下载、CRC、真实四类推理和 WFI 状态链均已取得实板证据；mandatory 1/3/4/5/6 功能验收 PASS，书包 30 分钟按 checklist `SKIP`。本结论不声称 AI 泛化或低功耗量化；format/重启后的 event 1 与约 203 条容量为已知 maintenance 限制。
