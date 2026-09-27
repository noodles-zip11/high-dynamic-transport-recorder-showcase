# 工作树硬件验证补充记录（2026-08-13）

本目录记录用户确认的“先在工作树验证，完成后再合并 `main`”流程中的补充实测。被测工作树为 `feature/hardware-bringup`，固件 `HEAD=b45286e`；本轮不执行 Bootloader/OTA，也不执行 U3。主线 `148d7c3` 仅做边界对照，不把主线旧结果当作工作树验收结果。

硬件：STM32H743VIT6，ST-Link `DEVICE_SERIAL_REDACTED__`，3.23 V，CH340（真实重新上电后重新枚举为 COM5；此前有效收发证据来自 COM6），UART3 PD8/PD9，115200 8N1。

## 工作树实测结果

| 子门 | 结果 | 原始证据 | 边界 |
|---|---|---|---|
| UART3 TERP 主机命令稳定性 | 20 轮 `info` + 20 轮 `health` + 20 轮 `events list`，共 60 次，全部退出码 0，JSON 响应稳定 | `terp-handle-reconnect-20/session-*.log`、`terp-handle-reconnect-20/result-all.txt` | 这是同一物理连接上的句柄重开/命令循环，不等同于物理拔插 20 次 |
| ICM 500 ms FIFO/INT1/DMA | `INT1=487`；FIFO `0x1F00=7936` 字节；DMA start/completion 错误均 0；callback=1；`completed=1` | `icm-fifo-followup/swd-result-raw.log`、`icm-fifo-followup/decoded.txt` | 关闭 FIFO/DMA 短探针子门，不替代 ICM 30 分钟长稳 |
| 修复工作树普通固件最终复测 | 普通 BIN 直接烧录、校验、复位均退出码 0；随后 `info/health/events list` 各退出码 0；最终镜像再次抓到 TERP `25 -> 37` 字节响应 | `../20260813-fast-validation/repaired-worktree-ordinary-final-reflash-console-2.log`、`final-ordinary-*-console.log`、`final-uart3-hello/raw.bin` | 随后为了准备 U2 掉电阶段一，板上改为 U2-only 诊断镜像 |
| U2 替代空扇区阶段一（新鲜准备） | `JEDEC=EF4017`；`0x007FE000` 写入/读回通过，`stage=1 flags=1100`，烧录退出码 0 | `u2-alt-phase1-followup/stage1-fresh/uart3-raw.bin`、`u2-alt-phase1-followup/stage1-fresh/uart3-raw.txt`、`u2-alt-phase1-followup/stage1-fresh/program-console.log` | 当前板上停在真实断电门前；默认非空 `0x007FF000` 未触碰 |
| 普通固件 SWD 软件复位运行态 | 10/10 次复位、70 次变量读回、10 次释放均退出码 0；每次 IMU/事件/存储/TERP 标志均 `1`，IMU 启动阶段 `13`，`CFSR=0` | `ordinary-runtime-swd/read-symbols-raw.log`、`ordinary-runtime-swd/software-resets-10/software-resets-10-swd-raw.log`、`ordinary-runtime-swd/software-resets-10/summary.txt` | SWD 运行态子门；不替代 UART1/冷启动原始日志 |
| 重新上电后 USART3 TDR 方向探针 | 已执行；COM5 未收到字节，不能标绿 | SWD 直写 USART3 TDR `0x40004828=0x55` 成功（CubeProgrammer 写入/释放均退出码 `0`），但 COM5 原始接收为 `0` 字节 | `final-uart3-hello-after-u2/com5-swd-tdr-probe/tdr-write-console.log`、`rx.bin`、`summary.txt` | 只说明本次 COM5 端口/接线没有取得 MCU TX 字节；不覆盖此前 COM6 `25 -> 37` 的有效工作树收发证据；待重新接线或端口恢复后再复测 |
| 用户重新上电后的普通固件运行态 | 通过运行态子门（待合并复核） | 7 个 SWD 读回全部退出码 `0`；IMU、事件、存储、TERP 启动标志均 `1`；IMU 启动阶段 `13`；`CFSR=0`；随后释放 CPU 退出码 `0` | `ordinary-runtime-swd/post-power-cycle/post-power-cycle-runtime-swd-raw.log`、`summary.txt` | 只证明一次用户重新上电后的软件运行态；不替代冷启动 10 次、UART 原始启动流或整机长稳 |
| 按 UART3 修复报告重新烧录（用户重新上电后） | 固件侧通过；COM5 主机发送仍失败 | `IDLE_THREAD_STACK_SIZE=1024` 的普通工作树镜像重新构建退出码 `0`，SHA-256=`84E243C1A2452ADA0B27AB582F9BF82BE8D75474F48B26BE2559662DA55FC5EC`；CubeProgrammer 烧录/校验/复位退出码 `0`；USART3/PD8/PD9 SWD 配置仍为 `CR1=0x2D`、`BRR=0x412`、AF7。COM5 在 DTR/RTS/流控关闭后仍 `Write timeout`，.NET 串口写入也返回参数错误 | `uart3-repair-reflash-after-user-power-cycle/ordinary-build-console.log`、`program-console.log`、`post-reflash-usart3-gpio-swd.log`、`com5-hello/summary.txt`、`com5-dotnet-probe.txt` | 修复报告对应的软件改动已实际烧入；当前剩余是 CH340/COM5 驱动传输状态，不是接线改变或 USART3 配置错误；需要 USB-TTL 设备重新枚举后再做最终收发复测 |
| CH340 重新枚举为 COM6 后 UART3 恢复复测 | 🟢 通过（待合并复核） | 同一普通工作树镜像下，单帧 HELLO `25→37`、连续 3 帧 `75→111`；`info`、`health`、`events list` 均退出码 `0` | `uart3-repair-reflash-after-user-power-cycle/com6-hello-after-reenum/summary.txt`、`com6-repeat-after-reenum/summary.txt`、`com6-terp/*-console.log` | 证明 COM5 异常是重新枚举后的主机端口状态；不改变工作树证据与 main 集成分层 |
| COM6 TERP 有界稳定性矩阵 | 🟢 通过（待合并复核） | 20 次 `info` + 20 次 `health` + 20 次 `events list`，共 60 次；每次 8 秒硬超时，60/60 退出码 `0`，无超时 | `com6-terp-20-current/summary.txt`、`results.json`、逐次 `*-console.log` | 证明当前物理连接下的主机命令稳定性；不冒充 20 次物理 USB-TTL 拔插 |
| COM6 TERP 时间接口运行态探针 | 🟡 已执行 / 当前映像未提供时间服务 | `time set` 返回设备拒绝 `0x0005:2`；随后 `time get` 两次均返回 `0x0004:2`；`info` capability flags=`11`（未包含时间能力位） | `com6-time-runtime/set-console.log`、`get1-console.log`、`get2-console.log`、`summary.txt` | 这是当前普通映像的能力边界，不是 RTC 掉电通过；RTC/LSE/VBAT 仍需后续接入时间服务并做人工断电验证 |
| COM6 当前状态复核 | 🟢 通过（待合并复核） | `ports --json` 发现 `USB-SERIAL CH340 (COM6)`；当前板上普通工作树镜像可稳定响应 `info`、`health`、`events list`；四项退出码均为 `0`；二次探测仍识别为 `phase08-terp-uart3`；`storage_ready=false`、事件列表为空与当前未执行 U2 日志格式化一致 | `current-state-check/ports-console.log`、`info-console.log`、`health-console.log`、`events-list-console.log`、对应 `*-exit.txt`、`summary.txt`；二次探测见 `current-probe-2/` | 只证明当前 UART3/TERP 控制面可用；不关闭 UART1 FinSH、U2 可靠事件日志、RTC 或物理拔插门 |
| UART1 FinSH 只读命令链路 | 🟢 子门通过（待合并复核） | COM6/115200 8N1 收到 `msh >`；`sysinfo`、`event status`、`log status`、`log list`、`log inspect` 五条只读命令均有回显，进程退出码 `0`；`JEDEC=EF4017`、`log state=0`、`events=0`、恢复扫描 `scanned=1 discarded=0` | `uart1-finsh-readonly/uart1-finsh-readonly-raw.bin`、`uart1-finsh-readonly-console.log`、`summary.txt` | 格式化前只读基线；格式化后结果见下方 |
| UART1 事件触发/未就绪存储降级 | 🟡 已执行 / 安全拒绝路径符合预期 | `event trigger_test` 被接受并处理；因 `log state=0`，原始日志出现 `event log is not ready; event discarded`；`event export` 明确拒绝，`resource_err=0`，未发生 U2 写入 | `uart1-event-preflight/uart1-event-preflight-raw.bin`、`uart1-event-preflight-console.log`、`summary.txt` | 只证明存储未就绪时的事件安全降级；不关闭 U2 格式化、事件写入/读回、重启恢复或 100 次闭环 |
| UART1 `log format` 二步确认/超时保护 | 🟢 安全保护子门通过（待合并复核） | `log format` 只返回 `format pending`；等待 11.2 秒后 `log format --confirm` 返回 `format not confirmed`；随后 `log status` 仍为 `state=0 events=0`，证明超时确认未擦写 U2 | `uart1-format-confirmation-guard/uart1-format-confirmation-guard-raw.bin`、`uart1-format-confirmation-guard-console.log`、`summary.txt` | 只验证防误擦确认机制；没有验证真正格式化、事件写入/读回或掉电恢复 |
| U2 整片备份资产审计/豁免 | 🟡 已豁免（不作为通过条件） | 用户明确说明 U2 现有内容无需保留并授权直接格式化；没有 8,388,608-byte 整片备份，整片只读尝试的中断流仅作诊断证据保留 | `u2-full-readonly-dump/u2-full-backup-waiver.txt`、`u2-full-backup-audit/summary.txt` | 明确破坏性授权，不是数据备份通过；不涉及 MCU 内部 Flash、Bootloader/OTA 或 U3 |
| U2 全片格式化与日志初始化 | 🟢 已通过（待合并复核） | 普通镜像构建/烧录/校验均退出码 `0`；`log format --confirm` 耗时 `115.703 s`；随后 `log state=1 events=0 next_id=1 next_offset=0x00002000 jedec=ef4017` | `u2-format-after-waiver/ordinary-build-console.log`、`ordinary-program-console.log`、`uart1-format-after-waiver-raw.bin`、`uart1-format-after-waiver-console.log` | 关闭日志初始化子门；不代表断电恢复或 100 次闭环 |
| U2 事件写入、列表、CRC 校验与软件复位恢复 | 🟢 已通过（待合并复核） | 自动导出出现 `event log write begins/complete`；`events=1 next_id=2 next_offset=0x0000c000`；event 1 `span=40960 ev01=38528`；`log verify 1=OK`；软件复位后 `recovery source=superblock_b generation=2 scanned=2 discarded=0`，事件仍可验证 | `u2-event-after-format/uart1-event-after-format-raw.bin`、`uart1-event-after-format-console.log`、`u2-recovery-after-reset/uart1-recovery-after-reset-raw.bin`、`uart1-recovery-after-reset-console.log`、`reset-program-console.log` | 关闭格式化→单事件→软件复位恢复子门；真实断电/掉电恢复、100 次资源恢复和 TERP 下载仍未完成 |
| UART1 FinSH 事件读取栈修复后紧凑复测 | 🟢 通过（待合并复核） | 初次紧凑探针在 `log list`/`log verify 1` 触发 `tshell stack overflow`；未扩大线程栈，改为把 `event_log` 读/CRC 工作缓冲移入 `event_log_t`；重新构建、烧录、校验后 `sysinfo`、`event status`、`log status`、`log list`、`log inspect`、`log verify 1` 全部有回显，`event=1 verify=OK`，恢复源为 `superblock_b generation=2` | `u2-current-compact-check/uart1-compact-console.log`（失败探针）、`u2-finsh-stack-fix/ordinary-build-read-scratch-console.log`、`ordinary-program-read-scratch-console.log`、`post-fix-compact-raw.bin`、`post-fix-compact-console.log`、`transport_recorder-read-scratch-sha256.txt` | 只关闭当前工作树 UART1 事件读取栈子门；不把 `FINSH_THREAD_STACK_SIZE=4096` 试验算入结果；仍不替代主线复测、真实掉电、100 次资源恢复或 TERP 下载 |
| U2 十次事件精简资源恢复矩阵 | 🟢 代表性子门通过（待合并复核） | 不重新格式化/烧录；10 次连续触发均出现 `event log write complete`；最终 `log state=1 events=11 next_id=12 next_offset=0x00070000`；列表含 event 1..11；event 2 和 event 11 独立 `verify=OK`；恢复源为 `superblock_b generation=2` | `u2-event-10-compact/uart1-event-10-raw.bin`、`uart1-event-10-console.log`、`verify11-only-console.log`、`event-10-closeout-summary.txt`、`README.md` | 只关闭精简代表性矩阵；不扩展为 100 次，不替代真实掉电/中断恢复或 TERP 下载 |
| U2 十一条事件软件复位恢复 | 🟢 通过（待合并复核） | 不重新烧录；SWD 软件复位退出码 `0`；复位后 `events=11`、`recovery source=superblock_b generation=2 scanned=12 discarded=0`；event 11 CRC 仍为 `OK` | `u2-event-10-compact/reset-recovery-10-program-console.log`、`post-reset-status-raw.bin`、`post-reset-status-console.log`、`verify11-only-console.log` | 只关闭多事件软件复位恢复；不替代真实掉电/写入中断扫描恢复 |
| U2 默认最后 4 KiB 测试区掉电保持 | 🟢 通过（待合并复核） | U2-only 诊断阶段一 `JEDEC=EF4017 stage=1 flags=1100`；用户真实断电/上电后 SWD 阶段二 `power_cycle_pattern_verified=1`、`final_sector_erased=1`、`failure_offset=0`；随后恢复普通工作树镜像 | `u2-default-sector-power-cycle/stage1/uart3-raw.bin`、`stage1/decoded.txt`、`stage2/swd-result-console.log`、`stage2/decoded.txt`、`restore-ordinary/` | 只关闭默认测试区真实掉电保持/擦回门；不替代 U2 写入中断恢复、冷启动 10 次、RTC/VBAT 或 USB-TTL 物理拔插矩阵 |

## 中止或待后续项目

- ICM 30 分钟长稳：按用户要求本轮暂缓。`icm-30min/` 仅保存构建产物和启动阶段残留，不含完整运行结果，禁止据此标绿。
- 物理 USB-TTL 拔插/重新连接 20 次：本轮未执行，已有的 60 次命令循环只能作为软件句柄重开子门。
- U2 替代空扇区和默认最后扇区的掉电保持门均已在本目录关闭；U2 可靠事件日志、冷启动/RTC、事件写入和整机稳定性继续保持各自边界。

本轮 `u2-alt-phase1-followup/retry-no-cli-log/` 中的 `stage=2` 不计为掉电通过；随后由 `stage1-fresh` 重新准备，并在用户真实断电/上电后读取到独立的 `stage=2`、掉电图样校验通过且测试区擦回的结果，详见 `power-cycle-result-decoded.txt`。

## 主线边界对照

本轮曾构建并直接烧录 `main=148d7c3` 普通镜像；COM6 UART3 HELLO 收到 0 字节，原始文件为 `main-148d7c3-uart3-hello-*.bin`。这只证明当前主线尚未包含工作树 UART3 修复，不能覆盖工作树通过结果，也不作为工作树失败证据。随后重新烧录了修复工作树普通镜像；U2 阶段二读取完成后再次烧录并校验普通镜像，当前板上为工作树普通固件。

补充说明：用户真实断电/上电后 CH340 曾从 COM6 重新枚举为 COM5；`ports --json` 能发现 COM5，但 `transport-recorder info --port COM5`、默认串口参数写入和显式关闭 DTR/RTS 后的 pyserial 写入均返回 Write timeout。随后在不改固件的情况下用 SWD 直写 USART3 TDR（写入退出码 `0`）并监听 COM5，仍收到 `0` 字节；原始证据单独保存于 `final-uart3-hello-after-u2/com5-swd-tdr-probe/`。用户重新枚举回 COM6 后，单帧/三帧 HELLO 和 `info/health/events list` 全部恢复通过，证明该异常是一次性主机端口状态，不改变工作树 UART3 结论。普通镜像复烧和 10 次 SWD 运行态验证均已完成。后续探测中 COM6 被 SerialPort Pro 占用，`info` 返回 Windows `PermissionError 13 / error 5`；该次未烧录、未杀进程，详见 `current-probe-3/summary.txt`。

所有本目录文件均为命令原始输出、镜像副本或机器生成摘要；目录哈希见 `SHA256SUMS.txt`。
