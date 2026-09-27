# Main 硬件两小时内回归摘要

会话：`main-two-hour-regression-001`  
被测提交：`main@60eecbd56338a382256009d3be827149a5f8db41`  
回归工作树：`feature/main-hardware-regression-20260815`  
结论类型：**广覆盖 smoke regression；不是硬件合同完整收口。**

## 本轮已确认

- 工具链、原生 C、bootloader、host（118 项）和 AI（1 项）自动化测试全部通过；普通固件构建、应用地址/内存映射和 ELF/BIN/MAP 产物检查通过。
- ST-Link 身份通过：Device ID `0x450`、STM32H7、2 MiB、目标电压 3.23 V。
- 烧录前两份完整内部 Flash 备份均为 2,097,152 字节，SHA-256 一致：`C602B500231914F96A7FA6A1AADA244A89962A82BAC401D22E2D6145D67244A4`。
- 普通固件 BIN（`80B55AAB0926518181C302C9767A7C8E65468ECC05FB3084268435422200C1F7`）在 `0x08020000` 烧录并由 CubeProgrammer 校验通过；脚本现在自动把 BIN 和日志 staging 到 ASCII 路径。
- COM8 接到 UART1 后，复位捕获 909 字节，出现 RT-Thread 启动信息和 `msh >`；`sysinfo`、`event status`、`log status`、`log inspect` 均有回显。原 REG-001 是本次接线/监听端口状态，不是当前 main 固件缺少 UART1 控制台。
- UART1 `event trigger_test` 产生 event 2；设备自动完成日志写入，随后 `log list` 列出 event 2，`log verify 2` 返回 `verify=OK`。正确语义是等待自动导出完成后直接 `log list/verify`，不重复调用无 pending event 的 `event export`。
- UART3 `info`、`health`、`events list` 全部退出码 0；event 2 下载成功（38,560 字节），TERP 列表完整记录 CRC `2376085022` 与主机完整记录 CRC 一致，主机 EV03 解码通过，SHA-256 为 `DB96D6E27F887B18E8F5250BA825B1F93632E8D876056DCC79A6416CFC143E54`。
- 在断电重上和复位后将 USB-TTL 移到 COM7 再复验：`info`、`health`、`events list` 和 event 2 下载均退出码 `0`；当前连接的 event 2 主机 EV03 解码通过，长度、CRC 和 SHA-256 与此前一致。该结果证明核心链路不是一次性 COM8 会话偶然成功。
- 在同一物理连接上完成 20/20 次串口句柄重开：每轮 `info/health/events list/download` 成功，event 2 长度、完整记录 CRC 和 SHA-256 均稳定；这不是 20 次真实 USB-TTL 拔插，因此 H8 总门禁仍为部分通过。
- 新增只读物理矩阵脚本 [`scripts/terp_physical_matrix.ps1`](../../../../scripts/terp_physical_matrix.ps1)，当前已完成 1 轮非物理 dry-run；正式 20 轮会要求每轮人工输入 `READY`，dry-run 不计入 H8。

## 问题与处理结论

1. **REG-001 / P1：已通过接线复测关闭。** UART1 启动、FinSH 和新事件触发均有原始日志；尚未把 10 次冷启动计数升级为正式 H2 PASS。
2. **REG-002 / P2：已修复并加回归测试。** `scripts/cubeprogrammer_paths.ps1` 对中文源路径执行 ASCII staging，`scripts/program_firmware.ps1` 只把 staging artifact 交给 CubeProgrammer；中文路径 staging 测试和实际 `-Program` 烧录/校验均通过。
3. **REG-003 / P2：未修，且当前行为与实现一致。** `terp_uart3.c` 传入 `time_service=NULL`，因此 capability flags `43` 不含时间位，`time get` 返回设备不支持（退出码 3）。要关闭此项必须另行完成真实 RTC/VBAT/LSE 设计、初始化、断电保持和协议能力声明；本工作树不伪造时间。

## 尚未闭合的合同门禁

H0 照片、H1 正式 10 次连接、H2 正式 10/10 冷启动、H3 严格传感器/Flash 身份、H4 受控 Flash 写入、H5/H6 1800 秒 HV1、H7 100 次事件/窗口断电、H8 20 次真实物理拔插续传、H9 RTC/VBAT/IWDG/断开恢复、H10 USB CDC、H11 7200 秒整机长稳及 OTA 物理门禁仍不能标记为完整 PASS。执行顺序和停止条件见 [闭环证明计划](../../../../docs/superpowers/plans/2026-08-15-full-chain-closeout.md)。

## 证据入口

- 机器结果：[results/gate-results.json](results/gate-results.json)
- 会话元数据：[metadata.json](metadata.json)
- 构建产物哈希：[firmware/normal/hashes.txt](firmware/normal/hashes.txt)
- 烧录日志：[logs/program-script-ascii-regression.log](logs/program-script-ascii-regression.log)
- 完整自动化测试：[logs/run-tests-after-fix.log](logs/run-tests-after-fix.log)
- 固件构建：[logs/build-after-fix.log](logs/build-after-fix.log)
- UART1 启动/FinSH：[logs/uart1-followup.txt](logs/uart1-followup.txt)
- UART1 新事件：[logs/uart1-event-e2e-followup.txt](logs/uart1-event-e2e-followup.txt)
- UART3 event 2 下载：[results/event-new-download.json](results/event-new-download.json)
- UART3 event 2 主机解码：[results/event-new-decode.json](results/event-new-decode.json)
- COM7 复位后 smoke：[logs/scoped-com7-smoke.log](logs/scoped-com7-smoke.log)
- COM7 event 2 下载/解码：[logs/uart3-com7-event2-decode.log](logs/uart3-com7-event2-decode.log)
- UART3 20 次句柄重开：[logs/terp-handle-reconnect-20-current.json](logs/terp-handle-reconnect-20-current.json)
- UART3 time capability 复核：[logs/uart3-time-get-closure.log](logs/uart3-time-get-closure.log)
- UART3 10 分钟日志：[logs/uart3-ten-minute-health-observation.log](logs/uart3-ten-minute-health-observation.log)
