# G1 new-capability closed loop — 2026-08-18

状态：**PASS / MODEL_OTA_LIVE_ACTIVATION / UART3_READBACK_HOST / CONTROLLED_MODEL_FAILURE**。

本轮先在独立工作树候选固件 `fix/model-ota-finalize-stack@4c47fe6`
上复测，再将修复快进合入 `main`，并在合并后的 Release 产物上重刷复核。
两份同提交构建产物因固件编入 `__TIME__` 而不同，必须按精确 BIN 分开归因：
候选 BIN 为 `BEDAE83E835A7A3F5E0CF402DFE31ED1038C1DDAB3F0D5BA91D30A5828250EA1`，
合并后 main BIN 为
`2F0E676507D1591060BA759D8F81C6DE472D302988172A22DEF627365B5E878A`；两者长度
均为 `128952`，逐字节差异为编译时间字符串。两轮应用镜像都只写入
`0x08020000`，未擦写 bootloader、内部 Flash 备份或整片 QSPI。

## 已完成的 G1 子门

- USB-TTL 保持 UART3/COM7（115200 8N1）；`info` 身份为 STM32H743、
  `phase08-terp-uart3`、`openmv4-h743-pd8-pd9`、`recorder-001`。
- Model A（510 B）完成 `begin → 2 次 write → finalize`，返回
  `total_bytes=510`、`verified_bytes=510`、`pending_install=false`、
  `model_valid=true`，期间 UART3 未掉线且未出现 HardFault。
- 候选 BIN 完成一轮带原始字节采集的 26 次 A/B 交替激活；每次均为
  `total=verified=510`、`pending_install=false`、`model_valid=true`。
  `active_slot` 是物理目标槽，会随上一次有效槽交替，不固定代表模型 A 或 B。
- 合并后的 main BIN 刷写/校验成功，并从持久化模型状态完成一次 Model A
  `finalize` 复核；随后在该精确 main BIN 上又保存了 26 次 B/A 交替激活，
  每次模型校验成功且物理 `active_slot` 相对前一次翻转，最终模型仍有效。
- 日志、逐帧摘要和机器判定保存在
  [`model-ota-activation-logged/`](model-ota-activation-logged/)；其中
  `uart3-activation.raw.log` 是 UART3 TX/RX 字节级采集，`run-summary.json`
  记录候选 BIN 的 26 次逐次结果及模型包哈希。合并后 main BIN 的同类证据在
  [`model-ota-activation-logged-main/`](model-ota-activation-logged-main/)，
  合并后刷写原始日志在 [`merged-main-reflash/`](merged-main-reflash/)。
- 运行结束 `GET_HEALTH` 返回 `storage_ready=true`、错误计数为 0，但
  `state=2` 是 `HEALTH_DEGRADED`，因此本轮不把整机 health 写成 PASS。

## UART1 FinSH 与连续事件子门（限缩通过）

指定审查任务对本轮新增证据结论为 **`APPROVE（限缩子门记录）`**。本段只记录
日志直接证明的子门，不提前关闭完整 G1、B-04、B-08 或 B-09：

- `uart1-finsh-followup/`：UART1/COM7/115200 8N1 只读基线确认固件为
  `4c47fe69b544ef9327300910d66f04927871f0dc`；`ps` 中 `terp_rx` 栈为
  `0x1000=4096` 字节、当前观测高水位 3%。原始串口日志 SHA-256 为
  `07FCF7BEA829CB68385C1A6809E16F62DA8BED47B01F5AA062EE989A60233BD4`，
  控制台日志 SHA-256 为
  `2D981CF366E7C888428753566EA0652A200A220F85C6A84AA2D9FAA08F1E0B73`。
- `uart1-event-boundary/`：未格式化 U2，连续触发事件 6–38 共 33 个；
  33/33 次均等待 `log events +1` 且回到 `event state=0`，最终日志为
  `events=38`，每条 `span=40960`、`ev01=38560`，事件/导出/资源错误均为 0。
  AI 统计为 `submitted=33 processed=33`，队列丢弃、sidecar/store、特征和
  runtime 错误均为 0。128-byte 记录与 4 KiB 扇区（32 条/扇区）的实现参数表明
  本轮写入负载覆盖了至少一个 sidecar 扇区边界；这不是 `GET_AI_RESULT` 读取或
  断电追溯完整性通过。raw SHA-256 为
  `811584E4640D0DE7CA4685B2D30DF5E471FFEB48B349BCDA227D6915E7568253`，
  console SHA-256 为
  `072ED37CE531FFB2AC64B487DE69DD73AD7AA843DA716429169E8A19DA37274C`。
- `uart1-postload/`：负载后 `terp_rx=3%`，`ai=91%`，`event=81%`；
  `ai submitted=33 processed=33` 且错误为 0。`ai/event` 高水位和
  `pool_backpressure=597` 只作为 C-03/B-08 风险观察，不能据此关闭 G3。
  raw SHA-256 为 `30875B9F66552D56E6C1490C0503777C71A2D31A30424B9F4B5D52AA0A5DD6E1`，
  console SHA-256 为 `F9EF964C84D66CBD48EC4A034E295993740BA115D186AB6CBF7D467F4E837189`。
- `uart1-software-reset-remount/`：ST-Link 软件复位返回码为 0；复位后启动日志
  出现 `AI result sidecar=ready`，`log events=38`，`log inspect` 为
  `recovery source=superblock_b generation=4 scanned=7 discarded=0`，
  `terp_rx` 仍为 3%。raw SHA-256 为
  `E0E1ABBBAE2F1E59452E7FB2340C9D0269568736D7888FEB3DEF743497D44FA4`，
  console SHA-256 为 `78D0B11F501F44180D440CB41A2768F9FE675622BB8CB59454413E98D83E4529`。

软件复位不等同于物理断电；上述记录不是 G2 掉电恢复或 G3 长稳/物理断连证据。
G0 的 `10/10` 冷启动已由 `core-acceptance-g0` 独立记录为 PASS；G2 与 G3 长稳/物理断连
已降级为 `DEFERRED_POST_CORE`。

## UART3 原始事件、AI 结果与 Host 闭环

`uart3-event-readback-logged/` 在 UART3/COM7/115200 8N1 上完成事件 ID 6–38
共 33 条的逐事件下载与校验。每条原始记录均为 38560 B，设备全文 CRC、下载全文
CRC、EV02/EV03 解码、事件 ID、AI 结果 ID/结果序号和模型身份均一致；Host SQLite、
JSON 导出和 Qt 回放均通过，人工 annotation 字段保持独立。核心证据哈希为：

- `uart3-terp.raw.log`：2686362 B，SHA-256
  `AD171AC30230F1A344C7A8909BDB3500853215D121BE1BD4C5A094220D427D35`。
- `uart3-frames.jsonl`：83246 B，SHA-256
  `F4E6539A2B094C5B3C499EBDCF459A238A13E69DA4118AAF65067DB532793969`。
- `event-comparison.jsonl`：47545 B，SHA-256
  `40394D248EDC8C50A32FD4515ACC7E7E459488B8447471C704F2180515C93C5E`。
- `run-summary.json`：92512 B，SHA-256
  `C6FC8510DA6B8A9705D41152D563D04B575A6513A347A8A930199D19F09229B6`。

## 受控模型失败与原始事件保留

为覆盖 G1 的“模型失败不影响原始事件”条款，本轮使用 ST-Link SWD 对运行时
易失 SRAM 做受控诊断注入：仅置空运行时模型指针、置位隔离标志并请求一次测试事件；
未写入 Flash/QSPI，也不是产品公开 UART 命令。注入操作日志
[`stlink-controlled-model-failure.log`](stlink-controlled-model-failure.log) 为
2623 B，SHA-256 为
`4EAA6E28E336416A39EC19932D38D80A70DBF6EA5C636195E1C0C6E17CFBBAAE`。

事件 39 通过 UART3 读回为有效 EV03，长度 38560 B；设备 CRC 与下载全文 CRC
均为 `3745476147`，原始文件 SHA-256 为
`CD30BC5A5B01058D9FCEE7CF5A4383711C02E8B79E601260120E7AFC7FF545E5`。
`GET_AI_RESULT(39)` 返回 `status=2/MODEL_UNAVAILABLE`、
`failure_reason=1/NO_MODEL`、`model_version=0`、`model_crc32=0`；Host SQLite、
JSON 和 UI 均保留 event 39 及该失败结果，label/note 仍为 null。

断电后重新上电的恢复观察见
[`stlink-controlled-model-failure/recovery-after-power-cycle.json`](stlink-controlled-model-failure/recovery-after-power-cycle.json)，
1377 B，SHA-256 为
`1548C94DF7E88610E0E9DB30087581C895DF6FCC0ADFBD7637B67634BCCEC6FA`：
`storage_ready=true`、存储/导出错误为 0、事件 39 和失败结果仍可读，
`model_valid=true` 且 `pending_install=false`。这证明本次模型失败不会阻止原始事件
提交或结果追溯；单次断电恢复仅作为 G1 观察，不替代 G2 掉电矩阵。

此前 GDB 连接导致运行态停顿、未形成事件的尝试日志仍作为历史诊断保留，不能作为
验收证据：`gdb-controlled-model-failure.log` SHA-256
`62761AF03559F7B34BB3AEEFEEB3DC49F0991E02B16F9A041050B04AB5B4ECDC`，
`gdb-controlled-model-failure-after-reset.log` SHA-256
`EBCF262F367BC5B8CCE4604B69326177FE21936E291600DD655878209340CAFC`，
`gdb-controlled-model-failure-after-power-cycle.log` SHA-256
`C7CBE728540645CAA5B4BCA1663041D5BAD9979707D2D9E1267BBF4104BD8A5D`，
`gdb-controlled-model-failure-booted.log` SHA-256
`45C5E7A403EA3056F79118E6D661704AC9442DF70AF01FB4BB514768CFFB4A56`。

指定审查任务最终结论为 `PASS`：确认模型失败不影响原始事件，且上述诊断边界、日志
和哈希已完整纳入本 summary、metadata 与共同台账。代码无需修改。

原始失败仍保留为历史诊断：旧镜像在 Model A `finalize` 阶段 TERP 超时，
ST-LINK 读回 `CFSR=0x01000000`（UNALIGNED），PC 指向
`icm45686_read_fifo_count`。随后审查通过的 ICM45686 未对齐访问修复和本轮
TERP worker 栈从 2048 B 增至 4096 B；候选 BIN 与合并后 main BIN 的 Model A
finalize 及各自的在线激活均未重现故障。

## 尚未关闭或已降级的 G2/G3 项

- G2 原 100 次受控掉电矩阵保留为 `DEFERRED_POST_CORE`，覆盖原始事件、AI sidecar、
  模型状态提交和轮转的撕裂/恢复合同。
- G3 当前核心只剩 2 小时并发预验收；72 小时长稳、物理断连/恢复及其栈余量和
  pool backpressure 合同保留为 `DEFERRED_POST_CORE`。本轮观察到 `ai=91%`、
  `event=81%` 和 `pool_backpressure=597`，仅作为风险记录。

G0、G1 已经指定审查任务最终确认并标记为 PASS；当前核心交付仅待 G3 2 小时预验收，
G2、G3 物理/72 小时及 B-04/B-08/B-09 的完整可靠性矩阵均为
`DEFERRED_POST_CORE`，不阻塞本轮收尾。
