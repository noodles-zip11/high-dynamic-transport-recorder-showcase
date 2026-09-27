# 硬件验证共同维护表

> **历史快照，不是当前 V1 状态页。** 本表冻结于 2026-08-15，保留当时镜像、门禁和失败
> 事实用于追溯；其中“当前”“尚未闭合”等措辞只对该日期成立。最新 V1 结论请以
> [`releases/v1.0.0/hardware/hardware-acceptance.md`](releases/v1.0.0/hardware/hardware-acceptance.md)
> 和 [`releases/v1.0.0/README.md`](releases/v1.0.0/README.md) 为准，不得用下方旧黄表覆盖发布证据。

最后更新：2026-08-15
证据来源：`feature/hardware-bringup`
设计合同：[STM32H743 + ICM-45686 + SHT40 硬件验证设计](../docs/superpowers/specs/2026-08-11-stlink-usb-ttl-hardware-validation-design.md)
机器门禁结果：[`gate-results.json`](hardware-bringup/gate-results.json)
合并证据哈希：[精选证据 SHA-256 清单](hardware-bringup/curated-evidence-sha256.txt)

本表把“硬件功能已实测通过”与“严格验收合同已全部闭环”分开记录。已经做过的实板结果不会因为缺一个 shim、照片或最终镜像复跑而被抹掉；但未满足严格验收条件的项目也不冒充整项绿灯。

> 历史集成边界：此前合入 `main` 的普通固件修复、SHT40 BSP、Bootloader/固件 OTA 软件基础及证据，合并当时未重新烧录或操作硬件；下方历史表格仍绑定各自日期目录和当时镜像。2026-08-15 的合并后 `main` 核心链路复验另列于下一节，不自动替代历史表格中的严格合同门禁。

## 2026-08-15 合并后 `main` 核心链路复验

本次在独立回归工作树从 `main@60eecbd56338a382256009d3be827149a5f8db41` 构建并烧录普通镜像到 `0x08020000`，随后完成一次跨串口核心闭环：

- UART1/COM8 捕获 RT-Thread 启动、`msh >` 和 FinSH；`event trigger_test` 生成 event 2，`log verify 2` 返回 `OK`。
- UART3/COM8 `info`、`health`、`events list` 均退出码 `0`；event 2 下载 `38560` 字节。
- TERP 列表记录的完整记录 CRC `2376085022` 与主机重新计算值一致；主机 EV03 解码通过，`sample_count=2400`、`data_loss_samples=0`，存储/导出/IMU 错误计数为 `0`。
- 在同一物理连接上完成 20/20 次串口句柄重开与 event 2 下载，长度/完整记录 CRC/SHA-256 均稳定；这只关闭句柄重开子门禁，不替代 20 次真实 USB-TTL 拔插。
- 本次证据目录：[main-two-hour-regression-001](hardware-bringup/2026-08-15/main-two-hour-regression-001/)，机器判定：[closure-results.json](hardware-bringup/2026-08-15/main-two-hour-regression-001/results/closure-results.json)。

这证明的是“构建/烧录 → UART1 触发 → 事件日志 → UART3 TERP → 主机解析”的核心链路，不等于整张硬件合同表全部变绿。H2 正式 10 次冷启动、H8 20 次真实物理拔插、H5/H6/H11 严格时长门禁、RTC/VBAT/LSE、USB CDC 等仍按下表保留未闭合状态。

## 🟡 尚未闭合（优先保留在表首）

| 门禁 | 已经完成的部分 | 尚缺条件 | 证据 |
|---|---|---|---|
| H0 受试板照片档案 | MCU 身份、SWD 连接和电压检查已通过 | 仓库中没有可链接的板卡正反面及最终接线照片，因此 H0 不能按设计合同整项标绿 | [2026-08-04 硬件摘要](hardware-bringup/2026-08-04/summary.md) |
| H2 最终普通固件冷启动 | 旧镜像 `10/10` 次真实冷启动通过；后续 `sysreport` 栈修复在实板做过短回归 | 两项使用的镜像 SHA 不同；缺少“本次合并后最终镜像”的 10 次冷启动复跑 | [冷启动报告](hardware-bringup/2026-08-14/cold-boot-10/attempt-5/cold-boot-10-report.md)、[精选收口摘要](hardware-bringup/curated-closeout.md) |
| H6 ICM-45686 严格 HV1 合同 | 30 分钟核心数据面已通过：`1802 s`、`2878530` samples、`completed=1`，FIFO/DMA/解析/容量错误均为 `0` | 当时诊断镜像未输出规范化 UART3 `START/PROGRESS/RESULT` HV1 JSON，结果也缺 `pool_backpressure_count` 和 `published_block_count` 两个 shim 字段；因此保留“核心长稳通过”，不声称严格 H6 全绿 | [SWD 原始结果](hardware-bringup/2026-08-14/feature-hardware-bringup-icm-30min/attempt-4/result-swd-console.log)、[正确心跳日志](hardware-bringup/2026-08-14/feature-hardware-bringup-icm-30min/attempt-4/monitor-heartbeat.log)、[验证摘要](hardware-bringup/2026-08-14/feature-hardware-bringup-icm-30min/attempt-4/verification-summary.md) |
| H8 USB-TTL 物理拔插与 20 次下载 | 同一物理连接上完成过 20 次串口 handle 重开，`info/health/list` 共 `60/60` 成功；另完成 7 次真实拔插 | 20 次 handle 重开不等于 20 次物理拔插；尚缺严格合同要求的 20 轮“物理断开/重连 + 下载 + 续传/一致性” | [20 次 handle 摘要](hardware-bringup/2026-08-13/main-148d7c3-fast-followup/terp-handle-reconnect-20/summary-all.txt)、[物理矩阵意图](hardware-bringup/2026-08-14/usb-ttl-physical-matrix/matrix-intent.md)、[第 7 轮后停止记录](hardware-bringup/2026-08-14/usb-ttl-physical-matrix/matrix-abort.txt) |
| H9 RTC/VBAT/LSE、watchdog、SHT40 断开恢复 | 已完成软件入口审计 | 当前普通固件缺可执行的 RTC/VBAT/LSE、复位原因/IWDG 和 SHT40 断开恢复验收入口；先补软件入口，再做人工验证 | [精选收口摘要](hardware-bringup/curated-closeout.md) |
| H10 USB CDC | 已在设计合同中明确为未实现边界 | 仓库尚未实现 USB CDC 引脚、48 MHz、VID/PID 和枚举链；没有枚举、20 次插拔或 TERP 等价功能证据 | 该项只记“未完成”；UART3 通过不替代 USB CDC |
| H11 修复后整机 7200 秒长稳 | 修复前曾运行约 `7261 s`，但采集结果为 `FAIL_CAPTURE`；修复后 SWD 只读最长窗口约 `1020 s`，错误计数为 `0` | 缺修复后连续 `7200 s` 的完整结果 | [17 分钟部分报告](hardware-bringup/2026-08-14/system-stability-2h/swd-runtime-2h/partial-window-summary.md)、[严格窗口停止记录](hardware-bringup/2026-08-14/system-stability-2h/swd-runtime-2h-final/capture-abort.txt) |

## 🟢 已通过（实板功能或明确子门禁）

| 门禁 / 子门禁 | 实际结果 | 真实证据 | 边界 |
|---|---|---|---|
| H1 SWD 连接与双备份一致性 | 连续 10 次 SWD 连接成功；两份内部 Flash 备份哈希一致 | [10 次连接](hardware-bringup/2026-08-12/main-598e536-no-power-cycle/swd-10-connect-console.log)、[备份 1 原始镜像](hardware-bringup/2026-08-12/main-598e536-no-power-cycle/internal-flash-backup-1.bin)、[备份 2 原始镜像](hardware-bringup/2026-08-12/main-598e536-no-power-cycle/internal-flash-backup-2.bin)、[哈希记录](hardware-bringup/2026-08-12/main-598e536-no-power-cycle/internal-flash-backups-sha256.txt) | H0 照片档案仍单独待补 |
| H2 冷启动功能子门 | `10/10` 启动横幅，`28` 个 `msh >`，assert/HardFault/stack overflow 关键字为 `0` | [报告](hardware-bringup/2026-08-14/cold-boot-10/attempt-5/cold-boot-10-report.md)、[机器摘要](hardware-bringup/2026-08-14/cold-boot-10/attempt-5/summary.json)、[UART1 原始字节流](hardware-bringup/2026-08-14/cold-boot-10/attempt-5/uart1-cold-boot-raw.bin) | 通过的是当时镜像；最终集成镜像复跑见表首 H2 |
| H3 ICM-45686/SHT40/U2 身份与 U2 安全拒写 | ICM `WHO_AM_I=0xE9`；SHT40 `0x44` 且序列号 CRC 通过；U2 JEDEC `EF4017`；默认非空测试区在 `stage=3 flags=0000` 安全拒写 | [传感器 UART3 原始日志](hardware-bringup/2026-08-12/main-598e536-no-power-cycle/sensor-diagnostic-uart3-raw.log)、[U2 原始日志](hardware-bringup/2026-08-12/main-598e536-no-power-cycle/flash-diagnostic-uart3-raw.log)、[精选收口摘要](hardware-bringup/curated-closeout.md) | `stage=3 flags=0000` 是当时诊断镜像对“写命令未执行”的等价编码 |
| H4 U2 可控擦写/重启持久性 | 只在受控尾部测试区擦写，两阶段验证通过，恢复普通固件后 U2 可重新挂载 | [精选收口摘要](hardware-bringup/curated-closeout.md)、[第一阶段 UART3 原始日志](hardware-bringup/2026-08-12/main-598e536-no-power-cycle/u2-7fe000-validation-uart3-raw.log)、[第二阶段 SWD 原始结果](hardware-bringup/2026-08-12/main-598e536-no-power-cycle/u2-7fe000-current-swd-result-raw.log) | 不包含 U3 |
| H5 SHT40 完整 HV1 30 分钟 | UART3 连续 `START/30×PROGRESS/RESULT`；SWD 结果一致；`1800/1800` 次测量，错误 `0`，`completed=1` | [UART3 原始流](hardware-bringup/2026-08-12/main-598e536-no-power-cycle/sht40-hv1-longrun-uart3-raw.log)、[SWD 原始结果](hardware-bringup/2026-08-12/main-598e536-no-power-cycle/sht40-hv1-longrun-swd-result-raw.log)、[解码结果](hardware-bringup/2026-08-12/main-598e536-no-power-cycle/sht40-hv1-longrun-decoded.txt) | 2026-08-14 的部分报告不再作为主证据；主证据是 2026-08-12 完整 HV1 |
| H6 ICM-45686 核心 30 分钟长稳子门 | `1802 s`、`2878530` samples、`287853` IRQ/services、FIFO 最大 `160`，FIFO/DMA/解析/容量错误均为 `0` | [SWD 原始结果](hardware-bringup/2026-08-14/feature-hardware-bringup-icm-30min/attempt-4/result-swd-console.log)、[心跳](hardware-bringup/2026-08-14/feature-hardware-bringup-icm-30min/attempt-4/monitor-heartbeat.log) | HV1 JSON/shim 合同缺口仍见表首 H6 |
| H7 U2 100 条事件链、列表与 CRC | `100/100` 触发/提交成功，事件 `1..100` 连续，`100/100 log verify=OK` | [精选收口摘要](hardware-bringup/curated-closeout.md)、[UART1 原始字节流](hardware-bringup/2026-08-14/u2-event-100/full-trigger-chain-3/uart1-event-100-incremental-raw.bin)、[机器摘要](hardware-bringup/2026-08-14/u2-event-100/full-trigger-chain-3/uart1-event-100-incremental-summary.json) | 不代表断电恰好命中未提交窗口 |
| H7 U2 真实断电后已提交数据持久性 | 重上电后 `state=1 events=1 next_id=2`，从 `superblock_b` 恢复，event 1 可列出且 CRC `OK` | [断电前原始流](hardware-bringup/2026-08-14/u2-write-interruption/attempt-3/uart1-interruption-precut-console.log)、[最终上电诊断](hardware-bringup/2026-08-14/u2-write-interruption/post-interruption-final-diagnostic-console.log)、[列表/CRC 复核](hardware-bringup/2026-08-14/u2-write-interruption/recovery-verify-console.log) | 已证明提交数据持久；本次断电未命中未提交丢弃窗口 |
| H8 UART3 TERP 当前连接下的协议功能 | COM7/115200 8N1 下 `info/health/events list`、event 1/100 下载均成功；每个事件 `38528` 字节并保存 SHA | [TERP 报告](hardware-bringup/2026-08-14/uart3-final-com7/uart3-final-com7-report.md)、[event 100 二进制](hardware-bringup/2026-08-14/uart3-final-com7/event-100.bin)、[哈希](hardware-bringup/2026-08-14/uart3-final-com7/event-100-sha256.txt) | 物理拔插 20 轮仍见表首 H8 |
| `sysreport` 栈修复后实板短回归 | 线程栈 `0x800`、最大使用 `57%`；5 秒采集到 5 条 IMU 与 5 条事件报告，错误标记为空 | [精选收口摘要](hardware-bringup/curated-closeout.md)、[线程采集](hardware-bringup/2026-08-14/system-stability-2h/runtime-probe-stack2048/runtime-commands-summary.json)、[5 秒摘要](hardware-bringup/2026-08-14/system-stability-2h/imu-repro-stack2048-5s/imu-repro-summary.json) | 仅是短回归，不扩大为 H11 2 小时通过 |

## 🟨 已执行但不冒充通过

- U2 写入中断试验已完成，重上电后 U2 健康且 CRC 正常；但 `recovery.discarded=0`，说明该次断电没有命中可证明的“未提交记录被丢弃”窗口。证据：[执行摘要](hardware-bringup/2026-08-14/u2-write-interruption/attempt-3/attempt-summary.md)。

## Bootloader / OTA（独立工作树，不计入本表统计）

- `feature/ota` 专门维护 Bootloader、固件 OTA、回滚、掉电安全和镜像有效性。
- 本节记录 `feature/ota` 的独立验证状态，不计入本表普通固件统计；合并软件基础能力不等于 Phase 11 全部验收通过。
- 2026-08-15 软件门禁复核通过：native Bootloader tests、镜像大小/链接图检查和 RT-Thread 符号排除均通过（历史基线 `24880/131072` bytes）；原始日志见 [`ota SW-BOOTLOADER-REGRESSION-001`](phase11-ota/2026-08-15/SW-BOOTLOADER-REGRESSION-001/test-bootloader.log)。这不是实板通过证据。
- 2026-08-15 修复内部 Flash 可写范围的无符号上溢边界，并用高地址、跨区域和零长度回归用例复测；Standalone Bootloader 镜像 `24968/131072` bytes，原始日志见 [`ota SW-BOOTLOADER-REGRESSION-002`](phase11-ota/2026-08-15/SW-BOOTLOADER-REGRESSION-002/test-bootloader-range-guard.log)。这不是实板通过证据。
- 2026-08-15 完整本地回归通过：native C `26` 项、Bootloader 门禁、host `100` 项和附加 host `1` 项均通过；原始日志见 [`ota SW-FULL-REGRESSION-001`](phase11-ota/2026-08-15/SW-FULL-REGRESSION-001/run-tests.log)。这不是实板通过证据。
- 2026-08-15 IRQ 保护修复后再次完成完整本地回归：native C `26` 项、Bootloader 门禁、host `100` 项和 AI `1` 项均通过；原始日志见 [`ota SW-FULL-REGRESSION-002`](phase11-ota/2026-08-15/SW-FULL-REGRESSION-002/run-tests-after-irq-guard.log)。这不是实板通过证据。
- 2026-08-15 合并前重新执行完整本地回归：native C、Bootloader 门禁、host `100` 项和 AI `1` 项均通过；原始日志见 [`ota SW-FULL-REGRESSION-003`](phase11-ota/2026-08-15/SW-FULL-REGRESSION-003/run-tests-pre-merge.log)。这不是实板通过证据。
- 2026-08-15 合并前重新构建默认固件 ELF/BIN/MAP：应用链接起点 `0x08020000`，memory-map 检查和构建产物检查通过；原始日志见 [`ota SW-FW-BUILD-001`](phase11-ota/2026-08-15/SW-FW-BUILD-001/build-pre-merge.log)。这不是实板通过证据。
- 2026-08-15 合并后重新执行完整本地回归：native C、Bootloader 门禁、host `118` 项和 AI `1` 项均通过；原始日志见 [`ota SW-FULL-REGRESSION-004`](phase11-ota/2026-08-15/SW-FULL-REGRESSION-004/run-tests-post-merge.log)。这不是实板通过证据。
- 2026-08-15 合并后重新构建默认固件 ELF/BIN/MAP：应用链接起点 `0x08020000`，memory-map 检查和构建产物检查通过；同时确认 QSPI HAL 的 MDMA 类型/源文件依赖已显式纳入，原始日志见 [`ota SW-FW-BUILD-002`](phase11-ota/2026-08-15/SW-FW-BUILD-002/build-post-merge.log)。这不是实板通过证据。
- 2026-08-15 两个分支合入 `main` 后再次执行最终全套回归：native C、Bootloader（含可写范围上溢回归）、host `118` 项和 AI `1` 项均通过；原始日志见 [`ota SW-MAIN-REGRESSION-001`](phase11-ota/2026-08-15/SW-MAIN-REGRESSION-001/run-tests-main.log)。这不是实板通过证据。
- 2026-08-15 两个分支合入 `main` 后再次构建默认固件 ELF/BIN/MAP：应用链接起点 `0x08020000`，QSPI HAL/MDMA、memory-map 和产物检查均通过；原始日志见 [`ota SW-MAIN-FW-BUILD-001`](phase11-ota/2026-08-15/SW-MAIN-FW-BUILD-001/build-main.log)。这不是实板通过证据。
- 2026-08-15 OTA 本地收尾门禁通过：HostOnly 环境检查、锁定的内部 Flash/QSPI 布局校验、OTA 打包工具 `41` 项测试、同输入双包生成哈希一致以及包回读检查均通过；原始日志见 [`ota SW-OTA-AUDIT-001`](phase11-ota/2026-08-15/SW-OTA-AUDIT-001/software-ota-audit.log)。本项不写板、不访问 SPI Event Flash，不替代实板断电与生产 Trial 验收。
- 2026-08-15 发现并修复 Bootloader 实板安装缺陷：降级安装时内部 Flash 擦写期间未屏蔽 SysTick，出现 `CFSR=0x00008200 / BFAR=0x00010000` 的 HardFault；在 `bootloader_flash_hal.c` 的擦除/写入临界区加入 IRQ 保护后，Bootloader 重新构建为 `24928/131072` bytes，镜像检查和 native 门禁通过。软件证据见 [`ota SW-BOOTLOADER-IRQ-GUARD-001`](phase11-ota/2026-08-15/SW-BOOTLOADER-IRQ-GUARD-001/)，实板故障与修复回放见 [`ota HIL-FW-UPDOWN-001`](phase11-ota/2026-08-15/HIL-FW-UPDOWN-001/)。
- 2026-08-15 ST-Link 只读快照成功：目标 `0x450`、3.23 V，当前 `VTOR=0x08000000`、USART3 `CR1=0x0000002D`；该条是修复前的历史快照，主状态页为 generation `12` / `CONFIRMED`，备用页为 generation `11` / `TRIAL` count `2`。未执行复位或写入；原始日志见 [`ota HIL-BOOT-SNAPSHOT-002`](phase11-ota/2026-08-15/HIL-BOOT-SNAPSHOT-002/stlink-hotplug-state-full.log)。
- 2026-08-15 发现板上 Bootloader/应用曾被其他映像覆盖；随后完成修复后最终基线恢复：固定 Bootloader SHA-256 `EF335254B1CCE9C7EAC7A554DC65323F53781895AAE6076EC7C06E5E3461C83F`，OTA-only 应用 SHA-256 `BA21D149B14CB226218D7CCBDA39FF34FF94A1CB47838AF0C2FD085B06AE4279`，只读确认 `VTOR=0x08020000`、USART3 `CR1=0x0000002D`、主/备状态页 generation `100/101` 均为 `CONFIRMED`；核心 PC 进入应用区。证据见 [`ota HIL-FW-UPDOWN-001`](phase11-ota/2026-08-15/HIL-FW-UPDOWN-001/) 和 [`ota HIL-FW-BASELINE-RESTORE-001`](phase11-ota/2026-08-15/HIL-FW-BASELINE-RESTORE-001/)。这是基线恢复证据，不替代完整 OTA 掉电验收。
- 2026-08-15 恢复后 COM7 的占用问题通过换线/重枚举变为 COM8；确认物理接到 UART3 `PD8/PD9` 并交叉 TX/RX 后，COM8 `info/health` 均成功，设备为 `STM32H743 / openmv4-h743-pd8-pd9`，健康错误计数为 `0`；原始日志见 [`ota HIL-FW-TERP-006`](phase11-ota/2026-08-15/HIL-FW-TERP-006/com8-info-health-uart3-retry.log)。本项只证明 UART3 TERP 基线，不替代事件下载或 20 轮物理拔插合同。
- 2026-08-15 COM8 UART3 又完成 10 次、约 65 秒的只读 `HELLO + health` 稳定性采样，设备身份一致、健康错误计数均为 `0`；原始日志见 [`ota HIL-FW-TERP-STABILITY-001`](phase11-ota/2026-08-15/HIL-FW-TERP-STABILITY-001/com8-terp-stability-60s.log)。这是 OTA-only 串口稳定性子门禁，不包含 SPI Event Flash 或事件下载。
- 2026-08-15 在 IRQ 修复后的最终 Bootloader + OTA-only 基线下，COM8/UART3 再完成 10 次、60.5 秒只读 `HELLO + health`；设备身份稳定，所有健康错误计数均为 `0`。原始日志见 [`ota HIL-FW-TERP-STABILITY-002`](phase11-ota/2026-08-15/HIL-FW-TERP-STABILITY-002/com8-terp-stability-post-irq-guard-60s.log)。
- 2026-08-15 完成一次 1.1.2 升级和一次 1.1.1 降级：两次包均通过 TERP 分块 CRC、整包校验和 Bootloader 可恢复复制，应用回读 SHA-256 分别匹配 `122494f1…` 与 `5b14233f…`；最终已恢复 OTA-only 基线，两个状态页为 `CONFIRMED`，`VTOR=0x08020000`。证据见 [`ota HIL-FW-UPDOWN-001`](phase11-ota/2026-08-15/HIL-FW-UPDOWN-001/)。该项只覆盖正常升级/降级，不替代 30 点物理断电矩阵和生产 health-based Trial 自动确认。
- 2026-08-15 完成 Phase 11 出口审计：当前 OTA-only、无 SPI Event Flash 操作范围内的可执行检查均已逐项归档；物理 30 点断电、生产 Trial 自动确认、Phase 10 模型 A/B 以及独立 Bootloader 救援通信仍明确列为未闭合，不将部分通过冒充整项绿灯。审计见 [`ota PHASE11-EXIT-AUDIT-001`](phase11-ota/2026-08-15/PHASE11-EXIT-AUDIT-001/exit-audit.md)。
- 本次合并边界：允许将 Bootloader/固件 OTA 基础能力合入 `main`，但合并说明必须保留上述四类未闭合门禁；在 30 点断电矩阵、生产 Trial 自动确认和独立救援通信完成前，不得将 OTA 标为量产放行或 Phase 11 全绿。
- Bootloader/OTA 未闭合项必须记在它自己的 evidence 文档中，不作为本轮普通固件合并的绿灯或阻断项。

## U3 QSPI（独立范围）

- U3 在本轮按范围暂停，未将 vendor SPI baseline、QSPI/MDMA 试验或本机绝对路径构建入口合入 `main`。
- U3 不影响 U2、SHT40、ICM 核心长稳和普通固件修复的历史结论。

## 状态规则

- 🟢 绿色只表示该行明确声明的功能或子门禁已有实板证据通过。
- 🟡 黄色表示已做过且有成果，但严格合同仍缺照片、字段 shim、最终镜像复跑、完整时长或物理矩阵。
- 软件集成通过不会把表首的硬件复验项自动改绿；未来补验时应使用合并后最终镜像，并新建日期证据目录。
