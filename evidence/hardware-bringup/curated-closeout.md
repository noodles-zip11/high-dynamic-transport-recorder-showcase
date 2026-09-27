# Hardware bring-up 精选收口证据

日期：2026-08-15
来源分支：`feature/hardware-bringup`
集成声明：本文只整理历史实板证据；本次未重新烧录、连接或操作硬件。

本文替代会引用 U3、临时调查目录和未入选资产的全量历史索引。下面所有链接都在同一精选提交中保存；完整机器状态见 [`gate-results.json`](gate-results.json)，文件完整性见 [`curated-evidence-sha256.txt`](curated-evidence-sha256.txt)。

## H1 SWD 和内部 Flash 备份

- [10 次 SWD 连接原始控制台](2026-08-12/main-598e536-no-power-cycle/swd-10-connect-console.log)全部成功。
- [备份 1 控制台](2026-08-12/main-598e536-no-power-cycle/backup-1-console.log)和[备份 2 控制台](2026-08-12/main-598e536-no-power-cycle/backup-2-console.log)都保存了完整 2 MiB 读取过程。
- [备份 1](2026-08-12/main-598e536-no-power-cycle/internal-flash-backup-1.bin)与[备份 2](2026-08-12/main-598e536-no-power-cycle/internal-flash-backup-2.bin)的 SHA-256 都是 `419df5205641fd01a6dbf5d74f0330e95dea61f435710eb8580c18e00f4f4b35`，见[独立哈希记录](2026-08-12/main-598e536-no-power-cycle/internal-flash-backups-sha256.txt)。

## H3 身份与 U2 安全边界

- [传感器 UART3 原始日志](2026-08-12/main-598e536-no-power-cycle/sensor-diagnostic-uart3-raw.log)记录 ICM-45686 `WHO_AM_I=0xE9`、SHT40 `0x44` 和序列号 CRC 通过。
- [U2 UART3 原始日志](2026-08-12/main-598e536-no-power-cycle/flash-diagnostic-uart3-raw.log)记录 JEDEC `EF4017` 及 `stage=3 flags=0000`；这是当时诊断镜像的安全拒写编码。

## H4-U2 受控测试区掉电保持

- 临时测试区 `0x007FE000..0x007FEFFF` 的[只读备份](2026-08-12/main-598e536-no-power-cycle/u2-7fe000-sector-backup.bin)为全 `FF`，哈希见[备份哈希](2026-08-12/main-598e536-no-power-cycle/u2-7fe000-sector-backup-hash.txt)。
- 第一阶段的 [UART3 原始日志](2026-08-12/main-598e536-no-power-cycle/u2-7fe000-validation-uart3-raw.log)、[SWD 原始结果](2026-08-12/main-598e536-no-power-cycle/u2-7fe000-validation-swd-result-raw.log)和[SWD 解码](2026-08-12/main-598e536-no-power-cycle/u2-7fe000-validation-swd-result-decoded.txt)记录 `stage=1 flags=1100`，即初始擦除态与编程读回通过。
- 真实断电/上电后，[第二阶段 SWD 原始结果](2026-08-12/main-598e536-no-power-cycle/u2-7fe000-current-swd-result-raw.log)与[解码](2026-08-12/main-598e536-no-power-cycle/u2-7fe000-validation-power-cycle-swd-result-decoded.txt)记录 `stage=2 flags=0011`，即掉电图样通过且最终擦回。
- 普通镜像随后恢复：[恢复控制台](2026-08-12/main-598e536-no-power-cycle/session-image-restore-after-u2-console.log)、[整片读回](2026-08-12/main-598e536-no-power-cycle/session-image-restore-after-u2-verify-console.log)、[恢复哈希](2026-08-12/main-598e536-no-power-cycle/session-image-restore-after-u2-hash.txt)。

## H7 U2 事件链

- 100 条事件的 [UART1 原始字节流](2026-08-14/u2-event-100/full-trigger-chain-3/uart1-event-100-incremental-raw.bin)和[机器摘要](2026-08-14/u2-event-100/full-trigger-chain-3/uart1-event-100-incremental-summary.json)记录 `100/100` 提交、连续 ID 与 `100/100` CRC 通过。
- 另一次真实断电中，[断电前流](2026-08-14/u2-write-interruption/attempt-3/uart1-interruption-precut-console.log)、[重上电诊断](2026-08-14/u2-write-interruption/post-interruption-final-diagnostic-console.log)与[列表/CRC 复核](2026-08-14/u2-write-interruption/recovery-verify-console.log)证明已提交数据持久；该次没有命中未提交丢弃窗口。

## H9 软件入口审计

对当时普通固件的本地源码与已归档命令结果进行离线审计后，未找到可执行的 RTC/VBAT/LSE、复位原因/IWDG 或 SHT40 断开恢复专用验收入口。因此 H9 保持部分完成，不以无效的人工操作冒充验收。

## `sysreport` 栈修复短回归

- [线程采集摘要](2026-08-14/system-stability-2h/runtime-probe-stack2048/runtime-commands-summary.json)显示 `sysreport` 栈 `0x800`、最大使用 `57%`。
- [5 秒 IMU 回归摘要](2026-08-14/system-stability-2h/imu-repro-stack2048-5s/imu-repro-summary.json)记录 5 条 IMU 与 5 条事件报告，错误标记为空。这只关闭短回归子门，不代替 H11 的 7200 秒窗口。
