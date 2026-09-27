# 项目问题台账：软件缺陷、硬件依赖与验证门禁

- 用途：作为本项目唯一长期问题台账；新发现、分类、状态变化、修复提交和验证证据都追加到本文档。
- 最后更新：2026-08-25。
- 当前 V1 软件基线：`v1.0.0^{}=a69b6c6c71b91e1267780c169760eca284b7523b`；后续 `main`
  只增加已审查的公开证据包和文档，不改变该 V1 固件、协议或模型身份。
- 状态词：`OPEN`、`IN_PROGRESS`、`SOFTWARE_DONE`、`PENDING_HARDWARE`、`DEFERRED_POST_CORE`、`RECLASSIFIED`、`MONITORED`、`BLOCKED`、`CLOSED`。
- 维护规则：每个活动项必须有类别、状态、下一步和关闭证据；构建/模拟通过不得写成硬件通过。

> **状态优先级：** 当前 V1 结论以
> [`evidence/releases/v1.0.0/`](../../evidence/releases/v1.0.0/README.md) 为准。本文后续带日期的
> 审查、固定点、建议顺序和“当前”措辞是纵向历史，不得脱离所属日期覆盖本页顶部的发布状态。

## 2026-08-25 V1 候选与正式集成证据

- **软件门禁：** [`software/software-gate.md`](../../evidence/releases/v1.0.0/software/software-gate.md)
  记录 Debug/Release、47 个 native、Host 131、AI 78 以及固件/vector/memory-map/ICM alignment
  通过；它与 V1 tag 和产物哈希绑定。
- **必做实板功能：** [`hardware/hardware-acceptance.md`](../../evidence/releases/v1.0.0/hardware/hardware-acceptance.md)
  记录启动、自然触发、固定 2400-sample EV03、QSPI/TERP/CRC、四类推理和 WFI 状态链 PASS；
  10 分钟静置无误触发，event 180–202 共 23 条连续有效记录。
- **真实 AI：** [`ai/real-ai-pilot.md`](../../evidence/releases/v1.0.0/ai/real-ai-pilot.md)
  记录 91 条合格真实事件、四分类 test 10/13、macro-F1 0.755952 和板端部署闭环；
  `within_session_pilot=true`，不冒充独立 session 泛化。
- **已知边界：** 低功耗仅验收 WFI 软件/状态链，未量化真实电流、节电百分比或 9000 mAh
  续航；100 次掉电、72 小时长稳和完整物理断连矩阵为 deferred reliability。原 event 1–204
  已外部完整归档后经单独授权清除，raw 按项目政策不入 Git。

## 历史问题总览与后续增强

下表从早期审查持续追加，用于解释问题如何关闭或延期；它不是比上方 V1 发布快照更新的状态页。

### 分类定义

- **A 类：**不依赖实物，现在就可以用确定性测试完成和关闭。
- **B 类：**现在可完成软件骨架与模拟验证，但最终适配和验收必须保留给硬件。
- **C 类：**缺少物理测量就无法负责任地选定参数/架构，现在禁止凭感觉修改。

**2026-08-17 结论：A-01 至 A-05 的历史问题状态不变；当日 AI 合入与收尾审查新增
A-06 至 A-09。** 其中 A-06、A-07、A-08 必须在正式重新训练/冻结最终模型前关闭；
A-09 是创建 Release Candidate 前的工程门禁。A-01 的剩余 runtime/HAL 绑定仍归 B-01。

### A 类真实性复审结果

| ID | 原问题 | 优先级 | 状态 | 真实性结论/处理 |
|---|---|---:|---|---|
| A-01 | Phase 07 runtime composition 未接入 RTC/time、SHT4x/environment、reset/power provider、worker 和诊断命令 | P1 | `RECLASSIFIED` → B-01 | 功能缺口真实，批准的 Phase 07 设计也把 composition/HAL integration 列为软件验收；但当前 BSP 没有 I2C/RTC provider，固件未链接对应 HAL 单元，本任务又限定为无硬件可确定关闭的问题。fake 只能证明调用顺序，不能证明实际启动链，因此不新增 fake-only composition，也不谎称 Phase 07 软件验收完成。 |
| A-02 | `environment_service` 快照无一致发布保护 | P2 | `CLOSED` | 在 worker 接入前采用与 health service 相同的短 mutex，初始化、错误/成功发布和完整快照复制使用同一把锁；native test 验证发布/读取成对加锁。未改变 I2C 事务或采样策略。 |
| A-03 | CLI 把未分类异常统一映射为 `EXIT_INCOMPATIBLE` | P3 | `CLOSED` | 新增内部错误退出码 1；设备拒绝或 HELLO 成功响应中的版本不兼容均映射为 5，其他设备拒绝仍为 3，传输/协议仍为 2/4。失败优先测试覆盖相关错误路径。 |
| A-04 | TERP 字段布局多处手写，`--verbose` 无 TX/RX 帧摘要 | P3 | verbose `CLOSED`；字段生成 `MONITORED` | client 在真实请求/响应边界只发布不含 payload 的只读 `FrameSummary`；CLI 打印类型、sequence、flags、payload 长度。trace sink 异常会被隔离并关闭 trace，不改变协议 I/O/状态机。字段重复无当前行为漂移证据，因此不重写序列化层。 |
| A-05 | `board_pinmap.md` 的 JEDEC 要求与驱动不一致 | P3 | `CLOSED` | 文档已改为完整 `EF 40 17` 白名单和表项几何；U2 实读仍是硬件门禁。 |
| A-06 | AI 冻结划分未进入训练入口，训练报告与提交产物不能完整互证 | P1 | `SOFTWARE_DONE` | 训练入口强制消费 split、dataset manifest 和 `build_feature_dataset` metadata；实际校验 manifest/源文件 SHA、事件身份/标签/源哈希、四个特征输入文件哈希和完整分区覆盖，并把证据写入报告。正式重训仍需新数据，不能把 pilot 占位指标当作 C-11 结论。 |
| A-07 | MCU 模型 CRC 未覆盖实际参与推理的完整模型数据 | P1 | `SOFTWARE_DONE` | Host 包加载器校验 runtime/feature/hidden 契约、manifest 量化尺度、浮点权重重新量化结果和 int8 张量；导出器只使用已校验包，运行时/包版本为 v2。掉电/存储损坏仍归硬件门禁。 |
| A-08 | Host/MCU 缺少共同黄金向量与正式评价报告生成链 | P1 | `SOFTWARE_DONE` | Host/native 共享黄金向量；评价入口绑定有效 v2 模型包、baseline 配置、冻结 split 的 test 分区、特征/metadata 和预测代码 SHA-256，交叉核对 manifest/source SHA、test 身份/标签/顺序、特征行数/哈希和背景时长。正式泛化验收仍归 C-11。 |
| A-09 | Release Candidate 构建、身份和入口冻结 | P2 | `CLOSED` | Release gate 已绑定候选 40 位 revision/serial 和 ELF/BIN/MAP 哈希，`v1.0.0` tag 已建立，V1 软件与必做实板门禁已归档到发布证据包；脏树仍只能输出 `DIAGNOSTIC ONLY / NOT RC PASS`。 |

### 已确认关闭、但旧审查截图仍可能标成未完成的项目

| 项目 | 当前状态 | 软件关闭证据 | 仍需硬件验证 |
|---|---|---|---|
| 真实触发 sample/block 边界 | `CLOSED` | `c77c467` 保存块内 trigger index；EV03 的 pre 不含触发样本，post 从触发样本开始；块首/中/尾 native test 通过 | 用真实冲击波形核对触发线与物理冲击时刻 |
| 丢样证据链 | `SOFTWARE_DONE` | EV03 记录 `DATA_LOSS`、丢失数量、首末序号、episode 和单调时间；桌面端区分显示数据丢失、饱和、UTC 无效和存储错误 | 在 1.6 kHz 压力、Flash 擦写和长稳条件下验证统计不漏报、不误报 |
| Phase 09 桌面端截图所列缺口 | `CLOSED` | 已支持追加分页、annotation UI/持久化、JSON 导出标签与备注，以及饱和/数据缺口等独立回放警告；相关 host/UI tests 已进入统一测试集 | 真设备持续下载、断连恢复和大数据量交互仍属于系统验收 |

### B 类：可先做软件，最终保留硬件门禁

| ID | 项目 | 性质/状态 | 现在可完成 | 必须留给硬件 |
|---|---|---|---|---|
| B-01 | Phase 07 runtime | 服务层完成 / `PENDING_HARDWARE` | time/environment/reset/health 服务及 fake 单元测试已经存在；environment 快照已具备一致发布保护 | 先确认实际 BSP 的 I2C/RTC provider 与启动行为，再实现真实 composition、worker、诊断和 health 聚合；RTC/LSE/VBAT、SHT4x、PVD/IWDG 必须实板验收。fake-only 调用顺序测试不能单独关闭本项 |
| B-02 | JEDEC 验证 | `CLOSED` | 完整 ID 白名单/器件表、未知器件拒绝和 fake transport 测试已完成 | 精选实板证据已记录 U2 `EF 40 17`，受控测试区擦除/编程/读回与掉电保持通过；证据见 `evidence/hardware-bringup/curated-closeout.md` |
| B-03 | Flash 查询与性能 | 软件部分完成 / `SOFTWARE_DONE` | 稀疏 RAM 索引、顺序分页、读取字节/次数上界已完成 | SPI 稳定频率、真实擦除/写入延迟和 1.6 kHz 并发余量 |
| B-04 | 固件 OTA / 模型 OTA | 固件 `SOFTWARE_DONE` + 实板部分通过；模型 `SOFTWARE_DONE` / 核心实链通过，可靠性 `DEFERRED_POST_CORE` | 固件包、CRC/SHA、版本/地址检查、候选/恢复槽、Bootloader 状态机、断点复制、Trial/确认和 host/native 测试已实现；模型 OTA 已补齐 TRMD 包校验、Model A/B 上传、双状态 bank 的追加/GC、提交记录读回、运行时 prepare/publish/quarantine、TERP 查询与 Host client 闭环；模型 publish 失败会隔离当前运行时并要求重启 remount | 固件安装随机断电、独立救援/看门狗，以及模型 QSPI 掉电、重复安装、运行时并发和完整 HIL 降级为后续可靠性验证，不阻塞当前核心交付 |
| B-05 | CAN FD / ISO-TP | 后续功能 / `OPEN` | 编解码器、分段重组、超时和错误状态机 | 收发器、仲裁、bus-off、终端电阻和实际总线时序 |
| B-06 | USB CDC | 后续功能 / `OPEN` | 复用 TERP 字节通道边界和 session 状态机，实现 CDC 适配层的可测试骨架 | D+/D-/VBUS、48 MHz、合法 VID/PID、UID 序列号、枚举和插拔恢复 |
| B-07 | 高 g、倾斜/倒置、环境越界和开箱事件闭环 | 后续功能 / `OPEN` | 先确定事件语义、记录字段、状态机和桌面端呈现，再为可确定逻辑增加 native/host tests | 高 g 传感器量程与带宽、安装方向、温湿度边界、开箱传感器和真实场景误报/漏报 |
| B-08 | Phase 13 端到端可靠性验收 | 修复前两小时已完成但严格 G3 `FAIL_POOL_BACKPRESSURE_OBSERVED`；pool 缺陷修复 `SCOPED_PASS`；修复后完整 2 小时 `DEFERRED_POST_CORE` | 保留修复前完整两小时的缺陷证据，并以最终提交镜像完成 240 秒并发事件导出回归，记录健康、事件读回、错误、pool/backpressure 和最小余量；不把短回归写成修复后完整 G3 PASS | 修复后 2 小时完整长跑、72 小时连续运行、至少 100 次受控掉电、物理断连/恢复、OTA 断电/回滚、CAN bus-off 及其完整矩阵仍是后续可靠性验证 |
| B-09 | AI 结果产品闭环 | `SOFTWARE_DONE` + G1 实板闭环通过；长稳可靠性 `DEFERRED_POST_CORE` | 已定义并实现 event ID、模型版本、类别、logits/置信度、输入质量、失败原因和结果序号；固件以带 CRC/提交标记及独立物理写入代数的 128-byte QSPI sidecar 记录并通过 TERP `GET_AI_RESULT` 查询，Host 独立于人工 annotation 持久化、展示和 JSON 导出；模型失败只记录结果，不阻止原始事件保存；G1 已完成真设备下载及 Host 回放闭环 | 多轮掉电/长稳下的结果追溯与并发压力作为后续可靠性验证，不阻塞当前核心交付 |

### 核心交付硬件验收路线（只补未闭合项）

范围仅含 B-03/B-04/B-08 核心子门/B-09；CAN、USB CDC、高 g、RTC/runtime 和最终模型泛化
（B-01、B-05～B-07、C-04、C-11）不在本轮。既有 H1、H3～H7、UART3 基础下载、固件正常
升降级和 Pilot AI 冒烟证据直接复用，不再单独重复；以下门禁必须按顺序执行，失败即停止。

| 门禁 | 只补的验收内容 | PASS 标准 |
|---|---|---|
| G0 最终镜像基线 | 记录 `main` SHA、ELF/BIN SHA、设备 serial、模型版本/CRC 和起始 Flash 状态；烧录最终 RC 后做 `10/10` 冷启动及 UART3 `info/health` 冒烟 | 身份全部匹配；10 次均正常启动，无 HardFault/assert/意外复位；历史基础能力无回归 |
| G1 新能力闭环 | UART3 连续完成模型 A→B→A，累计至少 26 次激活以跨过一次状态 bank GC；生成足以跨过一个 AI sidecar 扇区边界的连续事件，逐事件核对原始记录、`GET_AI_RESULT`、SQLite/UI/JSON，并在重启后复查 | 每次激活的版本/CRC 正确；事件 ID、结果序号和模型身份不串号、不丢失；模型失败不影响原始事件；人工 annotation 始终与模型结果分离 |
| G2 掉电恢复 | `DEFERRED_POST_CORE`：原 100 次受控断电矩阵保留为后续可靠性计划，本轮不执行 | 不计入当前核心交付的 PASS；恢复执行时仍按原合同验证已提交数据、撕裂记录和固件/模型恢复语义 |
| G3 并发预验收 | 修复前已完成完整 `7200 s` 两小时窗口，但严格结果为 `FAIL_POOL_BACKPRESSURE_OBSERVED`；修复后以最终提交镜像执行 sample-pool export backpressure scoped 回归：1.6 kHz 采集、U2 写入、AI 推理、UART3 事件读回，15 秒心跳，240 秒、2 次受控事件 | 修复前两小时证据保留并明确为严格失败；修复后 scoped 回归要求无 HardFault/assert/意外复位、两条新事件可追溯、EV03/AI/存储/导出/队列错误为 0、`pool_backpressure=0` 且 `pool_min_free>0`；修复后完整 2 小时不在本次声明范围 |
| G3 长稳与物理断连 | `DEFERRED_POST_CORE`：原 13 次物理断连补测及 72 小时连续运行保留为后续可靠性计划，本轮不执行 | 不计入当前核心交付的 PASS；恢复执行时仍按原合同验证断连恢复和长期稳定性 |

每个门禁保存 `metadata.json`、固件/模型哈希、原始串口日志、机器判定和 `summary.md` 到
`evidence/hardware-bringup/<date>/core-acceptance-gN/`。G0、G1 已通过；G3 的修复前两小时
预验收已完成但严格失败，修复后的独立 pool 缺陷已完成 `SCOPED_PASS`。修复后完整两小时、G2、
G3 长稳/物理断连以及 B-04/B-08/B-09 的完整可靠性矩阵保持 `DEFERRED_POST_CORE`，不把
修复前两小时的严格失败误写成缺失或通过。

### 2026-08-18 G0/G1 基础硬件记录（UART3/UART1 已确认）

- **G0：`PASS` / `10_OF_10_COLD_BOOT`。** 合并后 `main` 提交
  `4c47fe69b544ef9327300910d66f04927871f0dc` 的 Release 镜像已完成应用区
  `0x08020000` 刷写/校验；BIN SHA-256 为
  `2F0E676507D1591060BA759D8F81C6DE472D302988172A22DEF627365B5E878A`，设备为
  `STM32H743` / `recorder-001`，UART3/COM7（PD8/PD9）。
  操作者完成 10 次物理断电再上电，正式轮次 1～10 全部通过 UART3 `info`、`health`、事件列表和模型状态只读检查；
  身份全部匹配，事件 ID 1～39 稳定，`storage_ready=true`，存储/导出错误均为 0，
  `model_valid=true` 且 `pending_install=false`，未观察到 HardFault/assert/意外复位。
  第3轮以 `round-03-redo.json` 计数，初次超时排障记录不计入正式轮次。
  完整汇总、逐轮记录、帧摘要和哈希见
  [`core-acceptance-g0/summary.md`](../../evidence/hardware-bringup/2026-08-18/core-acceptance-g0/summary.md)、
  [`core-acceptance-g0/metadata.json`](../../evidence/hardware-bringup/2026-08-18/core-acceptance-g0/metadata.json)
  和 [`cold-boot-10-manual/run-summary.json`](../../evidence/hardware-bringup/2026-08-18/core-acceptance-g0/cold-boot-10-manual/run-summary.json)。
  `health state=2` 仍只作为已知 `HEALTH_DEGRADED` 观测，未写成整机 health PASS。
  G0 已关闭；截至该轮次，核心交付仅剩 G3 的 2 小时预验收。G2 掉电矩阵、G3 的物理断连/72 小时长稳
  及 B-04/B-08/B-09 的完整可靠性矩阵均为 `DEFERRED_POST_CORE`，不阻塞本轮收尾。
 - **G1：`PASS` / `UART3_READBACK_HOST + CONTROLLED_MODEL_FAILURE`。** 旧镜像在
  Model A `finalize` 阶段发生 TERP 超时并由 ST-LINK 确认为 HardFault，`CFSR=0x01000000`
  （UNALIGNED），PC 指向 `icm45686_read_fifo_count`；独立工作树中的修复及 TERP worker
  栈 `2048 -> 4096` 已获指定审查任务 `APPROVE`，提交 `4c47fe6` 已快进合入 `main`。
  合并后的 `main` Release 统一门禁通过，应用区重刷/校验成功，Model A finalize 复核成功。
- 候选 BIN `BEDAE83E...8250EA1` 已完成并落盘 26 次 A/B 交替在线激活；合并后 main BIN
  `2F0E6765...B5E878A` 也已完成并落盘 26 次 B/A 交替在线激活。两份 BIN 同属
  `4c47fe6`、长度均为 128952，但因固件编入 `__TIME__` 而按精确产物分开归因；不能把候选
  证据静默写成 main 产物证据。每次返回 `total_bytes=verified_bytes=510`、
  `pending_install=false`、`model_valid=true`，且物理 `active_slot` 相对前次翻转，期间 UART3
  未掉线且未观察到 HardFault。`health state=2` 只记录为 `HEALTH_DEGRADED`，不能写成整机
  health PASS。原始字节日志、逐帧摘要、机器判定和合并后刷写日志见
  [`core-acceptance-g1/summary.md`](../../evidence/hardware-bringup/2026-08-18/core-acceptance-g1/summary.md)、
  [`core-acceptance-g1/metadata.json`](../../evidence/hardware-bringup/2026-08-18/core-acceptance-g1/metadata.json)
  及其 `model-ota-activation-logged*` / `merged-main-reflash` 子目录。
- **UART1/连续事件子门：指定审查任务结论为 `APPROVE（限缩子门记录）`。**
  `uart1-finsh-followup/` 的 `ps` 证明 `terp_rx` 使用 4096 B 栈、当前高水位 3%；
  `uart1-event-boundary/` 在不格式化 U2 的前提下完成事件 ID 6～38 共 33/33 次，
  AI `submitted=33 processed=33`，事件/导出/资源/队列/持久化/特征/runtime 错误均为 0；
  `uart1-software-reset-remount/` 的 ST-Link 软件复位返回 0，复位后出现
  `AI result sidecar=ready`，且 `events=38`、`scanned=7 discarded=0`。负载观察到
  `ai=91%`、`event=81%`、`pool_backpressure=597`，仅作为 C-03/B-08 风险记录，不能关闭 G3。
  四组证据目录及 raw/console SHA 已写入
  [`core-acceptance-g1/summary.md`](../../evidence/hardware-bringup/2026-08-18/core-acceptance-g1/summary.md)
  和 [`core-acceptance-g1/metadata.json`](../../evidence/hardware-bringup/2026-08-18/core-acceptance-g1/metadata.json)。
 - **UART3 读取与 Host 闭环已完成。** `uart3-event-readback-logged/` 在 UART3/COM7
  下载事件 6～38 共 33/33 条；原始记录、设备全文 CRC、EV02/EV03 解码、事件 ID、
  `GET_AI_RESULT`、结果序号、SQLite/UI/JSON/Host 回放均一致，annotation 与模型结果
  独立。raw UART SHA-256 为
  `AD171AC30230F1A344C7A8909BDB3500853215D121BE1BD4C5A094220D427D35`，逐帧摘要为
  `F4E6539A2B094C5B3C499EBDCF459A238A13E69DA4118AAF65067DB532793969`，机器汇总为
  `C6FC8510DA6B8A9705D41152D563D04B575A6513A347A8A930199D19F09229B6`。
 - **受控模型失败技术子门已完成。** 通过 ST-Link SWD 仅修改易失 SRAM 运行态（非公开
  UART 命令、未写 Flash/QSPI）触发事件 39；成功注入日志
  `stlink-controlled-model-failure.log` SHA-256 为
  `4EAA6E28E336416A39EC19932D38D80A70DBF6EA5C636195E1C0C6E17CFBBAAE`。事件 39 为
  有效 EV03、38560 B，设备/下载全文 CRC 均为 `3745476147`，原始文件 SHA-256 为
  `CD30BC5A5B01058D9FCEE7CF5A4383711C02E8B79E601260120E7AFC7FF545E5`；设备返回
  `MODEL_UNAVAILABLE/NO_MODEL`，Host SQLite/JSON/UI 保留失败结果且标注为空。
 - **断电恢复观察已完成。** `stlink-controlled-model-failure/recovery-after-power-cycle.json`
  SHA-256 为 `1548C94DF7E88610E0E9DB30087581C895DF6FCC0ADFBD7637B67634BCCEC6FA`；
  物理断电上电后事件 39 与失败结果仍可读，`storage_ready=true`、存储/导出错误为 0，
  `MODEL_OTA_QUERY model_valid=true/pending_install=false`。该单次观察不替代 G2 掉电矩阵。
  - 指定审查任务首次结论为 `REQUEST CHANGES`，仅要求将上述成功注入日志、历史 GDB
   状态日志、断电恢复文件及哈希写入 summary/metadata/本台账；代码无需修改。补齐后
  最终复审结论为 `PASS`。G0 的 `10/10` 完整冷启动及 G1 闭环已完成；截至该轮次，核心交付仅保留
  G3 的 2 小时预验收。G2 掉电、G3 物理断连/72 小时及 B-04/B-08/B-09 完整可靠性矩阵均为
  `DEFERRED_POST_CORE`，不阻塞本轮收尾。
- **G2/G3 前置准备与当前范围。** G2 已建立 100 轮分段矩阵并完成一次只读基线读回：
  UART3/COM7 身份一致、事件 39 全文 CRC/下载 SHA 一致、存储错误为 0、模型有效；正式掉电轮次仍为 `0/100`，
  但已降级为后续可靠性验证。G3 的物理断连合同（历史 7 次补足至 20 次）和 72 小时连续运行同样延期；
  本轮只执行 2 小时并发预验收。
  操作表、元数据模板和只读读回工具见
  [`core-acceptance-g2/README.md`](../../evidence/hardware-bringup/2026-08-18/core-acceptance-g2/README.md)、
  [`core-acceptance-g3/README.md`](../../evidence/hardware-bringup/2026-08-18/core-acceptance-g3/README.md)
  和 [`scripts/hardware_acceptance_readback.py`](../../scripts/hardware_acceptance_readback.py)。
  G3 物理矩阵脚本已补齐完整 `LIST_EVENTS` 分页、目标事件/CRC 缺失即失败，以及
  `LIST_EVENTS` 设备 CRC、`GET_EVENT_INFO` 设备 CRC、下载字节 CRC 的三方比对；Event 39
  只读诊断轮实测跨 3 页，三项均为 `3745476147`，但因未拔插仍为
  `DIAGNOSTIC_NOT_PHYSICAL`，不计入正式轮次。指定审查任务复审结论为 `PASS`；G2/G3
  正式断连矩阵仍未执行，且不计入本轮核心交付；G3 两小时预验收的实际结果见下节。

### 2026-08-21 G3 两小时并发预验收（修复前镜像）

- **执行状态：`COMPLETED_STRICT_FAIL_POOL_BACKPRESSURE`。** 独立工作树
  `fix/hardware-2h-run-20260821` 使用板上已通过 G0/G1 的 Release 镜像，在 UART1/COM6、UART3/COM7
  和电池供电下完成完整 `7200 s` 窗口；T0 为 `2026-08-21T18:34:00.767179+08:00`，结束于
  `2026-08-21T20:34:00.791932+08:00`。前 10 分钟每 30 秒详细采样，之后每 60 秒轻量心跳并每 5 分钟
  补采完整 FinSH 状态，共保存 `130` 个心跳（详细 `41`、轻量 `89`）。
- **通过项。** 无 HardFault/assert/意外复位；EV03 事件 39/40/41 的下载、CRC、解码和 AI 结果读回均通过，
  `lost_sample_count=0`；AI `submitted=2/processed=2`、queue drop/result queue drop/存储/导出/特征/runtime
  错误均为 0；任务栈最高使用率 `86%`；Model A→B→A→B 四次均 `510/510`、valid、非 pending，物理槽
  `0→1→0→1`；事件数 `39→40→41`。
- **严格失败项。** `sample_block_pool` 的 `pool_backpressure` 在两次事件触发期间累计最高 `26`。
  台账 G3 严格标准要求无 pool/栈耗尽，因此本轮不能标记 G3 PASS。`sample_pool_min_free` 运行时输入尚未
  接线，EV03 中的 `0` 是 unavailable/default，不能用来掩盖该观测缺口。
- **证据。** 完整目录已纳入仓库：
  [`two-hour-preacceptance-001/summary.md`](../../evidence/hardware-bringup/2026-08-21/core-acceptance-g3/two-hour-preacceptance-001/summary.md)、
  [`metrics-summary.json`](../../evidence/hardware-bringup/2026-08-21/core-acceptance-g3/two-hour-preacceptance-001/monitor/run-001/metrics-summary.json)、
  [`run-summary.json`](../../evidence/hardware-bringup/2026-08-21/core-acceptance-g3/two-hour-preacceptance-001/monitor/run-001/run-summary.json)。
  该次两小时运行是修复前的缺陷定位证据，不是修复后通过证据。

### 2026-08-22 sample-pool export headroom 修复与最终提交镜像回归

- **问题定位。** 2026-08-21 的完整两小时预验收已实际完成；旧 `SAMPLE_BLOCK_POOL_SIZE=128`
  在同步事件导出期间无法同时覆盖最大事件持有、采集继续运行和 handoff 余量，第一次事件后
  `pool_backpressure` 从 0 增至 13，第二次增至 26，其他错误计数保持 0。完整原始证据见
  [`two-hour-preacceptance-001/summary.md`](../../evidence/hardware-bringup/2026-08-21/core-acceptance-g3/two-hour-preacceptance-001/summary.md)。
  该修复前镜像结果是缺陷证据，不是修复后通过证据。
- **软件修复。** 提交 `0a9ff12e5c40abdd3ad0506418b022db9914e252` 将固定池调整为
  192 blocks，明确 64-block export headroom，并以 `100 + 64 + 8 <= 192` 静态断言和
  native regression 固化最大事件、export、handoff 契约；新增 `pool_min_free` 诊断并接入
  UART1/runtime health。未改变协议帧、Flash 地址、BSP/DMA/ISR 或任务优先级。
- **软件与审查。** Native C、统一 host/AI/bootloader 测试、Release ARM 构建、内存映射和
  `git diff --check` 均通过；指定子 agent Tesla 最终结论为 `APPROVE`，无遗留 findings。
- **最终提交镜像。** Release BIN 为 129,088 bytes，SHA-256 为
  `E7836F4D12B3FC7AAB6A7FE7FECF3262393EDCCC0B8785F8CB536D8A3D071D37`，设备返回 revision
  `0a9ff127...`；该 40 字符串是历史构建输入笔误，不是 Git 对象，实际短提交 `0a9ff12`
  解析为 `0a9ff12e5c40abdd3ad0506418b022db9914e252`。ST-LINK 在 `0x08020000` 刷写/校验通过；
  被测二进制仍由上述长度和 SHA-256 唯一绑定。证据入口为
  [`pool-backpressure-regression-001/README.md`](../../evidence/hardware-bringup/2026-08-22/core-acceptance-g3/pool-backpressure-regression-001/README.md)
  和 [`run-003-final-commit/hardware-run/run-summary.json`](../../evidence/hardware-bringup/2026-08-22/core-acceptance-g3/pool-backpressure-regression-001/run-003-final-commit/hardware-run/run-summary.json)。
- **硬件 scoped PASS。** UART1/COM6 与 UART3/COM7 连续运行 240 秒，15 秒心跳 16 次，
  受控触发并读回 event 47、48；`pool_backpressure` 最大值 0、`pool_min_free` 最低 49，
  FIFO/DMA/导出/资源/队列/存储/特征/runtime 错误均为 0，无 fatal marker，机器判定为
  `SCOPED_PASS`。这关闭本次独立 pool 缺陷，但不冒充完整 2 小时 G3、72 小时长稳、物理
  断连或掉电矩阵；这些仍按当前范围 `DEFERRED_POST_CORE`。

### C 类：必须等硬件测量后再定

| ID | 冻结项 | 需要的硬件证据 | 解冻后可能的改动 |
|---|---|---|---|
| C-01 | SPI2 分频和最高稳定频率 | 不同分频下的 CS/CLK/MOSI/MISO 波形、JEDEC/连续读写错误率 | 调整 SPI2 分频、CS 建立/保持时间和超时 |
| C-02 | 是否引入异步 Flash writer、后台擦除或额外队列 | 最坏 page program/sector erase 延迟、pool minimum、backpressure、EV03 lost samples | 仅在同步路径确实超出余量时增加队列/后台擦除；否则保持简单同步架构 |
| C-03 | RT-Thread 任务优先级、栈大小和时间片 | 真实负载下的栈高水位、调度延迟、watchdog 进度和丢样 | 只调有证据超限的任务参数 |
| C-04 | FIFO 水位、DMA 块大小、IMU 自动触发阈值 | V1 已启用 impact（5120 counts、连续 2 samples、ABOVE）和 drop（1536 counts、连续 8 samples、BELOW）自然触发，并取得静置无误触发、自然事件写入及 4112 B FIFO 排空证据 | V1 核心门已关闭；跨安装方式、不同运输域的误报/漏报统计属于后续泛化标定，调整阈值必须建立新版本和实板证据 |
| C-05 | I2C 上拉、RTC/LSE 启动超时、VBAT 保持 | 总线上拉/上升时间、LSE 起振时间、断主电后 RTC 连续性 | 调 I2C 时钟/超时，核算或更换上拉，调 LSE 驱动/超时，必要时返修 VBAT 路径 |
| C-06 | IWDG 窗口和喂狗周期 | 真实 LSI 偏差、故意停滞关键 worker 时的复位时间/原因 | 选定实际超时和喂狗周期，在证据完整前不开物理 IWDG |
| C-07 | USB CDC 缓冲区、短写和断连恢复参数 | 实际 USB stack 回调/短写、主机插拔和持续下载数据 | 调 TX/RX buffer、busy 重试、帧间超时和 session reset |
| C-08 | CAN FD 位时序和 bus-off 恢复时间 | 实际时钟、收发器、线长/终端、总线负载和 bus-off 注入 | 选定 nominal/data bit timing、sample point 和恢复策略 |
| C-09 | BSP 公共化重构和旧 WeAct BSP 删除 | 新板引脚/时钟/外设闭环稳定，确认旧 BSP 无维护用途 | 这不是电气参数，但必须等板级基线稳定后再做，避免在 bring-up 期间移动调试目标 |
| C-10 | 高 g 传感器选型、量程、带宽和触发阈值 | 目标冲击谱、饱和率、噪声、安装方式以及与现有 IMU 的时间对齐数据 | 根据测量决定器件/接口、量程、ODR、滤波、触发阈值和是否需要独立采集链 |
| C-11 | 独立 session 的 AI 泛化验收 | V1 已完成 91 条合格真实事件的四分类受控 pilot、固定 64/14/13 split、int8 test 10/13、macro-F1 0.755952 和 MCU 部署；每类仍只有一个受控 session | V1 真实 AI 链路已关闭；若升级为跨场景泛化声明，仍需至少 3 个独立 session/类并冻结独立测试集，报告 confusion matrix、每类 P/R/F1、事件召回、背景每小时误报和失败案例 |

**C-02 特别约束：**“同步 Flash 写入一定要改成异步”目前只是风险推断。必须先使用已有
稀疏索引和读取计数，再增加实板耗时/pool 水位/lost-sample 统计；只有测量证明同步路径不足时
才增加 writer 队列或后台擦除，避免为假问题增加所有权和调度复杂度。

## 2026-08-17 AI 合入与全项目收尾审查

### 审查固定点与准入结论

- AI 提交：`47ded4a feat(ai): integrate pilot model inference`；合并后的 `main`：`63f6bee`。
- `47ded4a` 与 `63f6bee` 的代码树一致，因此 2026-08-17 Pilot HIL 可以证明当时合入代码的
  AI 冒烟行为；它在当时仍不是整机 Release 验收。
- 新鲜软件门禁：Host `124 passed`、AI `57 passed`、native C 与 Bootloader PASS；ARM 构建、
  应用向量和内存映射 PASS，尺寸为 `text=182332, data=1532, bss=187892`；审查区间
  `git diff --check` PASS。
- Pilot HIL：模型就绪，静止事件得到 `background`，明显敲击事件得到 `impact`；两条新增事件
  均完成提交和处理，队列丢弃、特征错误和运行时错误为 0。证据见
  `evidence/hardware-bringup/2026-08-17/ai-pilot-hil/summary.md`。
- **准入结论：可以进入收尾阶段；不能宣布 AI 正式验收、整机硬件合同完成或冻结 Release。**

### 本轮发现的去重归属

| 本轮发现 | 台账归属 | 去重说明 |
|---|---|---|
| 正式固件用 `UINT32_MAX` 禁用自动物理触发 | C-04 | 已属于“IMU 触发阈值必须由真实数据决定”，本轮只补充当前产品影响，不新建问题 |
| RTC/SHT4x/reset-power/IWDG 未进入正式 runtime | B-01 | 历史项继续有效，不重复新增 |
| 固件 OTA 剩余矩阵与 AI 模型更新缺失 | B-04 | 更新旧 OTA 条目的当前状态，不再沿用“OTA 整体尚未实现”的过时描述 |
| 72 小时、受控掉电、物理断连和并发压力 | B-08 | 在同一端到端可靠性项中补充 AI queue/pool 指标 |
| AI 冻结划分、产物追溯、实际模型 CRC、共同黄金向量 | A-06、A-07、A-08 | 旧台账没有这些可纯软件关闭的明确问题，因此新增 |
| AI 结果只存在 RAM/UART、没有事件/TERP/上位机闭环 | B-09 | 旧桌面端条目只处理人工 annotation 和原始事件回放，不覆盖模型预测，因此新增 |
| 正式重新采集、独立测试和模型收益验收 | C-11 | 与 C-04 的触发阈值标定不同；这是模型泛化与评价门禁，因此新增 |
| Release 配置、版本身份、统一入口和干净 RC | A-09 | 旧台账没有独立 Release 工程门禁，因此新增 |

### A-06/A-07 的当前可复核证据

- `ai/configs/dataset_v1.yaml` 的 split seed 为 `20260811`，而 Pilot 训练配置为 `20260816`；
  `train_model.py` 在训练内部重新 `_split_by_group()`，命令行没有冻结 split manifest 输入。
- Pilot 的 seed stability 显示测试准确率约为 `0.8108..1.0000`；因此选定 seed 的 `1.0`
  只能作为占位模型结果，不能独立证明泛化。
- `training-report.json` 记录的 model manifest SHA-256 为 `ae90f426...`，当前提交文件实际为
  `7786a647...`；报告中的 generated C SHA-256 为 `6efb9e55...`，当前文件实际为
  `104d20ca...`。`weights.npz` 的 `6f095ed0...` 与报告一致。
- 导出器把 `weights1/weights2` 同时生成成推理数组和复制的 `tensor_bytes`；固件只校验后者，
  实际推理使用前者。当前 `test_ai_runtime.c` 只破坏 CRC/复制字节，没有证明修改实际权重、
  bias、归一化或量化参数会被拒绝。

### 当前 AI 阶段边界

- `pilot-v1` 可以作为占位版和端到端接线证明；真实数据量、类别覆盖和模型指标归 C-11，
  不因用户计划重新采集而把 A-06 至 A-08 延后到训练完成后再处理。
- 建议在正式采集期间就关闭 A-06 至 A-08，避免新数据最终仍进入不可复现或 PC/MCU 不一致的链路。
- 原始事件优先级高于 AI：任何模型失败、低置信度或队列压力都不得阻止事件落盘；B-09
  只增加可追溯预测结果，不把模型变成原始事件保存门禁。

### 2026-08-17 修复工作树：A-06～A-08 软件门禁（返工前快照）

- 工作树：`fix/a06-a09-software-fixes`，路径为
  `LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\a06-a09-software-fixes`；未修改 `main`。
- A-06：训练 CLI 现在必须接收 `--split-manifest`、`--event-ids`、`--report` 和生成 C 输出路径；
  报告记录 source manifest、冻结 split、特征/标签/分组/事件 ID、训练代码、model manifest、
  weights、header 和 C 的 SHA-256。新测试证明冻结分区会覆盖实际训练行，不能被 seed 替换。
- A-07：模型完整性改为对实际推理字段按固定小端顺序计算 CRC，native 测试覆盖 mean、两层权重、
  两层 bias 和输入尺度篡改；pilot C 数据已按新 CRC 重新导出并通过 native model-data 校验。
- A-08：Host 与 native C 直接读取同一 `ai/tests/fixtures/ai_golden_vectors.csv`；四条向量同时
  比较 logits、概率、类别和置信度。`python -m ai.src.generate_evaluation_report` 生成模型/规则
  基线对照包，包含每类指标、事件召回、每小时误报和输入证据哈希。
- 软件验证：AI `61 passed`；native C `37` 个可执行文件通过。当前 pilot 的旧训练报告没有
  私有数据和冻结 split 文件，仍只作为历史占位证据；正式重新采集后必须用新入口生成新的报告，
  才能关闭 C-11 的最终模型验收。

### 2026-08-17 修复工作树：A-09 Release 软件门禁

- 工作树：`fix/release-gate-20260817`，路径为
  `LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\release-gate-20260817`；未修改 `main`。
- `firmware/rtconfig.py` 根据 `TRANSPORT_BUILD_PROFILE` 选择 Debug/Release；Release 使用
  `-O2 -g0 -DNDEBUG -DTRANSPORT_BUILD_RELEASE=1`，`firmware/config/rtconfig.h` 不再启用
  `RT_USING_DEBUG`。revision 和设备序列号通过 SCons 宏注入，且保留 Windows 命令行所需的
  引号；ELF 字符串已实测包含当前 HEAD `63f6beec56cd576a77e607babb665b0b20a55a82` 和 `recorder-001`。
- 新增 `scripts/release_gate.ps1` 作为干净工作树入口；`scripts/run_tests.ps1 -WithFirmware`
  会把 ARM 固件构建纳入统一门禁。Release 构建产物通过 vector/memory-map 检查，尺寸为
  `text=114044, data=1372, bss=187816`；native `36` 个可执行文件、Host `124 passed`、
  AI `57 passed`、脚本测试 `1 passed`，PowerShell 三个入口解析通过，`git diff --check` 通过。
- 当前工作树仍有本轮未提交修改，因此没有把 `release_gate.ps1` 的 clean-tree RC 结果写成已通过；
  硬件刷写、COM8/UART1 运行和正式 Release 冻结仍需在审查及授权后进行。

### 2026-08-17 审查返工状态（A-06～A-09）

- 首轮代码审查结论为 `REQUEST CHANGES`；返工后最终短复审结论为 `APPROVE`，因此上表
  A-06～A-09 已按软件验收条件标记 `SOFTWARE_DONE`。
- A-06 已补上对真实 dataset manifest 的文件哈希、事件身份/标签/源文件哈希校验，并消费
  `metadata.json` 对四个训练输入文件的哈希；A-07 已补上 manifest 与浮点/量化张量的单一事实源；
  A-08 已要求预测 JSON 精确携带模型包、baseline、split、特征、metadata 和预测代码证据哈希，
  且交叉核对 source/manifest SHA，身份/真值必须覆盖冻结 split 的 test 分区；
  A-09 已绑定当前 HEAD、ELF revision/serial 和 ELF/BIN SHA-256，脏树只输出诊断结果。
- 当前最新软件验证：AI `65 passed`、native C `37` 个可执行文件通过；Release 门禁在当前脏
  工作树上完成 ARM Release 构建及全套测试，输出 `DIAGNOSTIC ONLY / NOT RC PASS`。
- 最终审查已通过；干净 RC 冻结和 COM8/UART1 硬件运行验证尚未完成。本轮没有刷写设备，也没有
  修改 `main`，没有提交/推送。

### 2026-08-17 修复工作树：B-09 AI 结果产品闭环

- 工作树：`fix/b04-b09-model-lifecycle`，路径为
  `LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\b04-b09-model-lifecycle`；基于已合入
  A 类修复的 `main`，没有在 `main` 上直接修改。
- 固件新增 `ai_result_t` 和固定 128-byte、format v2 sidecar journal：记录包含 event ID、结果序号、模型版本/CRC、
  状态、类别、四类 logits、置信度、输入质量/事件 flags、样本数、失败原因及独立物理写入代数；记录 CRC 与
  commit marker 用于跳过撕裂记录。重启后的写指针按物理代数恢复，不再用业务结果序号选择待擦除扇区；擦除区
  至少要求 3 个扇区，并同时避开物理最新槽与业务最新槽，否则拒绝破坏性写入。QSPI metadata 区划分为 model
  state `0x0000..0x0FFF`、AI result `0x1000..0xEFFF` 和 HIL scratch `0xF000..0xFFFF`，不改变既有
  candidate/recovery/model A/B 槽地址。
- `GET_AI_RESULT` 使用 TERP `0x0200`，设备端按 event ID 或 latest 返回固定 48-byte 结果；Host client、
  `DeviceSession`、SQLite `ai_results` 表、Qt UI 和 JSON summary 均保留模型结果与人工 annotation 的独立
  字段。旧 schema v1 会在使用前升级到 v2。无 sidecar 时 RAM 结果仍带非零序号，原始事件优先级不变。
- AI service 对结果持久化队列增加容量/高水位/反压统计：失败结果预留正常结果所需容量，正常预测/特征错误/运行时
  错误与失败结果统一进入 peek/ack 队列，QSPI 成功前不出队，错误时退避重试；队列满时不发布未持久化的 RAM-only
  结果，已入队结果可按 event ID 查询。随后完整 `scripts/run_tests.ps1` 已覆盖 sidecar、AI service、TERP
  device/service、Qt UI 和 Host 回归：Native C、Bootloader/image PASS，Host `131 passed`、AI `66 passed`；
  实际 RT-Thread/STM32 头文件 ARM 编译与完整 `scons -C firmware -j4` 也通过，`git diff --check` 通过。
- 第五轮定向代码审查结论为 `APPROVE`；审查确认三扇区准入、victim 同时避开物理/业务 latest、写入读回确认、
  全结果 peek/ack、持久化 credit 反压及 sample block 释放顺序无 P1/P2 缺陷。native RT-Thread stub 不实际调度
  worker，因此持续 QSPI 故障下的真实线程/采样池余量仍属于硬件/RTOS 压力门禁。
- 仍需硬件门禁：COM8 是 UART1/FinSH，不是 TERP UART3；需要在真实 QSPI 上验证 sidecar 挂载、连续多事件、
  重启/掉电恢复、模型失败不影响原始事件、UART3 下载及 Host 回放一致性。72 小时/100 次受控掉电和
  真实 AI 数据/模型验收继续由 B-08/C-11 管理。

### 2026-08-17 修复工作树：B-04 固件/模型 OTA 生命周期

- 工作树：`fix/b04-b09-model-lifecycle`，路径为
  `LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\b04-b09-model-lifecycle`；基于已合入
  A 类修复的 `main`，没有在 `main` 上直接修改；功能实现提交为 `3a21b7f`，后续台账与统一测试门禁修订
  已随本分支 fast-forward 合入 `main`。
- 模型包：新增固定格式 TRMD v2 包、header/payload SHA-256 与 CRC32 校验，Host 导出器、固件解析器、
  Model A/B 槽和容量上界共用同一包边界；最大包长为 2528 bytes，并有 golden-vector/native/Host 测试。
- 生命周期：新增双状态 bank 的 160-byte append-only 记录、提交 marker 最终读回、独立 authoritative bank
  与追加游标；满 bank 时只擦除非权威 bank 并完整读回当前状态，避免 GC 擦除唯一有效记录。安装顺序为
  body 写入/读回、runtime prepare、commit 写入/整条记录读回、runtime publish、再更新 active/authoritative
  RAM 状态。publish 返回值已贯穿 lifecycle、TERP service 和 UART3；失败会消费记录、进入
  `activation_uncertain`、quarantine 并返回 `STORAGE_ERROR`，重启 mount 从已提交的新记录恢复。
- 协议/Host：新增模型 OTA begin/data/finalize/cancel/query 消息，查询在 activation uncertain 时返回内部错误，
  Host client 已覆盖包大小、分块上传和状态映射；COM8 已知是 UART1/FinSH，本项 TERP/模型 OTA 仍走 UART3，
  未把 COM8 连接误写成硬件通过。
- 软件验证：原生 `-Wall -Wextra -Werror` 生命周期、AI service、TERP service、模型包测试通过；
  ARM GCC 使用实际 RT-Thread/STM32 头文件对 AI service、model lifecycle、TERP service/device/UART3、
  QSPI candidate 做 `-fsyntax-only -Werror` 通过；Host 模型包/TERP 测试 `16 passed`，协议生成物检查、
  memory-map 检查和 `git diff --check` 通过；随后使用主工作区共享 `.venv` 执行完整
  `scons -C firmware -j4`，固件 ELF/BIN 链接通过（ROM 207856/1664 KB，RAM 193888/512 KB），
  并执行 `scripts/run_tests.ps1`：Native C、Bootloader/image、Host `131 passed`、AI `66 passed` 全部通过。
- 审查：审查会话 `codex://threads/01a00d9e-973c-7583-b2fb-09f79cdda5da` 最终结论为 `APPROVE`，未发现
  P1/P2；审查确认 publish 成功/失败顺序、quarantine gate 释放、重启 mount 和 authoritative-bank GC。
- 状态边界：模型 OTA 标记 `SOFTWARE_DONE`，但真实 QSPI 掉电/擦写损坏、重复安装、运行时并发、UART3
  传输、看门狗和完整 HIL 仍为 `PENDING_HARDWARE`；本轮不刷写 COM8，软件提交已合入 `main`。

## A 类真实性审计与修复证据（2026-08-09）

- 审计固定点：`4167779`；任务分支：`fix/a-class-audit`。
- A-01 归类证据：`app_runtime.c` 未绑定 time/environment/reset-power；当前 BSP 没有 I2C/RTC
  provider，固件 HAL 源清单也没有 I2C/RTC 单元。批准设计把 HAL integration 列为软件验收，
  所以 Phase 07 软件验收仍未完成；但在本轮“无硬件可确定关闭”边界下，fake-only composition
  不能证明真实启动链，剩余工作与实板 provider/启动行为一起保留在 B-01。
- A-02 修复证据：旧测试先因 `environment_service_t` 没有发布 mutex 而编译失败；最小实现后
  `test_environment_service.exe` 通过。锁只覆盖固定大小快照的赋值/复制，不覆盖 SHT4x I2C
  事务，也没有新增线程、队列或动态内存。
- A-03/A-04 失败证据：聚焦测试在旧实现上出现 4 个预期失败，分别证明内部错误码缺失、
  `INCOMPATIBLE` 被算作普通设备拒绝、verbose 未注入 trace、client 不发布 TX/RX 边界。
  后续又用失败优先测试证明“HELLO 成功但协商版本不支持”曾错误返回协议错误码 4；专用
  `ProtocolIncompatibleError` 修复后通过。独立复审发现初版 trace 仍把完整 `Frame` 交给回调，
  随后用失败优先测试收紧为无 payload 的 `FrameSummary`，并验证 trace sink 抛异常不改变连接。
- A-04 字段生成判定：`terp_messages.yaml` 是消息登记表，但当前生成器只生成 ID，三个既有
  golden 向量也不覆盖全部消息；这是检查能力的已知边界，不得写成“字段自动一致”。目前没有
  发现 C/Python 字段语义不一致，也没有待实施的字段变更；未来修改任一消息字段时必须先增加
  对应跨语言向量，只有重复漂移被证实时才扩展生成器。
- A-05 边界：文档修正不等于读到实物 JEDEC ID；B-02 继续保持硬件门禁。
- 完整软件门禁：协议生成物/golden `--check` 通过，27 个 native C 可执行测试通过，host
  `75 passed`，AI `1 passed`。ARM 固件从该工作树构建并通过 vector/memory-map 检查，尺寸为
  `text=156668, data=1532, bss=179124`。新工作树没有被忽略的 `.venv`，因此明确复用主仓库
  的 Python/SCons 可执行文件逐项运行同一门禁；没有把首次因缺少工作树 `.venv` 的脚本退出
  误报为测试失败或测试通过。以上仍不是任何实板验证。

## 历史审查来源（2026-08-01）

- 审查日期：2026-08-01
- Phase 07 / `main` 固定点：`6d0f97e84ab262088d764125f1ccc8d6de26c896`
- Phase 08 工作树：`feature/terp-usb`，HEAD 与固定点相同；审查范围包含相对 `main` 的修改和全部未跟踪源码、测试、协议及证据文件
- 审查范围：当前 `main` 的完整记录链路，以及 Phase 07、Phase 08 的新增实现、规则遵守、架构、耦合、复杂度、测试证据和 AI 代码盲区
- 初始审查结论：项目主架构仍然可控，没有形成依赖环或失控耦合；但早期阶段遗留的数据语义、Flash 安全、资源余量和硬件证据问题仍未关闭。该初始快照中的 Phase 08 runtime 问题已由下方“后续更新”取代；它仍不构成 USB CDC 或实板验收。

优先级：P1 = 合入/阶段验收阻塞；P2 = 应在进入实板或依赖该接口前修复；P3 = 可维护性或诊断准确性问题。

## 后续更新：无硬件 A 类审查修复与返板清单（2026-08-09）

- 当时的 `main`：`c77c467619ab9c4447316e5561f604683a54f312`（`fix(core): resolve software-only audit findings`）。
- 本节是截至 2026-08-09 对 2026-08-01 问题状态的判定；下文保留初始审查的代码位置和原始证据，
  原始描述均按当时状态加注，不得把历史固定点重新解释为当前缺陷。
- 本次只关闭可以用确定性软件测试证明的 A 类问题；构建通过不代表实板、电气、
  吞吐、掉电或长稳验收通过。

### 原问题对照结果

| 2026-08-01 问题 | 截至 2026-08-09 的状态 | 本次修复证据/剩余边界 |
|---|---|---|
| Phase 05 真实触发 sample/block 边界 | **已由 `c77c467` 关闭软件问题** | block 保存首样本序号和时间，trigger 保存块内 index；EV03 的 `pretrigger_samples` 不含触发样本，`posttrigger_samples` 从触发样本开始。块首/中/尾原生测试通过。实物冲击波形仍需标定。 |
| Phase 06 完整 JEDEC 白名单 | **已由 `c77c467` 关闭软件问题** | 只接受 Winbond W25Q64 `EF 40 17`，容量/page/sector 从器件表派生；错厂商、类型、容量以及全 `00`/`FF` 均拒绝。**U2 实读 ID 仍是硬件门禁。** |
| Phase 06 不可绕过的 format 二次确认 | **已由 `c77c467` 关闭软件问题** | 公开裸 `storage_service_format()` 已删除，只保留 request/confirm；未请求、超时、重复确认均不调用 fake NOR erase。真实芯片擦除与掉电仍需实板测试。 |
| sample pool 余量与同步 Flash 擦写 | **未关闭，需硬件数据后决定软件调整** | 本次未改 SPI 分频、擦除超时、任务优先级、FIFO 水位或存储 writer 架构。需用 1.6 kHz 实采集和最坏擦写延迟判断是否引入异步 writer/预擦除。 |
| health/acquisition/environment 快照并发一致性 | **软件问题已关闭** | `c77c467` 用同一 mutex 保护 health evaluate/watchdog/snapshot，IMU stats 在短中断临界区一次复制；本轮又为 environment 的错误/成功发布和完整复制增加同一短 mutex。实际 RTOS 调度仍随 B-01 做实板验收。 |
| 32 位 RT-Thread tick 约 49.7 天回绕 | **已由 `c77c467` 关闭软件问题** | 新增共享 64 位单调微秒时钟，原生测试注入 `0xFFFFFFFE, 0xFFFFFFFF, 0, 1`；采集和 health 统一使用。板上长跑仍需验证真实 tick 配置。 |
| Phase 07 provider/runtime/HAL/诊断接线 | **未关闭** | 当前 runtime 仍未初始化/poll RTC、SHT4x、reset-power，health 中的 UTC/environment/power 大多保持默认值；`health_service_watchdog_feed_allowed()` 仍无运行时喂狗路径。这里同时包含可无硬件完成的 composition 软件工作和必须上板的物理验收。 |
| Phase 08 TERP runtime/UART、HELLO、TX 错误、storage-ready、parser 重同步、CLI 握手/超时 | **已在 `c77c467` 之前关闭软件问题** | UART3 PD8/PD9 已接 runtime，设备端强制 HELLO/session，TX 有计数，`storage_ready` 读真实 EL01 状态，C/Python parser 有嵌套合法帧恢复测试。**UART 实链、物理断连和 USB CDC 仍未验证。** |
| AI 生成代码的共同盲区 | **风险仍存在，本次有缓解但不能宣称关闭** | 本次增加失败优先回归、生产服务边界测试、fake NOR 读取成本上界、EV03 固定 fixture 和编译告警门禁。这些仍不能证明 ISR/RTOS 时序、电气行为、真掉电或长稳。 |

### 本次额外关闭的后期审查项

- EV03 显式记录样本缺口数量、首末序号、episode 和单调时间；桌面端对
  `DATA_LOSS`、饱和、UTC 无效和存储错误给出不同警告。
- EL01 新增固定上界的稀疏元数据索引、最近记录缓存和顺序分页；显式 verify/recovery
  仍做完整 payload CRC，不用性能换取恢复安全。
- 主机 `DeviceSession` 串行化设备操作：只有用户取消可回 READY，协议/CRC/传输错误
  会关闭不可用 client 并进入 ERROR。桌面端已支持追加分页和持久化 annotation。

### `c77c467` 时点的软件问题清单（历史来源，状态已更新）

以下条目保留来源，但不再使用“仍未关闭”描述当前状态；当前状态以文件顶部表格为准。

1. **Phase 07 runtime composition（P1，`RECLASSIFIED` → B-01）：**真实 composition、worker、
   诊断和 health 聚合仍未闭环，不能用 fake-only 接线关闭。
2. **environment snapshot 发布（P2，`CLOSED`）：**现已使用短 mutex 保护完整发布和复制；
   锁不覆盖 I2C 事务。
3. **CLI 未知异常退出码（P3，`CLOSED`）：**内部错误已使用退出码 1；版本不兼容保持 5。
4. **TERP 字段布局和 verbose（P3）：**verbose 摘要已 `CLOSED`；字段重复因无当前漂移证据
   保持 `MONITORED`，未来字段变更必须先补跨语言向量。

### 需要实物处理、硬件验证或条件性返修的项目

以下将“已确定的接线/装配要求”和“只有测量后才能确认的返修”分开，不根据
空串口日志或软件构建直接宣称芯片/板卡损坏。

| 项目 | 类型 | 实物操作/通过标准 |
|---|---|---|
| U2 W25Q64 / SPI2 | **条件性返修，当前不能定性为硬件损坏** | 本地尚未跟踪的 `evidence/spi_w25q64_serial_20260805_213730.log` 只证明 30 秒收到 0 字节，不证明 Flash 坏，也尚不是可移植的仓库证据。返板后先用卖家隔离工程读 ID，同时测 3.3 V、PB12 CS、PB13 CLK、PB15 MOSI、PB14 MISO。只有在 MCU 侧时序/供电/片选正确但 MISO 仍无响应时，才返修焊点/连线或更换 U2。通过值是 `0xEF4017`。 |
| IMU 冲击实验连接 | **必须的装配修改** | 桌面联调可用短线，进入跌落/冲击前必须将 H743 与 ICM45686 焊接到统一载板或使用锁紧连接，并固定在同一刚性底板；杜邦线不能作为冲击验收连接。 |
| SHT4x / I2C1 | **必须的接线与冲突处理** | SCL/SDA 使用 PB8/PB9、仅 3.3 V；摄像头 FPC 不得同时连接。板上已有 4.7 kΩ 上拉，模块自带上拉时必须先核算，不得盲目并联。通过标准为唯一 `0x44`/`0x45` 地址应答、CRC-8 正确且连续 30 分钟无异常。 |
| RTC/LSE/VBAT | **硬件验证，失败时才返修** | 确认 32.768 kHz 起振、VBAT 电池/供电和后备域保持；断主电后 RTC 仍连续。若失败，先测晶体/负载电容/VBAT 路径后再决定补焊或更换器件。 |
| UART3 TERP | **外部接线与实链验收** | PD8（板 TX）接 3.3 V USB-TTL RX，PD9（板 RX）接 TX，必须共地；PB10 是 U3 QSPI CS，严禁改作 UART。验证 HELLO、无 FinSH 文本混入、TX 错误可见和 20 次物理断续下载。 |
| USB CDC | **板级功能尚未实现，不是已确认的 PCB 返修项** | 先确认 Type-C 到 PA11/PA12 的 D-/D+、VBUS 路径和 48 MHz 时钟，再完成 CDC BSP、合法 VID/PID、UID 序列号和 20 次枚举/插拔。只有导通、电平或时钟测量失败时才进入硬件返修。 |
| 真实掉电测试治具 | **需新增测试硬件** | 使用独立控制的负载开关/MOSFET/继电器切断板卡主电，不用 MCU 软件复位代替掉电。至少 100 次在不同写入阶段断电，已提交旧记录不损坏，未提交尾记录可丢弃/恢复。 |
| 1.6 kHz 吞吐和长稳 | **硬件在环门禁** | 同时运行 IMU FIFO/DMA、事件触发、U2 写入和 TERP 下载；记录 pool minimum、backpressure、lost samples、擦除最大延迟和任务栈高水位。这些数据决定是否需要调 SPI 分频、FIFO 水位、任务优先级或异步 storage writer。 |
| Phase 13 可靠性验收 | **硬件在环门禁** | 连续运行 72 小时；至少 100 次在记录写入及 OTA 关键阶段受控掉电；执行 USB 物理断连/恢复、OTA 回滚和 CAN bus-off 注入/恢复。每轮必须保存版本、条件、原始日志、统计和明确 PASS/FAIL，任一模拟结果都不能替代。 |
| 高 g / 倾斜或倒置 / 环境越界 / 开箱事件 | **功能与硬件联合门禁** | 先完成 B-07 的事件语义和软件状态机，再使用真实传感器、安装结构和代表性场景验证阈值、时间对齐、误报率、漏报率以及桌面端闭环；当前不得标记为完成。 |

### 2026-08-09 软件验证证据

- `pwsh scripts/run_tests.ps1`：协议生成物/golden 检查通过，27 个 native C 可执行测试通过；
  截至 `03de026` 的复核为 host `75 passed`、AI `1 passed`。
- ARM 固件 clean rebuild：项目自有源码 `-Wall -Wextra` 告警数为 0；ELF、冷启动向量和
  内存布局检查通过。尺寸为 `text=156668, data=1532, bss=179124`。
- 上述数字不是任何 SPI、IMU、RTC、I2C、UART、USB、真掉电或长稳的实板证据。

## 后续更新：Phase 08 UART3 软件闭环（2026-08-01）

本节只关闭本审查中属于 Phase 08 的软件问题；Phase 05--07 的 P1/P2 仍按原文保留，
不因本分支可提交而被误标为完成。

- 选择并启用独立 `USART3`：PD8 为 TX、PD9 为 RX、AF7、D2PCLK1；PB10 是新板 U3 QSPI
  Flash 的片选，明确不得作 UART。PA9/PA10 的 UART1/FinSH 保持不变。`app_runtime_start()` 已启动 `terp_uart3`，RX ISR 仅唤醒
  低优先级线程，线程才调用 TERP parser/service 和 storage service。
- 已关闭原 P1 “TERP 未接入 runtime/UART”：H743 构建中已链接 `terp_uart3.o`、
  `terp_service.o` 与 `terp_device.o`；运行时存在 RX、TX、1 秒帧间超时和明确的启动失败日志。
- 已关闭原 Phase 08 P2：设备强制 HELLO/session 并支持 reset；`storage_ready` 读取真实
  `EVENT_LOG_READY` 状态；TX 成功/失败被统计；CLI 握手失败关闭端口；主机本地超时进入
  `ERROR`；Python simulator 与 C 对齐 HELLO 错误；C parser 在损坏候选内重扫下一同步字节，
  C/Python 都有“合法帧嵌在坏 payload 中”的回归测试。
- 代码复杂度仍是中等且有边界：新 UART 适配器只做 RT-Thread 设备/线程生命周期，
  TERP 语义仍留在 `components/protocol` 与 `terp_service`；没有新增队列、通用传输接口或
  transport-to-Flash 依赖。解析器的有限重扫被限制为单层 replay，避免损坏输入诱发无界递归。
- Phase 08 仍有 P3：`--verbose` 没有逐帧摘要，消息字段布局未完全生成。它们不阻止该
  软件切片提交，但应在 GUI 或扩展协议前处理。
- USB CDC、板级 UART 连通、U2 实读、并发时序、断连恢复仍是硬件门禁；见
  `evidence/phase08/uart3-terp-hardware.md` 与 `evidence/phase08/usb-enumeration.md`。

## 全项目中期审查总览（2026-08-09 历史固定点，已被 V1 取代）

> 本节保留当时的中期审查结论，用于追溯问题如何关闭；它不是当前项目状态。当前 V1
> 结论以本文顶部和 `evidence/releases/v1.0.0/` 为准。

### 进度

- 已建立 BSP、ICM45686 采集、sample block pool、预触发/事件组装、EV01/EV02/EV03、EL01 SPI NOR 日志、Phase 07 服务模块、Phase 08 UART3 TERP 软件闭环和 Phase 09 桌面端确定性功能。
- native、host、AI smoke 和 H743 构建门禁能够运行；新增测试已进入统一 runner。
- 当时完成度主要是“软件模块和模拟链可验证”，不是“设备全部可用”；在该固定点，真实
  采集、Flash、RTC、SHT4x、IWDG、UART TERP 和 USB CDC 仍缺板级闭环。后续 V1 已关闭
  其中的真实采集、U2 事件日志和 UART3/TERP 核心闭环；RTC、SHT4x、物理 IWDG 和 USB CDC
  不属于 V1 核心功能声明。

### 架构与耦合

- 主数据链保持为：`BSP/driver -> acquisition -> sample_block_pool -> pretrigger/event -> EV01/EV02/EV03 -> event_export_sink_t -> storage_service -> EL01`；V1 当前写出 EV03。
- `event_export_sink_t` 的注入方向正确；event 不直接依赖 storage 实现。TERP 也只通过 `storage_service` 公共读取接口访问事件。
- 当前没有依赖环。不要为了“更纯”新增通用 contracts、额外 queue 或新的 record sink 抽象。
- health/acquisition/environment 的无锁快照风险已使用 mutex/临界区关闭；当前持续监控的耦合风险是协议字段在 C、Python、模拟器和生成脚本之间手工重复。

### 代码复杂度

- 整体复杂度属于中等、可控，不是全面失控；复杂度集中在少数状态机和存储函数。
- 既有热点包括 `event_log.c` 的记录扫描/恢复、`storage_service.c` 的命令分发、`health_service_evaluate()`、`terp_device.c` 和 host `client.py`；行数会随实现变化，不再把旧固定点的行数当作当前度量。
- `terp_device.c` 已按消息 handler 拆分，当前主要风险不是文件长度本身，而是握手状态、错误传播、跨语言一致性和测试遗漏。

### 规则遵守情况

- 工作树、分支边界、SCons 构建、native/host 测试、生成文件检查和软硬件状态分离总体遵守良好。
- 未修改未经批准的 pin、clock、DMA、Flash 地址、任务优先级和 USB 板级配置。
- 规则执行曾有一个关键偏差：测试和编译通过被当成阶段完成。当前台账已分开记录软件、真实 provider/字节通道与硬件验收；Phase 07 runtime、USB CDC 和板级门禁仍不得宣称完成。

### 2026-08-09 全项目问题状态（历史，曾取代 2026-08-01 快照）

1. Phase 05：真实触发 sample/block 语义和丢样证据链的软件问题已关闭；真实冲击标定、
   饱和及压力条件下的证据仍未完成。
2. Phase 06：JEDEC 白名单和不可绕过的 format 门禁已关闭；U2 实读、真实读写擦和掉电
   恢复仍未完成。
3. 数据路径余量仍未关闭：sample pool 约 150532 字节；最大事件可持有 100/128 个 block，
   同步擦除最多跨 26 个 4 KiB sector。是否增加异步 writer 必须由 1.6 kHz 实测决定。
4. Phase 07：快照一致性和 32 位 tick 回绕已关闭；provider/runtime/HAL/诊断接线仍为 B-01。
5. Phase 08：UART3 TERP 软件链已完成；UART 实链、USB CDC 适配和物理断连恢复仍未完成。
6. Phase 09：截图所列追加分页、annotation、JSON 和回放警告缺口已关闭；真设备大数据量和
   断连交互仍待系统验收。
7. 固件 OTA 的软件链、升级/降级与 Trial 冒烟已有证据；正式断电/救援矩阵和模型 OTA
   继续由 B-04 跟踪。CAN FD/ISO-TP 与 USB CDC 分别由 B-05、B-06 跟踪。
8. 高 g、倾斜/倒置、环境越界、开箱事件闭环及 Phase 13 可靠性验收由 B-07、B-08 跟踪。
9. AI Pilot 已合入并完成两类实板冒烟；冻结划分/产物追溯、实际模型 CRC、共同黄金向量、
   预测结果产品闭环和最终数据验收分别由 A-06、A-07、A-08、B-09、C-11 跟踪。
10. Release 配置、版本/设备身份、统一验证入口和干净 RC 由 A-09 跟踪。

下面的 Standards 与 Spec 章节给出上述问题的具体代码位置和最小修复方向。

## Standards（2026-08-01 历史发现，状态已更新）

以下保留原始审查证据；每项标题给出当前状态，正文中的路径和行为均指 2026-08-01 固定点。

### P2

1. **[软件已关闭，调度待硬件] 多字段快照并发撕裂（历史发现）。**
   - 历史证据：health、acquisition 和 environment 曾在无一致发布保护时跨任务复制多字段及 64 位时间。
   - 当前处理：health/environment 使用短 mutex，acquisition 使用短中断临界区；高频路径不等待慢 I/O。

2. **[软件已关闭，短写/断连待硬件] TERP 发送失败静默丢失（历史发现）。**
   - 历史证据：固定点曾忽略 `write()` 失败，设备侧没有可查询的 TX 错误。
   - 当前处理：UART3 runtime 已接线并保留 TX 错误计数；真实短写和物理断连仍由 B-06/C-07 验收。

3. **[软件已关闭] HELLO 失败泄漏串口（历史发现）。**
   - 历史证据：固定点在负责关闭的 `try/finally` 之前执行 `hello()`。
   - 当前处理：握手失败路径关闭 client，并有主机回归测试。

4. **[软件已关闭，物理断连待硬件] 请求超时状态不可信（历史发现）。**
   - 历史证据：本地截止时间到达后曾可能保留 `READY` 或 `HANDSHAKING`。
   - 当前处理：协议/CRC/传输错误关闭不可用 client 并进入 `ERROR`；只有用户取消可以回到 `READY`。

5. **[已知漂移已关闭，覆盖范围受监控] Python/C 错误语义漂移（历史发现）。**
   - 历史证据：模拟器曾把错误长度的 HELLO 也判为 `INCOMPATIBLE`，与 C 端 `MALFORMED` 不同。
   - 当前处理：已知错误向量已对齐；生成器仍不覆盖全部字段，继续按 A-04 的 `MONITORED` 约束处理。

### P3

1. **[软件已关闭] CLI 未知异常误报为“不兼容”（历史发现）。**
   - 当前处理：内部/文件系统错误使用退出码 1；专门的版本不兼容异常使用 5。

2. **[`MONITORED`] 消息登记表只生成 ID，字段布局仍需多处手工同步。**
   - `protocol/generate_messages.py:14-50` 只提取 `id/name`，忽略 `terp_messages.yaml` 中的 request/response 字段。
   - C device、Python client、Python simulator 和 golden 生成器仍各自手写布局。
   - 影响：后续改一个字段会形成 Shotgun Surgery，`--check` 也不一定发现漂移。
   - 最小修复：至少为每类消息生成完整跨语言向量和字段长度断言；暂不必引入复杂的通用序列化框架。

### Standards 中确认没有问题的部分

- `components/protocol`、`app/transport`、host protocol 和串口适配的职责方向清楚，未发现依赖环。
- TERP 通过 `storage_service_get_event_info()` / `storage_service_read_event()` 读取 EL01，没有让 USB/UART 或协议层直接解析 Flash 格式。
- 固件协议路径无动态分配，parser payload 有 4096 字节硬上限。
- 未修改 USB 引脚、时钟、DMA、任务优先级或 Flash 地址。
- 08 新增的 3 个 native 可执行文件已加入 `scripts/test_native.ps1`，生成代码和 golden `--check` 已进入统一门禁。
- 在该历史固定点，复杂度集中在 `terp_device.c` 和 `client.py`，但前者已按消息 handler 拆分；风险主要来自状态/错误路径，而不是函数数量本身。

## Spec

以下内容是 2026-08-01 固定点的历史审查证据。标题已标注当前状态；代码行号只用于追溯
当时发现，不得绕过文件顶部的当前状态表重新判定为未修复。

### P1

1. **[软件已关闭] Phase 07：触发样本边界（历史发现）。**
   - 历史证据：组装器曾把整个触发块算作 post，触发点前的同块样本最多误标 63 个。
   - 当前处理：事件保存块内 trigger index，导出从真实触发样本分割，并覆盖块首/中/尾测试。

2. **[软件已关闭，实读待硬件] Phase 07：JEDEC 探测（历史发现）。**
   - 历史证据：探测曾只检查容量字节并写死 8 MiB。
   - 当前处理：完整 `EF 40 17` 白名单和表项几何已实现；U2 实读仍为 B-02。

3. **[软件已关闭，真擦除待硬件] Phase 07：format 二次确认（历史发现）。**
   - 历史证据：公开 API 曾允许绕过 MSH 层的 10 秒确认直接格式化。
   - 当前处理：公开裸 format 已删除，只保留 request/confirm；真擦除和掉电仍待硬件。

4. **Phase 07：runtime/HAL/诊断接线仍不是已批准的软件闭环。**
   - `firmware/app/runtime/app_runtime.c:27-48` 只聚合 acquisition/event/storage 的少数字段；UTC、environment、reset/power、free space、pool minimum、backpressure 等保持零值。
   - `app_runtime.c:138-180` 没有初始化/poll time、environment、reset-power，也没有 I2C1/RTC ops、health/env worker 或 `health show`/`env show`/`time` 命令接线。
   - `health_service_watchdog_feed_allowed()` 只有库函数和单测，没有运行时调用者；物理 IWDG 保持未启用是允许的，但监督喂狗策略也尚未形成实际路径。
   - 影响：EV02 虽能编码快照，真实事件中的 07 上下文字段大多仍是默认值；单测验证的是孤立模块，不是运行设备组合。
   - 最小修复：按 Phase 07 设计的启动顺序接入 provider ops/worker/诊断命令，并增加 runtime composition 测试；物理 I2C/RTC/IWDG 验收仍单独保留为硬件门禁。

5. **[UART 软件已关闭，USB/实链未完成] Phase 08：TERP 字节通道（历史发现）。**
   - 历史证据：固定点只有协议库和 native test，没有 runtime RX/TX/timeout 驱动。
   - 当前处理：UART3 已形成软件闭环；UART 实链和 USB CDC 分别保留硬件/后续功能门禁。

### P2

1. **[软件已关闭，长稳待硬件] Phase 07：32 位 tick 回绕（历史发现）。**
   - 历史证据：固定点直接扩宽 32 位 tick，约 49.7 天回绕。
   - 当前处理：共享 64 位单调时钟已覆盖 wrap 测试；72 小时实板长稳仍由 B-08 验收。

2. **[软件已关闭] Phase 08：设备端 HELLO 会话（历史发现）。**
   - 历史证据：固定点允许未 HELLO 直接分发请求。
   - 当前处理：设备端强制 HELLO/session，断连/复位状态由相应测试覆盖。

3. **[软件已关闭] Phase 08：GET_HEALTH `storage_ready`（历史发现）。**
   - 历史证据：固定点曾从总体 health state 反推 storage ready。
   - 当前处理：现在读取 EL01/storage 的真实 ready 状态。

4. **[软件已关闭] Phase 08：C parser 最小丢弃恢复（历史发现）。**
   - 历史证据：固定点不会在已消费的损坏候选内寻找下一组 sync。
   - 当前处理：C/Python parser 已有嵌套合法帧的对称恢复测试。

### P3

1. **[verbose 已关闭，字段生成受监控] Phase 08：协议帧摘要（历史发现）。**
   - 历史证据：固定点没有 TX/RX type、sequence、flags 和 payload length 摘要。
   - 当前处理：client 仅发布不含 payload 的 `FrameSummary`；字段自动生成覆盖范围继续受监控。

## 已确认关闭的旧 07 项

这些不再列入未解决问题：

- Phase 07 的 5 个 native 可执行文件现已全部进入 `scripts/test_native.ps1`。
- idle storage 不再被无条件判 stale；provider loss、持续 backpressure、power droop 和 watchdog permission 已有聚焦单测。
- runtime 已把 health 实例注入 event service，触发接受时的 EV02 快照只冻结一次。
- EL01 对 EV01/EV02 的版本、长度、事件 ID 和内部 CRC 校验仍保留。

## 2026-08-01 历史验证证据

- `main`：`pwsh scripts/run_tests.ps1` -> Native C PASS，host `9 passed`，AI `1 passed`。
- `main`：加载 `scripts/project_env.ps1` 后执行 SCons H743 build -> exit 0。
- Phase 08：`pwsh scripts/run_tests.ps1` -> 生成文件/golden check PASS，Native C（含 3 个 TERP 测试）PASS，host `23 passed`，AI `1 passed`。
- Phase 08：加载 `scripts/project_env.ps1` 后执行 SCons H743 build -> exit 0。
- 上述证据只能说明现有软件门禁通过；不能反证本文件列出的共享盲区，也不能代替硬件验收。

## 2026-08-17 硬件门禁审查快照（历史）

- U2 JEDEC 与受控测试区擦写/掉电保持已有精选证据，B-02 已关闭；不得据此扩大为整机写入窗口掉电、
  1.6 kHz 并发余量或 72 小时长稳通过。
- Phase 07：真实 runtime 仍未接入 I2C1/SHT4x、RTC/LSE/VBAT、PVD/IWDG，相关降级和恢复矩阵
  继续由 B-01/B-08 跟踪。
- Phase 08：UART3 TERP 已有实链与同一 handle 重连证据；USB CDC、正式物理拔插次数和下载续传矩阵
  仍未关闭。
- AI：Pilot HIL 只证明两条受控台架事件的接线和推理冒烟；PC/MCU 共同黄金向量、并发资源压力、
  预测持久化和真实箱内模型验收仍未完成。

## 2026-08-17 建议修复顺序（历史）

以下顺序保留为审计轨迹，其中自动触发、四分类真实 AI、B-09 闭环、模型 OTA 和 RC 冻结已经
在后续 V1 工作中完成；不得把这份旧顺序重新解释成当前待办。

1. 在正式重新训练前关闭 A-06、A-07、A-08：冻结 split、统一产物追溯、修复实际模型完整性校验、
   建立 Python/C 共同黄金向量与报告生成入口。
2. 完成 B-09 的预测结果事件/TERP/上位机闭环，同时保持“AI 失败不影响原始事件落盘”。
3. 用重新采集的真实数据关闭 C-04 的自动触发标定和 C-11 的最终模型验收；Pilot 指标不作为门槛。
4. 完成 B-01 的 Phase 07 真实 provider/runtime/HAL/诊断接线；若最终缩减范围，必须以批准的范围决策
   更新需求和台账，不能静默遗漏。
5. 在 B-04 中补齐模型 OTA，并按批准范围完成固件 OTA 断电/救援门禁；B-05 CAN 与 B-06 USB
   若不进入本次 Release，也必须明确记录为非核心未实现项。
6. 按当时 B-08 计划执行该轮 `main` 的并发压力、长稳、掉电和物理断连验收，再关闭 A-09、冻结干净 RC。
7. 用 1.6 kHz 实测数据决定 C-02 是否需要异步 Flash writer，不凭感觉增加队列和任务。
