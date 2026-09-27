# 招聘者与技术面试官阅读路线

这是一条按时间分层的阅读路线，帮助招聘者和技术面试官在不同深度核对项目的真实边界。
它不是新的验收证据，也不替代 V1 公开证据包、发布说明或 Reliability Evidence 收口报告。

## 3 分钟：确认项目是否真实、完整

按下面顺序快速浏览；每一步都应能在仓库中看到对应的物料和边界。

如果你已从 README 进入，可跳过下面重复的 README 定位，直接继续看系统架构和 V1 evidence package；本页单独打开时仍按完整路线阅读。

1. **先看 README status。** 打开 [README 当前状态](../../README.md#当前状态)，必要时回看
   [README hero](../../README.md)，确认硬件、软件栈、V1 发布基线、当前 `main` 的 default-off
   状态和 Reliability-enabled Release 的 `LOCKED` 标记都被同时写出。招聘者应看到一个有
   版本边界和证据边界的项目，而不是只有功能宣传语的首页。
2. **沿主链看数据如何走通。** 先看 README 的[项目主链](../../README.md#项目主链)，再用
   [系统架构页](./architecture.md)核对采集、事件、EL01、TERP、桌面、TinyML 与 OTA 的分层。
   应能看出数据从 ICM45686 FIFO 走到事件和主机，而不是把所有能力并列成孤立模块。
3. **检查已有事件画廊。** 在 README 的[真实运行证据](../../README.md#真实运行证据)中查看
   event-180、event-195、event-202；请把它们称为“按 event ID 标识的桌面派生回放图”，
   而不是语义真值或示波器原始测量。招聘者应看到可追溯的回放产物，同时看到它的中性证据边界。
4. **落到 V1 证据包。** 打开 [V1 evidence package](../../evidence/releases/v1.0.0/README.md)，
   先对照能力结论和明确限制，确认实板主链的公开来源，也看到 AI pilot、低功耗和 deferred
   reliability matrix 没有被包装成更强的结论；provenance、manifest 和逐项元数据留给深度核查。

## 10–20 分钟：判断嵌入式工程深度

下面五个入口是技术面试前的定向总览：每个主题保留一个 canonical entry，作为第一阅读入口；
同一主题的额外链接只是深读参考，用来追实现或契约。重点不是记住名词，而是检查每个决定
解决了什么实时性、恢复性或兼容性问题，以及它付出了什么代价。

1. **DMA / Cache / fixed ownership。** **Canonical entry：**架构页[第 3 节](./architecture.md#3-dmacache-与缓冲区所有权)。
   深读 [DMA/Cache policy](../decisions/dma-cache-policy.md) 和[采集实现](../../firmware/app/acquisition/)。
   看 D2 SRAM1 的 cache-safe 约束、32-byte 边界、DTCM 排除，以及 ISR 只完成确认和通知的边界；
   要能解释一致性和 ISR 延迟为何优先于“偶尔读对”的经验判断。
2. **EV03 / EL01 / recovery。** **Canonical entry：**架构页[第 4 节](./architecture.md#4-预触发事件组装与-ev03)。
   深读架构页[第 5 节](./architecture.md#5-w25q64el01-与恢复策略)、[EV03 record format](../protocol/event_record_v3.md)、
   [EL01 event log](../storage/event_log_v1.md)、
   [事件与存储实现](../../firmware/app/event/)和[storage](../../firmware/app/storage/)。看固定窗口、loss accounting、commit marker、CRC、
   superblock A/B 和启动扫描怎样共同定义“已提交事件”。
3. **TERP chunk / CRC / V1 compatibility。** **Canonical entry：**架构页[第 6 节](./architecture.md#6-uart3terp-下载链)。
   深读 [TERP v1](../protocol/terp_v1.md)、[协议登记与生成物](../../protocol/)以及
   [Host transport/replay](../../host/transport_recorder/)。看 HELLO、4096-byte payload、4000-byte
   chunk、块级 CRC、断线续传和原子改名如何保持旧事件读取语义不变。
4. **Bootloader / OTA / release gate。** **Canonical entry：**架构页[第 8 节](./architecture.md#8-bootloaderota-与回退)。
   深读 [memory layout](../../config/memory_layout.yaml)、[bootloader](../../bootloader/)、
   [OTA implementation](../../firmware/app/ota/)和[release gate](../../scripts/release_gate.ps1)。
   看固定地址契约、trial confirm、健康快照、双副本状态和回退路径如何限制升级风险；不要把
   软件回退链自动等同为已经完成的物理 OTA 可靠性认证。
5. **Reliability default-off / lock。** **Canonical entry：**架构页[第 9 节](./architecture.md#9-reliability-evidence-旁路升级)。
   深读 [reliability source](../../firmware/app/reliability/)、[current-main closeout](../superpowers/reports/2026-08-29-reliability-evidence-closeout.md)
   和同一 [release gate](../../scripts/release_gate.ps1)。看默认构建如何隔离旁路和 FaultInjection，
   以及为何只有同一 sealed source revision 上 H0–H5 全部 physical-board PASS 才能解锁发布。

## 深度核查：从声明走到源码和证据

下表把公开说法、实现入口、契约、证据状态和诚实边界放在同一行。主要链接来自已审查的架构页和仓库内 canonical 文档/源码入口。
需要核对 provenance、manifest 或逐项元数据时，从证据状态栏的 V1 package 和 closeout 入口继续。

| public claim | implementation/source | contract/design | evidence/status | honest boundary |
|---|---|---|---|---|
| 高频 FIFO/DMA 采集，ISR 保持最小 | [acquisition](../../firmware/app/acquisition/) | [DMA/Cache policy](../decisions/dma-cache-policy.md)；[架构第 3 节](./architecture.md#3-dmacache-与缓冲区所有权) | [V1 evidence package](../../evidence/releases/v1.0.0/README.md) | 是固定缓冲和所有权设计；不是对所有物理负载吞吐的承诺。 |
| 固定事件所有权与显式背压 | [event](../../firmware/app/event/)、[pipeline](../../firmware/app/pipeline/) | [pre-trigger memory budget](../decisions/pretrigger-memory-budget.md)；[架构第 4 节](./architecture.md#4-预触发事件组装与-ev03) | [V1 release notes](../v1.0.0-release-notes.md) | 资源不足可拒绝/降级并计数；不声称长 episode 可变长保存。 |
| EL01 追加、CRC 与启动恢复 | [storage](../../firmware/app/storage/) | [EL01](../storage/event_log_v1.md)；[EV03](../protocol/event_record_v3.md) | [V1 evidence package](../../evidence/releases/v1.0.0/README.md) | 固定提交边界且不循环覆盖；破坏性 U2 掉电门禁 H3 尚未完成。 |
| TERP 分块下载与桌面回放 | [transport](../../firmware/app/transport/)、[Host](../../host/transport_recorder/)、[protocol](../../protocol/) | [TERP v1](../protocol/terp_v1.md)；[架构第 6 节](./architecture.md#6-uart3terp-下载链) | [V1 evidence package](../../evidence/releases/v1.0.0/README.md) | CRC/SHA-256 发现意外损坏；不是签名、身份认证或来源证明。 |
| 四分类 TinyML 与模型 A/B 生命周期 | [AI](../../ai/)、[OTA](../../firmware/app/ota/) | [model contract](../ai/model_contract_v1.md)；[架构第 7 节](./architecture.md#7-tinyml-推理与模型生命周期) | [release notes](../v1.0.0-release-notes.md)；[AI evidence](../../evidence/releases/v1.0.0/README.md) | `10/13`、macro-F1 `0.755952` 是 within-session pilot，不证明独立 session 泛化。 |
| Bootloader / OTA / Release gate 有清晰边界 | [bootloader](../../bootloader/)、[OTA](../../firmware/app/ota/)、[release gate](../../scripts/release_gate.ps1) | [memory layout](../../config/memory_layout.yaml)；[架构第 8 节](./architecture.md#8-bootloaderota-与回退) | [V1 release notes](../v1.0.0-release-notes.md)；[closeout](../superpowers/reports/2026-08-29-reliability-evidence-closeout.md) | V1 软件链可回退；Reliability-enabled Release 仍因 H0–H5 未全 PASS 而锁定。 |
| 软件实现已完成，但发布证据/实板门禁仍未封板；Reliability-enabled Release 继续锁定 | [reliability](../../firmware/app/reliability/)、[release gate](../../scripts/release_gate.ps1) | [架构第 9 节](./architecture.md#9-reliability-evidence-旁路升级) | [current-main closeout](../superpowers/reports/2026-08-29-reliability-evidence-closeout.md) | default-off snapshot 不是 V2、工业认证或完整 physical reliability release。 |

## 嵌入式固件面试入口

这些是可以沿源码和契约追问的入口；不应把它们改写成预先脚本好的个人答案。

### FIFO + DMA / Cache

- **看什么：** [DMA/Cache policy](../decisions/dma-cache-policy.md)、[acquisition](../../firmware/app/acquisition/)和[架构第 3 节](./architecture.md#3-dmacache-与缓冲区所有权)。
- **核查后应能说明：** DMA 缓冲的区域、对齐和 cache 维护是明确契约，ISR 只通知消费者；不能仅凭“读到了正确值”证明一致性。

### Fixed pool / pressure

- **看什么：** [pre-trigger memory budget](../decisions/pretrigger-memory-budget.md)、[event](../../firmware/app/event/)和[架构第 4 节](./architecture.md#4-预触发事件组装与-ev03)。
- **核查后应能说明：** 固定池、预留块和所有权计数把背压显式化；资源不足时拒绝/降级并计数。

### No silent truncate / overwrite

- **看什么：** [事件实现](../../firmware/app/event/)、[存储实现](../../firmware/app/storage/)和[架构第 5 节](./architecture.md#5-w25q64el01-与恢复策略)。
- **核查后应能说明：** 受保护事件不被静默覆盖，空间不足进入 `FULL`；V1 没有循环覆盖或自动垃圾回收。

### Power-loss ordering / CRC

- **看什么：** [EL01 contract](../storage/event_log_v1.md)、[EV03 contract](../protocol/event_record_v3.md)和[架构第 5 节](./architecture.md#5-w25q64el01-与恢复策略)。
- **核查后应能说明：** 头、EV03、CRC、尾字段和最后的 `commit_marker` 形成可审计提交顺序；软件恢复逻辑不等于已完成破坏性掉电实测。

### Old TERP compatibility

- **看什么：** [TERP v1](../protocol/terp_v1.md)、[message registry](../../protocol/)和[Host client](../../host/transport_recorder/)。
- **核查后应能说明：** 新的可靠性消息 ID 是 additive，旧事件读取 ID 保持不变；分块 CRC 支持续传，但不是安全认证。

### OTA rollback

- **看什么：** [memory layout](../../config/memory_layout.yaml)、[OTA](../../firmware/app/ota/)、[bootloader](../../bootloader/)和[架构第 8 节](./architecture.md#8-bootloaderota-与回退)。
- **核查后应能说明：** trial 只有满足健康条件并确认后才推进状态；未确认 trial 由 Bootloader/双副本路径回退到已知有效槽。

### Reliability not formally qualified

- **看什么：** [Reliability Evidence closeout](../superpowers/reports/2026-08-29-reliability-evidence-closeout.md)、[reliability](../../firmware/app/reliability/)和[架构第 9 节](./architecture.md#9-reliability-evidence-旁路升级)。
- **核查后应能说明：** 软件 safeguards 和 default-off gate 已有记录，但 H0–H5 尚未在同一封板 revision 上全部 physical-board PASS；因此不称为正式可靠性认证。

## 术语与状态速查

- **EV03：** 固定采样事件记录格式；V1 形状为 25 个 pre-trigger 块、50 个 post-trigger 块，共 2400 samples。
- **EL01：** 包住 EV03、支持 CRC 和掉电恢复的 SPI NOR 追加式事件日志封装。
- **TERP：** UART3 上的版本化二进制传输协议；事件按块读取，协议版本与旧读取语义分开。
- **WFI：** Cortex-M7 的 Wait For Interrupt；V1 是普通 Sleep/WFI，不是 tickless、Standby 或续航验证。
- **H0–H5：** Reliability Evidence 的六项 physical-board gates；它们不能由 native 或 Host 测试替代。
- **default-off：** current main 的 reliability evidence 旁路默认不进普通 Release，不能据此宣称已解锁可靠性版本。
- **V1 baseline：** 已发布、可回退的 `v1.0.0`，覆盖公开实板主链和固定 firmware/protocol/model 边界。
- **current-main snapshot：** 当前开发基线的 default-off 软件 gate 快照；不是 `v1.0.0` artifact，也不是 Reliability-enabled Release。

## 阅读完成后的正确结论

- V1 实板主链按公开证据已闭环；它是发布与讨论的 board-closed baseline。
- 当前 `main` 增加了 default-off reliability source 和更强的软件 safeguards，但这不改变 V1 baseline 的身份。
- Reliability-enabled Release 仍为 `LOCKED`；不宣称 V2、工业级认证、72 小时长稳或 100 次物理掉电通过。
- AI 的 `10/13`、macro-F1 `0.755952` 只支持 within-session pilot 结论；不证明 independent-session generalization。

招聘者应据此判断“是否有真实工程证据和边界意识”；面试官则应回到源码、契约和证据状态
追问取舍，而不是要求候选人背诵一套看似完整的个人答案。
