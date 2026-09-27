# Full Project Chain Closeout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用可复现的证据证明“构建/烧录 → 设备启动 → 传感器采集 → 事件落盘 → UART3 TERP 下载 → 主机解析”的链路闭环，同时把 RTC、USB CDC 和尚未达到合同门槛的长稳/物理拔插项目明确隔离。

**Architecture:** 先固定 `main` 软件基线并生成不可变哈希，再按只读门禁、受控写入门禁和跨串口事件门禁分层执行。UART1 只负责 FinSH/触发与设备状态，UART3 只负责 TERP/下载；每个门禁都有原始日志、机器结果和 SHA-256 清单。当前固件没有真实 RTC/VBAT 适配，因此 H9 的时间能力不通过伪造单调时钟解决，而是作为独立设计/实现门禁。

**Tech Stack:** STM32H743 + RT-Thread 5.3.0、SCons、STM32CubeProgrammer CLI、PowerShell 7、Python 3.12、pytest、TERP UART3 115200 8N1、UART1 FinSH 115200 8N1。

---

## 当前基线与边界

- 回归工作树：`LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\main-hardware-regression-20260815`。
- 基线：`main@60eecbd56338a382256009d3be827149a5f8db41`；普通应用写入地址 `0x08020000`。
- 已确认：UART1 接线改正后能捕获启动和 `msh >`；`event trigger_test` 自动生成 event 2，`log verify 2` 返回 `OK`；UART3 已完成 event 2 下载和主机 EV03 解码；CubeProgrammer ASCII staging 修复已通过；本地 `run_tests.ps1` 和 `build_firmware.ps1 -RequireElf` 通过。
- 未宣称：RTC `time get`（当前 capability flags=43，`time_service=NULL`）、USB CDC、20 次真实物理拔插、1800 秒/7200 秒合同长稳，以及需要额外设计的 RTC/VBAT/IWDG 门禁。
- 证据写入目录：`evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/`；禁止覆盖原始 `.raw` 和首次失败日志。

## 文件清单

- Modify: `scripts/program_firmware.ps1` — 仅从 ASCII staging 路径向 CubeProgrammer 提供 BIN 和日志。
- Create: `scripts/cubeprogrammer_paths.ps1` — ASCII 路径检测、隔离目录和 artifact staging。
- Create: `scripts/tests/test_cubeprogrammer_paths.ps1` — 中文源路径字节保持不变的回归测试。
- Modify: `scripts/run_tests.ps1` — 将 staging 回归纳入全套自动化测试。
- Create: `scripts/terp_physical_matrix.ps1` — 只读 UART3 物理拔插矩阵，逐轮要求人工确认并核对事件一致性。
- Create/Modify: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/logs/` — UART1/UART3、构建、烧录和主机命令原始日志。
- Modify: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/results/gate-results.json` — 每个硬件门禁的机器可读状态。
- Modify: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/summary.md` — 面向评审的结论、已知限制和证据入口。
- Create: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/results/closure-results.json` — 只在所有必需门禁完成后生成的最终闭环判定；不得用 `SMOKE_PASS` 替代 `PASS`。

### Task 1: 固定软件基线并验证工具链

**Files:**
- Read: `scripts/run_tests.ps1`
- Read: `scripts/build_firmware.ps1`
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/logs/closure-baseline.log`

- [x] **Step 1: 记录工作树和主线身份**

```powershell
git status --short --branch
git rev-parse main
git rev-parse HEAD
git diff --check
```

Expected: 分支为本回归工作树；源代码差异只包含已审阅的 ASCII staging 改动；`git diff --check` 无输出。

- [x] **Step 2: 跑全套本地验证**

```powershell
pwsh scripts/run_tests.ps1
pwsh scripts/build_firmware.ps1 -RequireElf
```

Expected: `All host-side tests: PASS`、原生 C/bootloader 全部 PASS、`Firmware artifacts: PASS` 和 `Application vector and memory map: PASS`。

- [x] **Step 3: 固化 BIN/ELF/MAP 哈希**

```powershell
Get-FileHash firmware/build/transport_recorder.bin,firmware/build/transport_recorder.elf,firmware/build/transport_recorder.map -Algorithm SHA256
```

Expected: 将完整哈希和文件大小写入 `firmware/normal/hashes.txt`，随后不再替换被测产物。

### Task 2: 执行上板安全前置和烧录验证

**Files:**
- Read: `scripts/program_firmware.ps1`
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/swd/backup-a.log`
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/swd/backup-b.log`
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/logs/program-script-ascii-regression.log`

- [x] **Step 1: 记录 ST-Link/目标电压/芯片身份**

使用 CubeProgrammer `-c port=SWD -l`，保存 CLI 输出；Expected: ST-Link 可见、目标为 STM32H743、目标电压约 3.2 V、内部 Flash 2 MiB。

- [x] **Step 2: 在 ASCII 目录做两份完整内部 Flash 备份**

```powershell
$ascii = 'LOCAL_USER_HOME\Desktop\hw_regression_backup_20260815'
New-Item -ItemType Directory -Force $ascii | Out-Null
& $ProgrammerCli -c port=SWD -r8 $ascii\internal-flash-a.bin 0x08000000 0x200000 -log $ascii\backup-a.log
& $ProgrammerCli -c port=SWD -r8 $ascii\internal-flash-b.bin 0x08000000 0x200000 -log $ascii\backup-b.log
Get-FileHash $ascii\internal-flash-a.bin,$ascii\internal-flash-b.bin -Algorithm SHA256
```

Expected: 两个文件均为 2,097,152 字节且 SHA-256 一致；若不一致，停止，不写入固件。

- [x] **Step 3: 通过脚本烧录并校验普通 BIN**

```powershell
pwsh scripts/program_firmware.ps1 -Program
```

Expected: 日志中出现 `Download verified successfully`、`SWD program and verify: PASS`；CubeProgrammer 的输入 BIN 和日志路径均为 ASCII-only。

### Task 3: UART1 启动、FinSH 和事件产生门禁

**Files:**
- Create: `scripts/hardware_uart1_smoke.ps1` — COM8 115200 8N1，监听启动，发送固定 FinSH 命令并保存 raw/text。
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/logs/uart1-closeout.raw`
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/logs/uart1-closeout.txt`

- [ ] **Step 1: 固定接线并做 10 次复位启动采样**

COM8 接 UART1（TX/RX 交叉、共地），每次通过 SWD reset 后监听至少 5 秒。每轮必须捕获 `RT-Thread` 与 `msh >`；记录复位次数、捕获字节数和是否出现乱码。

Expected: 10/10 轮出现启动提示，且没有 UART1 读写错误计数增长；任何一轮 0 字节都标记 `H2=PARTIAL`，不能继续宣称 UART1 门禁 PASS。

- [x] **Step 2: 在同一会话验证 FinSH 只读命令**

依次发送 `sysinfo`、`event status`、`log status`、`log inspect`；保存完整回显。Expected: 命令有明确回显、提示符回归 `msh >`，无 shell 崩溃。

- [x] **Step 3: 触发并验证新事件**

按设备实际语义执行 `event trigger_test`，等待 `event log write complete`，再执行 `log list` 和 `log verify`，目标 ID 取 `log list` 中本轮新增的最大 ID。不要在自动导出完成后重复执行 `event export`；该命令在无 pending event 时会返回 `-255` 并只增加运行时 `export_err` 计数。

Expected: `log list` 出现递增 event ID，对该 ID 执行 `log verify` 返回 `verify=OK`；将 event ID、长度、CRC 写入 `closure-results.json`。

### Task 4: UART3 TERP 下载闭环

**Files:**
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/logs/uart3-closure.log`
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/results/uart3-events-list.json`
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/results/event-new.bin`
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/results/event-new.sha256`

- [x] **Step 1: 将 COM8 物理移到 UART3**

UART3 使用目标板标注的 TX/RX（PD8/PD9），TX/RX 交叉、共地；不要把 UART1 的 FinSH 线直接当 TERP 线。执行 `ports --json` 记录 COM8 枚举。

- [x] **Step 2: 验证 TERP 身份、健康和事件列表**

```powershell
$py = '.venv\Scripts\python.exe'
& $py -m host.transport_recorder.cli info --port COM8 --json | Tee-Object evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/logs/uart3-info.json
if ($LASTEXITCODE -ne 0) { throw 'TERP info failed' }
& $py -m host.transport_recorder.cli health --port COM8 --json | Tee-Object evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/logs/uart3-health.json
if ($LASTEXITCODE -ne 0) { throw 'TERP health failed' }
& $py -m host.transport_recorder.cli events list --port COM8 --json | Tee-Object evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/results/uart3-events-list.json
if ($LASTEXITCODE -ne 0) { throw 'TERP events list failed' }
```

Expected: 三个命令退出码均为 0；`storage_ready=true`；列表包含 Task 3 生成的新 event ID；capability flags 仍可如实记录为 43，不得把缺失的时间位解释成通过。

- [x] **Step 3: 下载同一 event 并核对设备 CRC/主机哈希**

```powershell
$py = '.venv\Scripts\python.exe'
$resultRoot = 'evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/results'
$eventId = (Get-Content (Join-Path $resultRoot 'uart3-events-list.json') -Raw | ConvertFrom-Json).events[-1].event_id
$out = Join-Path $resultRoot 'event-new.bin'
& $py -m host.transport_recorder.cli events download --port COM8 --id $eventId --output $out --json | Tee-Object (Join-Path $resultRoot 'event-new-download.json')
if ($LASTEXITCODE -ne 0) { throw "TERP download failed for event $eventId" }
Get-FileHash $out -Algorithm SHA256 | Tee-Object (Join-Path $resultRoot 'event-new.sha256')
```

Expected: 下载退出码 0，文件长度等于 `log list` 的 `ev01` 长度；协议客户端的 CRC 校验通过；保存 JSON 响应和 SHA-256。

- [x] **Step 4a: 做 20 次同一物理连接的句柄重开子门禁**

当前镜像已完成 20/20 轮 `info/health/events list/download`，event 2 的长度、完整记录 CRC 和 SHA-256 均稳定。证据为 `logs/terp-handle-reconnect-20-current.json` 和 `results/terp-handle-reconnect-20/results.json`；`physical_unplug_count=0`，因此不能替代下一步真实拔插。

- [ ] **Step 4: 做 20 次真实物理拔插/重连**

每轮由人实际拔下并插回 USB-TTL，然后在当前工作树执行：

```powershell
pwsh scripts/terp_physical_matrix.ps1 -Port COM8 -EventId 2 -Rounds 20
```

脚本只执行 `info`、`health`、`events list` 和同一事件下载，不烧录、不格式化 Flash；每轮必须输入 `READY` 才会继续。Expected: `physical_confirmation=true` 且 20/20 轮命令成功，最终文件与首次文件字节一致；句柄重开不计为物理拔插。当前已用 `-SkipPhysicalPrompt -Rounds 1` 完成脚本 dry-run，不能替代该门禁。

### Task 5: 传感器、Flash、长稳和异常恢复门禁

**Files:**
- Read: `evidence/hardware-validation.md`
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/logs/hv1-sht40.log`
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/logs/hv1-icm45686.log`
- Write: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/results/hv1-results.json`

- [ ] **Step 1: 只读身份与安全拒写**

使用现有诊断镜像/既有证据确认 ICM-45686 `WHO_AM_I=0xE9`、SHT40 `0x44`/序列号 CRC、U2 `JEDEC=EF4017`，并确认默认非空测试区拒绝未授权写入；普通镜像不执行未经批准的 Flash 擦写。

- [ ] **Step 2: 运行 SHT40 和 ICM FIFO/DMA HV1**

各运行 1800 秒，记录每秒计数、错误计数、丢块/背压字段；Expected: `1800/1800`、错误为 0、完成标志为 1。未满足时保留原始日志，H5/H6 维持未通过。

- [ ] **Step 3: 执行 7200 秒只读整机长稳**

每 60 秒记录 UART1 `event status`、UART3 `health`/`events list` 和错误计数；Expected: 7200 秒内无复位、无错误计数增长、事件列表/CRC 稳定。10 分钟 smoke 只能标记 `SMOKE_PASS`。

- [ ] **Step 4: 单独处理 RTC/VBAT/IWDG/USB CDC**

当前 `firmware/app/transport/terp_uart3.c` 传入 `time_service=NULL`，因此 `time get` 返回 `TERP_ERROR_UNSUPPORTED` 是一致行为。只有完成 RTC 晶振/电池/备份域设计、驱动初始化、断电保持与 IWDG 恢复测试后，才把 H9 和时间 capability 设为 PASS；USB CDC 需要独立实现和枚举/收发证据，不能由 UART3 证据代替。

### Task 6: 生成最终机器判定和评审摘要

**Files:**
- Modify: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/results/gate-results.json`
- Create: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/results/closure-results.json`
- Modify: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/summary.md`
- Create: `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/SHA256SUMS-final.txt`

- [x] **Step 1: 只按原始证据更新门禁**

每个门禁只能使用 `PASS`、`PARTIAL`、`NOT_EXECUTED` 或 `BLOCKED`；`SMOKE_PASS` 不得映射为 `PASS`。REG-001 在 UART1 10/10 复位后才关闭；REG-002 以 ASCII staging 测试和实际烧录日志关闭；REG-003 继续保留，直到 RTC 设计和实现完成。

- [x] **Step 2: 生成闭环判定**

当且仅当 Task 1–5 的必需项全部为 `PASS`，写入：

```json
{
  "chain": "build->program->uart1->sensor->event-log->uart3-terp->host",
  "status": "PASS",
  "required_gates": ["R0", "H1", "H2", "H3", "H5", "H6", "H7", "H8", "H11"],
  "excluded_design_gates": ["H9-RTC", "H10-USB-CDC"]
}
```

若任一必需项仍为 `PARTIAL`/`BLOCKED`，写入同样结构但 `status: "INCOMPLETE"`，并列出具体 gate 和证据路径；不得用已有历史证据替代本次被测镜像的缺口。

- [x] **Step 3: 生成证据清单并复核**

```powershell
Get-ChildItem evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001 -File -Recurse |
  Get-FileHash -Algorithm SHA256 |
  ForEach-Object { '{0}  {1}' -f $_.Hash, $_.Path } |
  Set-Content evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/SHA256SUMS-final.txt
git diff --check
```

Expected: 原始日志、机器结果、固件哈希和摘要均有清单项；清单生成后不再修改证据文件。

## 退出标准

本计划完成不等于自动宣称产品验收。只有 `closure-results.json.status == "PASS"`、所有 required gates 为 `PASS`、RTC/USB 的范围决定已由项目负责人确认，才可以在 `main` 的硬件状态中写“整条项目链路闭环”。否则结论应明确写为“main 可运行 smoke 基线 + 剩余门禁清单”。
