# Reliability Evidence 统一可靠性证据系统规格

## 状态与适用范围

本文是 V1 兼容旁路升级的可实施规格。它冻结可靠性证据的语义、持久化边界、异常捕获
边界、兼容协议和验收门禁；实施计划只补齐低风险的符号、常量、字节偏移和测试编排。

当前 V1 发布物仍是唯一产品基线。可靠性证据能力必须以旁路方式增加，不得替换已经发布
的 V1 二进制、事件记录、协议或 OTA 资产。实板门禁完成前，不得把可靠性候选版本标记为
V1 替代品。

2026-08-27 实板勘误：目标板在 `SYSRESETREQ` 后不能保留 `0x38800000` Backup SRAM，
而未被 V1 使用的 D3 SRAM4 尾部 `0x3800FE00..0x3800FFFF` 已通过同一 software reset
保留测试。因此 CrashRecord 的 512B `NOLOAD` A/B 区改用该固定 D3 SRAM4 范围。该勘误
不改变记录格式、协议、Bootloader/Flash/QSPI/U2 布局或默认关闭行为，也不扩大为掉电保留
承诺；watchdog 和其余目标 reset 仍须通过 Phase 4 实板门禁。

## V1 不可破坏契约

以下内容在本规格范围内冻结：

- `v1.0.0` tag 及其发布产物保持不变；发布记录中的 `v1.0.0` 提交是回退基线。
- EV03 事件字节格式、EV03 样本编码、EV03 头部语义和 EL01 事务格式保持不变；不新增
  EV04，不改变已有 EL01 recovery、CRC、commit 和 append-only 规则。
- U2 SPI2 W25Q64 事件日志所有权和布局保持不变。事件日志仍使用既有 EV03/EL01 数据区，
  不混入新的记录类型。
- 当前 QSPI 8 MiB 布局保持不变：metadata、candidate、recovery、model A、model B
  的范围和所有权不变。metadata 内的 model state、AI result sidecar、HIL scratch 也不
  改作 CrashRecord 区域。
- 当前内部 Flash 的 Bootloader、Application、state primary、state secondary 区域保持
  不变；不从 OTA state 或 Application 已占用范围偷取 CrashRecord 空间。
- 现有 TERP v1 message ID、帧版本、既有 request/response payload、长度、字段顺序和错误
  语义保持不变。特别是既有事件下载和 `GET_AI_RESULT` 48B response 不扩展、不重排。
- AI result sidecar v2 的 record 格式、校验和旧记录读取行为保持不变。
- Bootloader、OTA candidate/recovery、model A/B 和既有升级状态机保持不变。
- 新能力只通过新增 capability 和新增 message 协商。旧 TERP 客户端在不认识新 capability
  和新命令时，仍能完成原有 HELLO、健康查询、事件列表、事件下载和既有 AI 查询。

冻结依据为 [V1 release notes](../../v1.0.0-release-notes.md)、[EV03/EL01 storage
contract](../../storage/event_log_v1.md)、[memory layout](../../../config/memory_layout.yaml)、
[TERP v1 contract](../../protocol/terp_v1.md)、[TERP message registry](../../../protocol/terp_messages.yaml)
和 [AI sidecar implementation contract](../../../firmware/app/ai_inference/ai_result_sidecar.h)。

可序列化的 `DEGRADED`/`INVALID` EV03 仍是合法 EV03，因此可能出现在旧客户端的
`LIST_EVENTS` 响应中。没有 reliability capability 的旧 Host 必须能够下载、校验和解析这类
EV03，不得崩溃；它只能显示原始 event flags/丢样信息和“无 AI result”，不得把记录显示为
`PASS`。新 capability 只增加解释能力，不改变旧客户端对既有事件下载的工作方式。

## 受控启用与回退契约

新增构建开关固定为 `TRANSPORT_RELIABILITY_EVIDENCE_ENABLED`，控制本规格的旁路能力。

- Phase 0–3 期间所有普通 V1 构建默认关闭；只有隔离的测试或 FaultInjection profile
  可以显式打开，且不得作为发布物。
- 开关关闭时，事件入口保持原 V1 准入行为，现有 TERP capability/message 和 payload 保持
  不变，现有 RT-Thread fault entry 保持不变，Reliability Evidence/CrashRecord 捕获和
  FaultInjection 入口均不可用。
- Phase 4 只有在全部软件门禁和实板门禁通过后，发布候选才允许打开该开关；候选失败必须
  回退到开关关闭的冻结 V1 行为和已发布 V1 tag。
- `CAPABILITY_CRASH_RECORD_V1` 只有在 reset-retained SRAM backend 初始化和固定自检成功后才广告。
  初始化或自检失败时不广告该 capability，相关 TERP 操作返回 unsupported/not available，
  且不得阻塞或改变现有 V1 启动、事件、OTA 和协议行为。

## 目标

- 为每条能够安全序列化的事件生成确定的 `PASS / DEGRADED / INVALID` 结论和原因集合。
- `PASS` 是第一版唯一允许进入 AI 的事件质量；非 `PASS` 不产生正常预测。
- 保留可序列化的降级/无效原始事件，使质量结论能够追溯到 EV03/EL01 事实。
- 在 HardFault、MemManage、BusFault、UsageFault 中捕获最小可校验 CrashRecord，并在
  MCU reset 后由 Application 恢复。
- 通过统一 Reliability Evidence facade 提供事件质量、AI 决策和 CrashRecord 查询。
- 用自动化测试、独立 FaultInjection profile 和实板门禁形成可审查证据。

## 非目标与硬边界

- 不重构或替换 EL01、EV03、OTA、Bootloader、TERP v1 和现有 Flash layout。
- 不在 fault context 调用 RT-Thread API、调度器、mutex、heap、文件系统、printf、普通
  日志服务、TERP/UART、AI、复杂栈回溯、QSPI 操作或内部 Flash 擦除/编程。
- 不在 fault context 发送数据或等待外设；捕获完成后只能执行受控 reset，handler 不得
  返回到故障指令。
- CrashRecord 第一版只承诺目标已验证 SRAM 区域的 reset-retention。完整断电、brownout 后保留和
  跨电源域恢复不属于第一版承诺。
- CRC 只用于检测意外损坏、半写和边界错误；SHA-256 只用于 Host 证据清单或产物指纹，
  不代表身份认证、防重放或防篡改。
- 事件质量不是训练集外检测；模型置信度继续使用既有 AI 结果语义，不改变事件 verdict。
- Assert 和 watchdog 继续由既有诊断/复位事实覆盖，不作为第一版处理器 Fault Capture
  的强制入口。

## 术语与边界

### 两个连续的事件入口

事件入口必须按以下顺序分为两个独立模块：

```text
Event blocks/facts
        |
        v
serialization_gate
        |-- CAPTURE_FAILURE: 不生成 EV03，不进入 AI
        v
event_quality_policy_v1
        |-- PASS / DEGRADED / INVALID
        v
EV03 -> EL01 -> Reliability Evidence view
                    |
                    v
              AI Quality Gate
```

`serialization_gate` 只回答“是否能安全形成合法 EV03 payload”。它必须检查块引用、边界、
样本计数、长度运算、序列字段和 EV03 所需的编码前提。空块、空指针、越界、整数溢出、
无法形成完整 payload 等情况输出 `CAPTURE_FAILURE`，不得用零填充或伪造 EV03。
块指针必须先由当前 sample pool 验证所有权和有效引用，才能解引用。
`trigger_sequence` 和 `trigger_monotonic_us` 必须与真实触发块内的
`trigger_sample_index` 对应，含既有 uint32 sequence 回绕和 uint64 时间饱和语义。
由于 EV03 不保存块边界，任一块不是既有固定的 32 samples 时，不能在重启后
无歧义重建质量事实，因此必须在此输出 `CAPTURE_FAILURE`，不得写入。

`event_quality_policy_v1` 只接收 `SERIALIZABLE` 事件，并使用同一组版本化规则在事件提交
前后计算 verdict。EL01 commit 失败不属于某个已提交事件的质量 verdict，而是独立的
`STORAGE_FAILURE`；它不得被描述为同一介质上已经持久化的失败证据。

## 事件质量策略 v1

### 判定规则

对通过 serialization gate 的事件，规则固定如下：

| Verdict | 必须满足的条件 | 原因集合 | AI 决策 |
|---|---|---|---|
| `PASS` | 固定 2400 samples；25 pre + 50 post 完整；序号连续；无丢样 | `NONE` | 允许进入既有 AI |
| `DEGRADED` | 结构可序列化、序号连续，且存在 `PRETRIGGER_SHORT` 或 `DURATION_CAPPED` | 对应降级原因 | 第一版不进入 AI |
| `INVALID` | 结构可序列化，但存在 `DATA_LOSS`、序号缺口或其他完整性错误 | `DATA_LOSS`、`SEQUENCE_GAP`、`FIXED_SHAPE_VIOLATION` 等 | 不进入 AI |

优先级固定为：`INVALID > DEGRADED > PASS`。例如同时存在前触发不足和序号缺口时，结果
必须是 `INVALID`。可序列化但既不满足 PASS、也没有明确 DEGRADED 原因的固定窗口偏差，
使用 `FIXED_SHAPE_VIOLATION` 归入 `INVALID`，保证策略对所有输入都有确定输出。

以下情况不进入 event_quality_policy_v1：

- 空块、坏边界、缺少形成 EV03 所需 payload 的结构损坏：`CAPTURE_FAILURE`，不生成 EV03。
- EL01 begin/write/commit/CRC 失败：`STORAGE_FAILURE`，不得声称该事件已在 U2 上持久化。
- TERP 下载中断：不改变设备上的事件 verdict；由 Host 按既有 partial-file/CRC 规则处理。

### 原始事实与结论

事件质量计算至少保留以下可归属于单条事件的事实：固定窗口要求、实际样本数、25/50
块计数、序号连续性、EV03 丢样标志/摘要、`PRETRIGGER_SHORT`、`DURATION_CAPPED`、
serialization 状态和 EL01 commit/readback 状态。队列深度、块池低水位和全局错误计数只有
在能明确归属于本事件时才能成为 reason；否则只留在既有 health/diagnostic 统计中。

Reliability Evidence endpoint 从已提交 EV03/EL01 原始事实重新计算 `event_quality_policy_v1`
并返回 verdict，避免新增事件物理格式。Host 不得用自己的启发式规则覆盖设备 verdict。

### 提交前后 round-trip 不变量

事件提交前必须冻结一次 live facts，并由同一份 canonical EV03 payload 完成编码和解码。
规范化后的 EV03 facts 必须与 live facts 在以下字段上逐项等价：样本数、25/50 块计数、
序号连续性、丢样标志/摘要、`PRETRIGGER_SHORT`、`DURATION_CAPPED` 和所有参与策略的
完整性事实。verdict 和 reason_flags 必须由这份等价事实得到。

EV03 v3 不新增块计数字段。在既有每块 32 samples 的固定编码前提下，
readback 的块事实按以下唯一规则规范化：

- `trigger_sample_index = pretrigger_samples % 32`；
- `pretrigger_block_count = pretrigger_samples / 32` 向下取整；
- `posttrigger_block_count = (posttrigger_samples + trigger_sample_index) / 32`，且分子
  必须能被 32 整除。

上述规则只对已通过 gate 的 32-sample 块使用。如果整除关系不成立，或规范化结果
与 live 块计数不等价，提交后结果必须为 `INVALID`，原因包含
`FIXED_SHAPE_VIOLATION` 和 `EVIDENCE_ROUND_TRIP_MISMATCH`。全部块均为 32 samples，
但 25/50 计数、旗标或固定窗口不一致的事件仍可安全序列化，并以
`FIXED_SHAPE_VIOLATION` 归为 `INVALID`。该推导不改变 EV03 字节布局。

EV03 v3 的 `DATA_LOSS` 与 loss summary 继续使用冻结的双向不变量：无缺失时
标志和六个 summary 字段全为零；有缺失时标志必须置位，且 summary 必须自洽。
flag-only `DATA_LOSS`、summary-only loss 或跨 episode 时间倒退都不是合法 EV03 编码
前提，必须在触碰 sink 前输出 `CAPTURE_FAILURE`。

EL01 commit 后的 readback 必须再次解码 EV03，并与提交前 live facts round-trip 等价。若
不等价，事件证据状态为 `INVALID`，原因加入 `EVIDENCE_ROUND_TRIP_MISMATCH`，查询端不得
返回 `PASS`；关联 AI 结果也不得作为本事件的正常预测发布。该检查不能依赖全局计数或重新
采集的状态快照。

AI gate 和 Reliability Evidence query 必须使用同一个 `event_quality_policy_v1` 和同一组
规范化字段。live verdict 与 readback verdict 不一致是自动化测试失败和实板发布阻断条件。

## 可序列化事件的持久化

- `DEGRADED` 和 `INVALID` 只要通过 serialization gate，继续使用原 EV03 payload 和原
  EL01 transaction 保存；不新增 EV04，不改变事件样本字节。
- EV03 的已有事件 flags、序列信息和丢样摘要是事件质量的原始证据。Reliability Evidence
  作为逻辑旁路视图读取这些字段，不要求新增 QSPI 物理分区。
- EL01 commit 成功后，设备可对该事件提供 evidence view。EL01 commit 失败只产生
  `STORAGE_FAILURE`，不生成一个“已持久化的失败事件”或伪造质量记录。
- 无法安全序列化的事件没有合法 event ID 对应的 EV03 记录；只允许进入现有运行时诊断/故障
  统计，不得在 TERP 中伪造一个可下载的事件。
- Host 下载的原始事件仍使用既有 `READ_EVENT_CHUNK` 和文件完整性流程；Host 的 MATCH
  结果不写回设备事件。

## AI Quality Gate 与 sidecar v2

### 固定决策

- 只有 `PASS` 事件可以调用现有 AI submit path。
- `DEGRADED`、`INVALID` 和 `CAPTURE_FAILURE` 都不产生正常预测。
- 非质量原因导致的 AI 不可用、资源不足、feature/runtime 错误继续使用现有 AI result
  status/failure reason，不把它们重命名成事件质量 verdict。
- 既有 AI sidecar v2 record 和旧记录 mount/read 必须保持不变。
- 既有 `GET_AI_RESULT` 仍为 48B 原 payload；不加入 `DATA_INVALID` 新 status，不把已有
  `RESOURCE_LIMIT` 借作事件无效语义。

Reliability Evidence 的 `ai_decision` 逻辑值固定为：

- `NOT_RUN_QUALITY`：事件 verdict 非 `PASS`。
- `ELIGIBLE_NO_RESULT`：事件 verdict 为 `PASS`，且没有同一 `event_id` 的现有 sidecar
  记录；该状态不推断 AI 是否曾经运行。
- `RESULT_PRESENT`：事件 verdict 为 `PASS`，且存在同一 `event_id` 的现有 sidecar 记录。

`ai_decision` 完全由 event verdict 和同一 `event_id` 的 sidecar 记录是否存在推导。若
`RESULT_PRESENT`，endpoint 另返回该既有记录的 `ai_result_status` 和
`ai_failure_reason`；若没有记录，两字段为无结果状态。endpoint 不声称 AI 曾经真正运行，
也不把重启后的缺失记录解释为 runtime failure。现有模型 confidence、unknown 和 failure
语义保持不变。非 `PASS` 事件即使查询到其他事件的最新 AI 结果，也不得把它关联为本事件
的正常预测。

## CrashRecord v1

### 载体和持久性承诺

- CrashRecord 固定存放在 D3 SRAM4 尾部 `0x3800FE00..0x3800FFFF` 的 A/B 两个固定槽。
- 不使用 QSPI、内部 Flash、Bootloader state、OTA candidate/recovery、model A/B、HIL
  scratch 或 AI result sidecar 区域。
- 该 SRAM4 区域只承诺跨目标已验证的 MCU reset 保留；不承诺完整断电、brownout、备用电源
  失效或电源域复位后的保留。
- Application 在正常启动早期校验并登记 CrashRecord；Bootloader 不读取、不清除、不改写
  CrashRecord。

### 固定记录字段

每个槽包含一个固定长度记录和一个独立的 acknowledgement marker。记录字段按以下逻辑
定义，具体 C 对齐由当前 H743 linker 实现并以静态断言锁定：

| 字段 | 语义 |
|---|---|
| `magic` | 固定记录标识；错误值表示槽无效 |
| `format_version` | CrashRecord v1 版本；未知版本拒绝解析 |
| `header_length` | 头部长度；必须在固定槽边界内 |
| `record_length` | 整条记录长度；必须在固定槽边界内 |
| `sequence` | 非零单调记录序号；用于 A/B 选择和 ACK 关联 |
| `fault_kind` | `HARDFAULT`、`MEMMANAGE`、`BUSFAULT` 或 `USAGEFAULT` |
| `capture_flags` | 栈来源、FPU 扩展帧存在性、寄存器可读性等捕获状态 |
| `exc_return` | 异常返回值，标识 MSP/PSP 和异常栈形态 |
| `sp` | Fault handler 获得的活动栈指针 |
| `r0..r3`、`r12`、`lr`、`pc`、`xpsr` | Cortex-M 自动入栈基本帧 |
| `cfsr`、`hfsr`、`shcsr`、`mmfar`、`bfar` | SCB 故障状态寄存器快照 |
| `reset_flags` | Fault 发生时可安全读取的复位标志快照 |
| `build_id` | 固定长度构建标识，用于 Host 选择 ELF/MAP |
| `crc32` | 覆盖固定记录内容但不覆盖 commit marker 和 ACK marker |
| `commit_marker` | 最后写入的提交字；不是预期值表示半写/无效 |

第一版只保存基本自动入栈帧；`exc_return` 和 `capture_flags` 必须明确标识 FPU 扩展帧，
不在 fault context 做扩展 FPU 栈解析。`sequence=0`、长度越界、未知版本、CRC 错误、
commit 缺失的记录一律无效。

### 写入、选择和恢复

1. 在任何写入前，以固定边界校验两槽的 `magic`、version、length、CRC 和 commit；从有效
   记录中按 wrap-safe 比较选出 `newest_valid`。新序号固定为
   `sequence = newest_valid + 1`；没有有效记录时从 `1` 开始；加一后跳过 `0`。
2. 正常目标槽不能是当前最新槽。优先选择无效槽，其次选择已 ACK 的最旧槽，再其次选择
   序号最旧槽。只有在另一个槽已经验证为有效备份且明确的写入策略允许时，才允许覆盖当前
   最新槽；V1 默认策略不启用该例外。
3. 对目标槽执行不可逆顺序的提交事务：先写无效 `commit_marker`，再把该槽的
   `ack_marker` 重置为 `ACK_UNACKED`，然后写入新字段和 CRC；执行 `DMB`/`DSB` 等目标平台
   要求的写入屏障；最后才写有效 `commit_marker`。旧 commit 绝不能在新字段写入期间保持
   有效，避免 reset-retention 后把旧记录误认成新记录。目标 H743 的 SRAM4 采用 cacheable
   映射，因此有效 commit 写入后还必须对整条 128B 记录执行 32-bit 写后读回，并按 32B
   cache line clean-to-PoC，再执行 `DSB`，保证 reset 前数据已到达物理 SRAM。
4. 两槽均有效时，使用 32-bit wrap-safe 序号比较；非零序号差值在半范围内时，较新者为
   newest，超过半范围者为较旧者，正好半范围属于不可判定并使该选择失败。序号相等时
   固定选择 A 槽，保证结果确定。
5. 启动时只向 Reliability Evidence 暴露最新且通过全部校验的记录。两槽都无效或序号比较
   不可判定时报告无有效 CrashRecord，不尝试猜测现场。
6. 新故障可能覆盖较旧、未 ACK 的记录；第一版保证最新记录优先，不保证无限历史。

### ACK 语义

- `ACK_CRASH_RECORD` 必须携带完整 `sequence`，只接受当前可见记录的精确序号。
- ACK 是幂等的；重复 ACK 返回已确认，不清除、不修改、不重写 CrashRecord 内容。
- ACK 只把对应槽的独立 `ack_marker` 写为该记录的 sequence，不改动 CRC 覆盖区和 commit
  marker；新写事务开始时必须先将其重置为 `ACK_UNACKED`。
- 序号不匹配返回 stale/not-found；不得因为错误序号清除任何槽。
- 第一版没有通过 TERP/MSH 删除或擦除 CrashRecord 的操作。

### Fault context 规则

Fault 入口只允许：取得异常栈指针、读取自动入栈基本帧和 SCB 状态寄存器、执行固定边界
检查、写入固定 reset-retained SRAM 槽、写入 commit marker、请求系统 reset。禁止调用 RT-Thread
函数、调度器、mutex、heap、printf、字符串格式化、文件/Flash/QSPI 驱动、UART/TERP、
AI、动态栈回溯或任何阻塞操作。

捕获完成后必须通过受控 reset 离开故障态；handler 不得返回到原故障 PC。若 reset 请求
未生效，进入无调用的不可返回循环。Fault handler 必须覆盖 H743 当前启用的 HardFault、
MemManage、BusFault、UsageFault 入口，并将来源写入 `fault_kind`；不得依赖所有 fault
自动升级为 HardFault 后再猜测来源。

## Reliability Evidence Service

该服务是只读查询门面加受保护 ACK 操作，不是新存储引擎。它只组合既有 EL01/EV03、AI
sidecar v2 和 reset-retained SRAM CrashRecord 的结果。

### 事件证据

`GET_EVENT_EVIDENCE(event_id)` 只读取已提交事件，返回以下逻辑字段：

- `event_id`；
- `evidence_version = 1`；
- `verdict`：`PASS`、`DEGRADED` 或 `INVALID`；
- `reason_flags`：版本化的原因集合；
- `ai_decision`：`NOT_RUN_QUALITY`、`ELIGIBLE_NO_RESULT` 或 `RESULT_PRESENT`；
- `storage_state`：EL01 readback/CRC 结果；
- 同一 `event_id` 的既有 AI result sequence、status 和 failure reason（无结果时为无结果状态）。

`CAPTURE_FAILURE` 和未提交的 `STORAGE_FAILURE` 不伪装成已存在的 event evidence；它们只能
通过既有诊断/health 语义报告。

### CrashRecord 证据

`GET_CRASH_RECORD` 按 CrashRecord `sequence` 读取当前最新有效记录。请求和响应采用固定
的 sequence、offset、total length、actual length、chunk CRC 和 data 逻辑字段；单次 data
不得超过既有 TERP 最大 chunk 4000B。读取始终是只读操作。

## TERP 与 MSH 兼容旁路

### 新增操作

新增以下 TERP 操作，并只在新的 reliability capability 协商成功后使用：

- `GET_EVENT_EVIDENCE`
- `GET_CRASH_RECORD`
- `ACK_CRASH_RECORD`

新操作使用当前 registry 中未占用的 message ID 和一个新的 reliability capability bit。
具体数值由实施计划从未占用空间分配，并在协议 golden vector 和 registry 中一次性冻结；
不得复用既有 ID。新操作的 logical payload、长度、错误码、handshake 要求和 4000B chunk
上限必须在实现前写入协议定义。

旧 TERP message、既有 payload 和既有 response 长度完全不变。旧客户端不发送新命令时，
仍按 V1 流程运行；不认识新 capability 的客户端不得被要求解析新字段。新客户端连接旧
设备时，未协商 capability 的 reliability 操作返回现有结构化 unsupported/error，不影响
既有事件下载。

MSH 只提供事件证据读取、CrashRecord 读取和带完整 sequence 的 CrashRecord ACK；没有无序
ACK、clear、delete 或物理擦除命令。MSH 只读操作不能修改 EV03/EL01、AI sidecar 或 CrashRecord。

## FaultInjection profile

Fault injection 只存在于独立的 `FaultInjection` 构建 profile 和对应测试目标：

- native 使用替身注入序列缺口、队列/块池压力、EV03 serialization failure、EL01 failure
  和 CrashRecord torn-write；
- 实板 profile 提供显式确认的事件故障和四类 processor fault 注入；
- 正常 Debug/Release 产品路径不暴露注入命令；
- Release 构建必须通过符号、字符串、注册表和可达性检查，证明 FaultInjection 入口不在
  产物中；
- 注入不得改变任何已有 V1 message、payload、EV03/EL01 字节或 OTA/model 资产。

## 分阶段实施与门禁

### Phase 0：防回退门禁

软件验收：

- 固化 V1 tag/产物 hash、EV03/EL01 golden bytes、AI sidecar v2 golden records、TERP
  v1 golden vectors 和当前内存布局校验。
- 建立有效 V1 事件、旧 TERP 客户端、OTA/model package 的基线测试。
- 固化 `TRANSPORT_RELIABILITY_EVIDENCE_ENABLED=0` 时的原 V1 事件准入、TERP capability
  和 fault entry 行为；该状态下现有无效事件丢弃语义保持不变。
- 用合法但带 `PRETRIGGER_SHORT`/`DATA_LOSS` 的 EV03 fixture 验证旧 Host 能列出、下载和
  解析记录，不崩溃且不把原始 flags 解释为 `PASS`。
- 证明新能力以旁路方式加入，既有接口没有字段、长度和地址变化。

硬件状态：不新增硬件 PASS；只记录现有 V1 实板证据和未覆盖项目，native 不能替代实板。

回退条件：任一既有字节 golden、旧客户端、OTA/model、内存布局或 V1 构建门禁失败时，
停止后续阶段，可靠性改动回退到 V1 基线。

### Phase 1：事件质量闭环

软件验收：

- 实现 serialization_gate 和 event_quality_policy_v1，覆盖本规格的完整判定表及优先级。
- 可序列化 DEGRADED/INVALID 继续写原 EV03/EL01；不生成 EV04。
- 空块/坏边界输出 capture failure，不写伪造 EV03。
- EL01 failure 只报告 storage failure，不生成同介质持久化声明。
- 提交前 live facts、canonical EV03 decode facts 和 EL01 readback facts round-trip 等价；
  不等价时返回 `INVALID` + `EVIDENCE_ROUND_TRIP_MISMATCH`，不得查询为 `PASS`。
- 只有 PASS 调用 AI；非 PASS 没有正常预测；AI sidecar v2 和 GET_AI_RESULT 48B 不变。
- 既有有效 2400-sample 事件的事件编号、样本字节、EL01 CRC/recovery 和 AI 结果不回退。
- 旧 Host 对新产生的合法 DEGRADED/INVALID EV03 仍能从 `LIST_EVENTS` 看到、下载和解析；
  没有 reliability capability 时只显示原始 flags/丢样信息和无 AI result，不显示 `PASS`。

硬件状态：事件质量逻辑可先由 native/fake storage 验证，但这些结果不是硬件 PASS；实板
IMU/DMA 丢样和资源压力验证留到 Phase 4。

回退条件：有效事件字节、EL01 提交/recovery、AI sidecar、既有 AI 结果或旧导出发生回退，
立即禁用质量旁路并保留 V1 事件路径；若非 PASS 不能安全保存，则不得发布该能力。

### Phase 2：CrashRecord native + linker

软件验收：

- 增加 D3 SRAM4 尾部固定 A/B 槽符号/段、固定记录结构和静态大小/边界断言。
- 通过 native 测试覆盖字段、CRC、commit-last、未知版本、半写、A/B 选择、序号 wrap-safe
  规则和幂等 ACK。
- reset-retained SRAM backend 初始化和固定自检失败时不广告 Crash capability，且现有 V1 启动继续。
- 通过编译/静态检查证明 fault context 没有 RT-Thread、日志、存储驱动、TERP 或阻塞调用。
- Fault handler 捕获后受控 reset 且不可返回；Application 早期恢复只读取有效最新记录。

硬件状态：Backup SRAM 已在目标板 software reset 下证实不保留并被拒用；D3 SRAM4 候选
范围已通过单字 software-reset 保留探测，完整 CrashRecord、fault vector、栈帧、受控 reset
和 watchdog reset 仍由 Phase 4 验证；完整断电持久性不在本阶段状态声明中。

回退条件：linker 改动影响 Application/stack/DMA、Fault handler 二次 fault、reset 不可控、
固定 SRAM4 区不能跨目标 reset 保留，或现有启动流程被破坏时，禁用 CrashRecord，并保留
Phase 1 事件质量能力。

### Phase 3：TERP / Host

软件验收：

- 注册新 capability 和三个新操作，冻结未占用 ID、logical payload、错误码和 golden vectors。
- 新 endpoint 能读取事件 verdict/reason/ai_decision、CrashRecord raw bytes 并按 sequence ACK。
- 旧 TERP 客户端的 HELLO、HEALTH、LIST、GET_EVENT_INFO、READ_EVENT_CHUNK、GET_AI_RESULT
  测试和断线续传测试不变且通过。
- 旧 Host 对 `LIST_EVENTS` 中出现的合法 DEGRADED/INVALID EV03 能稳定下载和解析，只显示
  原始 flags/丢样信息和无 AI result，不能显示为 `PASS`。
- MSH 只读查询和带 sequence ACK 通过；无 clear/delete/erase 路径。

硬件状态：native、Host loopback 和协议 golden 不是 UART3/真实设备硬件 PASS；真实串口
重连和旧客户端兼容留到 Phase 4。

回退条件：任何旧客户端 payload 解析、HELLO、事件下载、AI 查询或 OTA/model 协议受影响，
撤掉新 capability/新命令，设备恢复为原 TERP v1 行为。

### Phase 4：实板故障注入与发布候选

软件验收：

- Release 产物通过 FaultInjection 入口不存在/不可达检查。
- 完成事件故障、四类 processor fault、CrashRecord 恢复、TERP 导出/ACK 和证据包。
- 完成 ROM、RAM、实时路径、启动恢复和 watchdog 余量报告。
- 仅在所有 V1 非回归、reset-retained SRAM backend 自检和实板门禁通过后，以
  `TRANSPORT_RELIABILITY_EVIDENCE_ENABLED=1` 生成发布候选；发布候选失败时回退到冻结的
  V1 tag 和开关关闭行为。

硬件状态：必须在目标 STM32H743 板上完成 fault vector、D3 SRAM4 reset-retention、
IMU/FIFO/DMA 故障、U2 event log、UART3/TERP 和真实复位流程验证。native 结果不得写成
硬件 PASS。

回退条件：任何错误 PASS、非 PASS 产生正常预测、CrashRecord 校验/恢复失败、旧 TERP
客户端回退、介质/OTA/model 破坏、二次 fault 或实板时序超限，均拒绝发布候选并保留 V1。

## V1 自动化非回归矩阵

| 范围 | 自动化门槛 | 通过含义 |
|---|---|---|
| 发布基线 | V1 tag/产物 hash、构建 identity、`git diff --check`、`TRANSPORT_RELIABILITY_EVIDENCE_ENABLED=0` 行为快照 | 可靠性改动未替换 V1 基线 |
| 内存布局 | `memory_layout.yaml` 解析、区域边界/重叠/容量检查 | U2/QSPI/内部 Flash 所有权未变化 |
| EV03/EL01 | 有效和可序列化非 PASS 的 EV03 golden bytes、EL01 header/footer/commit/CRC、recovery/torn-write、live facts→EV03 decode→readback facts round-trip | 既有字节格式和原子提交未变化，提交前后事实与 verdict 等价 |
| 事件 gate | PASS、PRETRIGGER_SHORT、DURATION_CAPPED、DATA_LOSS、sequence gap、空块/坏边界、EL01 failure、`EVIDENCE_ROUND_TRIP_MISMATCH` 全表 | verdict、capture failure、storage failure 边界确定且无伪造记录 |
| AI | PASS submit；非 PASS no submit/no normal prediction；AI sidecar v2 旧记录回放 | 质量 gate 不破坏既有 AI 结果格式 |
| TERP | registry/golden vectors、新 capability/new operations、旧 48B AI response、旧客户端断线续传、旧 Host 解析合法 DEGRADED/INVALID EV03 | 新能力可协商，旧 payload 和客户端保持工作 |
| CrashRecord | 固定字段、CRC、commit-last、旧 commit 失效→ACK_UNACKED→字段/CRC→屏障→新 commit、未知版本、半写、A/B 选择、sequence、ACK 幂等、自检失败不广告 | native 能证明编码/恢复逻辑，但不证明实板保持 |
| Release profile | Debug/FaultInjection 可注入；Release 无注入符号、字符串、命令和可达路径 | 产品构建不带主动崩溃入口 |
| 受控启用 | 开关关闭时原 V1 准入/协议/fault entry；开关打开且 Crash backend 自检失败时不广告 Crash capability | 回退路径不影响已发布 V1 |
| 资源回归 | ROM/RAM/stack/DMA 静态预算、编译告警、事件路径时延预算 | 旁路能力没有超过已批准的软件预算 |
| Host/证据 | 原始 EV03 校验、evidence schema、CrashRecord raw 导出、符号化失败保留原始 PC/LR | Host 不猜测、不覆盖设备结论 |

自动化测试可以证明软件规则、编解码和协议契约，不能把模拟存储、fake reset 或 native
Fault 注入写成目标板硬件通过。

## 必须实板验证矩阵

| 项目 | 实板要求 | native 不能替代的原因 |
|---|---|---|
| H743 异常入口 | HardFault、MemManage、BusFault、UsageFault 的真实 vector、栈帧、SCB 寄存器、不可返回 reset | vector、异常栈形态、缓存/MPU/FPU 和二次 fault 是芯片运行时行为 |
| D3 SRAM4 CrashRecord 区 | software reset、watchdog reset、目标支持的系统 reset 下保留性；启动初始化不清除记录 | retention 取决于电源域、复位源和启动顺序；单字探测不能替代完整记录验证 |
| 事件采集 | ICM45686 FIFO/DMA 丢样、序列缺口、前触发不足、duration cap、pool/queue 压力 | native 无法复现真实 DMA、IRQ、cache 和采集时序 |
| U2 event log | W25Q64 JEDEC、页写、4 KiB erase、WIP、SPI 时序、事务中断恢复和物理掉电窗口 | fake NOR 无法证明真实介质时序和掉电状态 |
| QSPI/OTA/model | 实际 QSPI 读写和 candidate/recovery/model/sidecar 互不破坏；CrashRecord 不得触碰这些区 | 分区所有权和外设时钟/驱动状态需在目标板确认 |
| UART3/TERP | PD8/PD9 实际连接、复位重连、旧客户端、断线续传、新 capability/new commands | loopback 不覆盖板级串口、电源复位和真实设备响应 |
| fault context 余量 | reset 延迟、watchdog 余量、IRQ/cache/MPU/FPU 组合、重复 fault 行为 | native 无法证明真实故障路径的时限和寄存器状态 |
| 长期与断电 | 若沿用 V1 门禁，按既有计划执行物理断电、长期运行和统计；CrashRecord 第一版不宣称断电保持 | 软件注入不能替代真实电源和介质故障 |

## 证据与发布规则

每个阶段必须保存固件 revision、build profile、产物 hash、测试输入、结果统计和失败日志。
实板证据必须标注板卡、硬件版本、复位源、介质、执行次数和时间；simulation/native、bench
和hardware-in-loop 不得混写。

V1 release notes 已明确不把 72 小时、100 次物理断电和完整物理断连写成 V1 已验证能力。
本规格新增的 CrashRecord 也不得把 reset-retention 写成掉电持久化。没有实板证据的项目只能
标记为未验证风险，不能进入发布候选的 PASS 统计。

## 实施阶段允许确定的低风险细节

以下细节不改变本规格决策，并且必须在相应代码提交前固定到实现和 golden vectors：

- 新 TERP message 的具体未占用数字 ID、新 capability bit 的具体位值和错误码常量。
- 当前 H743 linker 使用的 D3 SRAM4 retention 符号、地址表达、段对齐和 A/B 槽具体 byte offset。
- `reason_flags` 的具体 bit 编号、CrashRecord 常量值、C struct packing 和端序断言。
- native/Host/实板测试脚本的具体文件名、fixture 名和证据包目录。

这些细节只能落实已冻结的边界；不得借此改变 EV03/EL01、V1 TERP payload、AI sidecar v2、
存储区域、CrashRecord reset-only 承诺、PASS-only AI gate 或 Release 无注入要求。

## 剩余风险

- D3 SRAM4 reset-retention 依赖实际 H743 板的电源域、复位源和启动初始化；第一版不覆盖
  完整断电。
- Fault 时栈、缓存、MPU、FPU、时钟或 RTOS 状态可能已损坏；固定捕获仍可能失败，必须以
  `capture_flags` 标示不完整现场。
- A/B 槽只保证最新记录优先，不能保存无限次崩溃；未 ACK 的旧记录可能在新故障时被覆盖。
- EL01 commit failure 和 capture failure 没有同介质事件记录，必须在 health/diagnostic
  证据中诚实呈现，不能伪造成 event evidence。
- 没有匹配 build_id 的 ELF/MAP 时，Host 只能显示原始 PC/LR，不能猜测函数名。
