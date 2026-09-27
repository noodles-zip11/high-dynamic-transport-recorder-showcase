# 实板记录 2026-08-04

- 固件提交：`b45286e36e398cdeb74c29d8a58a0408aafe1a49`
- 固件 SHA-256：`b4f248a20518776ac02d9172685df5e109312c1d7034981d74f58ecadd381e9a`
- ST-Link SN：`DEVICE_SERIAL_REDACTED__`
- ST-Link FW：`V2J46S7`
- 目标电压：3.24 V
- 连接方式：SWD，Hot Plug，950 kHz

| Gate | 结果 | 证据 | 观察 |
|---|---|---|---|
| H0 身份与 SWD | PASS | STM32CubeProgrammer 输出 | Device ID `0x450`、STM32H7xx、Cortex-M7、内部 Flash 2 MiB；SWD 连通。 |
| H1a 项目固件烧录与回读 | PASS | STM32CubeProgrammer 输出 | `transport_recorder.bin` 150.76 KB 写入 `0x08000000`，扇区 0-1 擦除后回读校验成功并软件复位。 |
| H1b 内核运行 | PASS | `-score` 输出 | 复位后报告 `Core is running`。 |
| H1c UART 启动日志 | BLOCKED | 无 | 尚未接入 3.3 V USB-TTL，不能观察 FinSH 或应用启动日志。 |
| H2 IMU SPI / FIFO / DMA | BLOCKED | 无 | 当前仅有 H743 板，未接 ICM45686 模块。 |
| H3a U2 SPI NOR JEDEC 探测 | FAIL | D2 SRAM1 诊断 + GPIO 软件 SPI 交叉读取 | 断电重上电后，硬件 SPI2 和独立 GPIO Mode 0 软件 SPI 均得到 `FF FF FF`；U2 接口未返回有效 JEDEC ID。 |
| H3b 事件与 SPI NOR 读写恢复 | BLOCKED | 无 | 先定位 H3a 初始化失败；随后仍需 UART 控制台与受控事件条件。 |
| H4 UART3 TERP | BLOCKED | 无 | 尚未接入 USB-TTL。 |

## 原厂固件备份

在覆盖前已通过 SWD 读取完整 2 MiB 内部 Flash。

- 文件：`D:\BaiduNetdiskDownload\STM32H743\backups\2026-08-04\openmv4_factory_internal_flash_2MiB.bin`
- 大小：2,097,152 bytes
- SHA-256：`4bda3a28f4ffe603c0ec1258c0034d65a1a0d35ab7bd523a834608adabf03cc5`

该备份在仓库外保存，避免把原厂二进制纳入版本控制。

## H3a SWD 诊断细节

固件启动时会调用 `storage_service_start()`，成功后才将完整三字节 JEDEC ID 写入
`storage_jedec_id`。为避免格式化或写入 U2，使用 SWD 只读 RAM：

- `storage_started`（`0x24026F98`）：`00`
- `storage_jedec_id`（`0x24026F9C`，3 bytes）：`00 00 00`

因此当时的结论是“U2 初始化或 JEDEC SPI 探测失败”，而不是“U2 的 JEDEC ID 为全零”。随后刷入
临时的只读诊断固件（仅发送 `0x9F`，不擦写 U2），再通过 SWD 读取 RAM：

- 诊断固件 SHA-256：`3514122a3a7e7a9c0066a892860e8c9fb6d1b24fb36071663abd657cf1f1baa4`
- `storage_jedec_id`（`0x24026FA4`，3 bytes）：`FF FF FF`
- `storage_last_nor_transfer_result`（`0x24000078`，32-bit）：`00000000`（`RT_EOK`）
- `u8_nor_spi_initialized`（`0x2402A410`）：`01`

该诊断的 RAM 变量位于 `0x24000000` AXI SRAM；运行时启用了 D-Cache，只有 D2 SRAM1 被配置为
non-cacheable。因此 ST-Link 读取到的物理 AXI SRAM 可能不是 CPU 缓存中的最新值：`FF FF FF` 和
`RT_EOK` 均不能作为 JEDEC 回包或 SPI 状态的硬件证据。此处先前将其判定为 U2 未响应是错误的，现已
撤回该结论。诊断后已恢复上方所列 SHA-256 的普通项目固件。

原理图视觉核对确认：U2 是普通 SPI `W25Q64`（CS=PB12、DO=PB14、CLK=PB13、DI=PB15），U3 是
独立 QSPI `W25Q64`（CS=PB10、D1=PD12、D2=PE2、D0=PD11、CLK=PB2、D3=PD13）。当前代码只选择
U2/SPI2，未碰 U3。下一次诊断必须把回包和状态放入 D2 non-cacheable SRAM，或在 SWD 读取前显式
clean D-Cache；随后才能判断 Flash 本体或连线。

### 缓存无关交叉诊断结论

在真正断电重上电后，使用诊断固件 SHA-256
`f5f6fad63274f7e5a4c517b2c50abd23cad73b844d36aa6dc9ad8db21ece95d8` 进行第二轮只读验证。
诊断块位于 MPU 配置为 non-cacheable 的 D2 SRAM1 `0x30000840`，因此 SWD 结果不受 AXI D-Cache
影响。两次读取均只发送 W25Q64 数据手册规定的标准 SPI `0x9F` 指令，不发送写使能、编程或擦除命令：

| 方法 | JEDEC 原始回包 | 传输/执行结果 |
|---|---|---|
| STM32 SPI2 HAL（PB12/PB13/PB14/PB15） | `FF FF FF` | `RT_EOK`，probe 返回 `-RT_ERROR` |
| GPIO 软件 SPI Mode 0（同一组 U2 引脚） | `FF FF FF` | 完成并恢复 SPI2 初始化，`RT_EOK` |

两种时序独立的方法在同一真实引脚上得到相同结果，故排除 AXI D-Cache 读回错误以及 SPI2 HAL/时钟/复用的
单一配置问题。当前可证实的范围是：U2 的 DO/MISO 在 JEDEC 响应阶段没有提供有效数据。尚未区分的物理原因
包括底面 U2 实物型号或装配、U2 的 VCC/GND、CS/SCK/MOSI/DO 导通及焊接；需底面清晰照片或逻辑分析仪/万用表
继续定位。诊断结束后已恢复普通项目固件，并确认 `Core is running`。
