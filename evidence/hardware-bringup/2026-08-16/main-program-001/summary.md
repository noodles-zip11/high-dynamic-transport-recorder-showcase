# 当前 main 固件烧录记录

日期：2026-08-16
目的：把当前仓库 `main` 基线烧录到已连接的 STM32H743VIT6，并在不修改 AI 文件的前提下完成启动验证。

## 来源与产物

- 工作树分支：`feature/tinyml-pipeline`
- 源提交（与 `main` 一致）：`203bfaa55ab905375c640216e78b8d438e25a89c`
- 应用地址：`0x08020000`
- BIN 大小：`176824` bytes
- BIN SHA-256：`44363DBA488CEFA5F2E0E893FCCC484F261DA29D1A82BD4339C1B115C8C2FC7F`
- ELF SHA-256：`4D41C35656598B11B5E31F4E15B512A90D500B182E0A2B6CC02592516A02903A`
- MAP SHA-256：`5143D94AE78BAA50AF88EFA989143C1030BA67900B65ADDA1EF3C1A7846B2307`
- 构建时刻（设备 `sysinfo`）：`Aug 16 2026 12:56:45`

构建脚本报告应用向量/内存映射通过，ROM 使用 176824 bytes，RAM 使用 186064 bytes。

## ST-Link 写入与校验

- ST-Link SN：`DEVICE_SERIAL_REDACTED__`
- ST-Link 固件：`V2J46S7`
- 目标：STM32H7xx，Device ID `0x450`，Flash `2 MBytes`
- 目标电压：`3.23 V`
- 写入：CubeProgrammer `-w ... 0x08020000`
- 校验：`Download verified successfully`
- 复位：软件复位成功

## 烧录后 UART1 运行态

设备返回：

```text
firmware_version=phase08-terp-uart3
git_revision_or_local=local
build_time=Aug 16 2026 12:56:45
sysclk_hz=240000000
rt_tick_hz=1000
reset_reason=0x01fe0000
heap_free_bytes=323552
thread_count=8
```

随后 IMU 运行报告持续增长，`fifo_count_err`、`fifo_capacity_err`、`fifo_read_err`、
`fifo_parse_err`、`dma_start_err`、`dma_timeout`、`dma_complete_err`、
`pool_backpressure`、`export_err` 和 `resource_err` 均为 0；`event state=0` 表示待命。

本轮 USB-TTL 保持在 UART1 以便读取 FinSH，因此没有把同一适配器再次移动到 UART3；
TERP `info/health` 可在移动至 PD8/PD9 后另做一次只读复核。ST-Link 的写入校验和
UART1 的启动/运行态已经完成。
