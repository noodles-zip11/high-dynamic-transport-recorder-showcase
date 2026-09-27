# V1 Evidence Package Design

## Goal

在 `evidence/releases/v1.0.0/` 建立一个完整但精选、可公开、可复核的 V1 GitHub 发布证据包。包内内容只复制小型摘要、索引、元数据、哈希和通过现有 Host 解码及 PySide6 `ReplayWidget` 导出的代表波形 PNG；不复制历史 `evidence/` 全量内容，也不复制任何私有原始事件文件。

## Scope and non-goals

- 范围是发布证据整理、可重复生成工具、Host 侧 PNG/元数据验证和文档化 provenance。
- 不修改固件、协议、模型算法、Flash 地址/布局、引脚、时钟、DMA/cache、任务优先级或事件数据。
- 不读取或修改私有训练 raw；允许只读读取用户指定的 QSPI 备份，用于选择代表事件、校验原始文件哈希并生成 PNG。
- 不把受控 AI pilot 写成泛化结论；不把低功耗 WFI/software gate 写成真实节电量或续航验证。
- 不移动、删除或重写既有 `evidence/` 文件；不配置远端、不 push、不移动既有 `v1.0.0` 标签。

## Public/private boundary

公开包允许包含：

1. 可审计的 Markdown/JSON/CSV 摘要、索引和来源相对路径；
2. 包内文件的大小与 SHA-256；
3. 从指定 QSPI 备份只读解码得到的 PNG 和最小事件元数据；
4. 已提交的软件/硬件/AI 证据的精选快照。

公开包禁止包含：

1. QSPI 备份中的原始事件文件、训练 raw、绝对本机路径和私有 manifest；
2. 未经证据核实的人工类别标签、模型结果或硬件结论；
3. 构建缓存、虚拟环境、密钥、token、用户数据或超过 GitHub 单文件限制的生成物。

## Evidence levels

每一条发布摘要和每一个波形 metadata 都显式标注等级：

- `software`: native/Host/Python/firmware 软件门禁，可复现但不等于实板通过；
- `hardware`: 实板日志、读回校验或 HIL 证据，必须带来源、事件号和硬件边界；
- `ai_pilot`: 真实采集的受控 AI 试验，必须同时声明样本规模、数据划分和不代表独立 session 泛化；
- `derived`: 从已标识来源解码/绘图生成的 PNG 或摘要，不能把派生图当作原始证据。

## Package layout

```text
evidence/releases/v1.0.0/
├── README.md
├── manifest.json
├── SHA256SUMS.txt
├── catalog/
│   ├── evidence-inventory.csv
│   └── key-logs.csv
├── software/
│   └── software-gate.md
├── hardware/
│   └── hardware-acceptance.md
├── ai/
│   ├── real-ai-pilot.md
│   ├── training-report.json
│   └── model-manifest.json
├── desktop/
│   ├── README.md
│   ├── index.json
│   ├── event-<id>.png
│   └── event-<id>.json
└── provenance/
    ├── source-and-builds.md
    └── release-equivalence.md
```

`manifest.json` 记录包版本、源提交、证据等级、来源相对路径、生成工具版本和除自身/校验清单外每个包文件的大小与 SHA-256。`SHA256SUMS.txt` 覆盖 `manifest.json` 以及其余包文件，清单文件自身不在自身哈希列表中；README 明确这一验证边界。这样既避免自引用，又能让发布者按文件复核。

## Generation architecture

`host/tools/v1_evidence_package.py` 提供无硬件的可测试逻辑：

- 读取 `git ls-files evidence` 生成完整 tracked evidence inventory；
- 从已核实的 canonical evidence 生成软件、硬件、AI 和日志精选快照；
- 通过现有 `host.transport_recorder.analysis.event_record.load_event` 解码事件；
- 在 `QT_QPA_PLATFORM=offscreen` 下创建 `ReplayWidget`，调用 `set_event` 与 `export_png`，不复制 raw；
- 生成事件元数据、manifest、SHA256SUMS，并验证覆盖关系和 Markdown 相对链接。

`scripts/generate_v1_evidence_package.py` 是显式 CLI 入口。它接受仓库根目录、只读事件源目录、输出目录和经证据核实的事件选择；遇到无法解码、缺少 CRC/sample/loss/subtrigger、缺少人工标签或缺少模型结果时失败或将事件列入缺口，绝不推断标签。默认只输出经核实的代表事件，连续振动没有独立真值时可以不输出该类图，并在 README/AI 摘要中列明缺口。

## Event metadata contract

每个 `desktop/event-<id>.json` 至少记录：

- `event_id`、源文件相对标识和源文件 SHA-256；
- `format_version`、`crc_status`、`sample_count`、`lost_samples`、`subtrigger_count`；
- `human_label` 及其 evidence source；没有核实值时为 `null` 并给出缺口说明；
- `ai_prediction.class_name`、`class_index`、`confidence` 及其来源；没有板载结果时为 `null`，不可用人工标签代替；
- `label_semantics`，明确人工类别标签与模型预测是两个独立字段；
- `derived_from` 和 `evidence_level=derived`。

## Known conclusions and limits

AI 摘要必须准确保留当前已审查数字：10/13、`macro_f1=0.755952` 和四类 recall，并显著声明 `within_session_pilot=true`；这些数字不是独立 session 泛化证明。软件摘要只声明现有 software gate 结果。低功耗摘要只声明 Cortex-M7 Sleep/WFI、blocker 观测和软件证据；不声称实测低功耗电流、续航或 Stop/Standby 已验收。G2、物理断连和 72 小时长稳继续按项目状态标为 deferred/未纳入 V1 核心验收。

## Acceptance boundary

完成条件是：

1. 设计和计划先以独立 docs commit 提交；
2. 包内目录、manifest、SHA256SUMS、软件/硬件/AI/desktop/provenance 内容均可公开且不含 raw；
3. PNG 来自 `ReplayWidget.export_png`，PNG signature、非空、元数据追溯和标签语义检查通过；
4. manifest 与 SHA256SUMS 覆盖关系、Markdown 相对链接和 tracked evidence inventory 校验通过；
5. `python -m pytest host/tests ai/tests -q`、`scripts/test_native.ps1`、`git diff --check` 通过；
6. `git diff main...HEAD` 仅包含 docs/evidence/host 或 scripts 测试/生成工具，所有文件小于 100 MiB，未发现 secret、token 或私有 raw；
7. 最终状态干净，提交列表、包文件数/大小、图对应事件、扫描结果和剩余硬件风险已回传主审。
