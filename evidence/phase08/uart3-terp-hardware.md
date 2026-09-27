# Phase 08 USART3 TERP 实板验收记录

状态：**PENDING HARDWARE**。本文件描述已进入 H743 构建的预定配置，不是实板成功证据。

## 已构建的软件配置

- BSP 启用 `USART3`，D2PCLK1 作为 USART3 时钟；PD8/PD9 配置为 AF7。PB10 是板载 U3 QSPI Flash 片选，明确不作 UART。
- `app_runtime_start()` 在 storage service 启动后调用 `terp_uart3_start()`。
- UART RX 中断只通知 `terp_rx` 线程；线程才读取字节、解析 TERP、访问 storage service 并发送响应。通用串口 RX FIFO 配置为 1 KiB，给系统报告等较高优先级任务留出约 89 ms 的 115200 bit/s 接收余量。
- UART1/PA9/PA10 仍是 FinSH 控制台，绝不承载 TERP 二进制流。

## 接线

| 开发板 | USB-TTL | 约束 |
|---|---|---|
| PD8 / USART3_TX | RX | 交叉连接 |
| PD9 / USART3_RX | TX | 交叉连接 |
| GND | GND | 必须共地 |

USB-TTL 只能使用 3.3 V 逻辑电平；不要把 5 V TX 或供电脚接到开发板。

## 实板步骤与通过条件

1. 用 SWD 烧录本分支对应的 `transport_recorder.bin`，记录固件哈希、板卡丝印和 USB-TTL 型号。
2. 接入上述三根线，以 115200、8N1、无流控打开 Windows COM 口；发送 `transport-recorder --port COMx info --json`。
3. 通过条件：HELLO 成功，返回 `model=STM32H743`、`firmware_version=phase08-terp-uart3`，且没有 UART1 FinSH 文本混入。
4. 用板载 U2 W25Q64 Flash 记录至少一个事件，依次运行 `events list`、`events download`；下载后的完整事件 CRC 必须与设备返回 CRC 一致。
5. 对下载过程执行至少 20 次物理断连/重连；每次主机重新 HELLO 后，`.part`/manifest 能恢复，最终文件 CRC 正确，设备事件未被删除。
6. 保存终端日志、事件 ID、CRC、固件哈希和失败现象。没有这些证据，不得标记本文件通过。

## 尚未覆盖

- USB CDC（PA11/PA12、VBUS、48 MHz、VID/PID、枚举）仍是独立的后续工作。
- 当前 UART TX 为 RT-Thread 串口默认阻塞写；高吞吐、断线和与 1.6 kHz 记录并发的真实时序必须实测。
