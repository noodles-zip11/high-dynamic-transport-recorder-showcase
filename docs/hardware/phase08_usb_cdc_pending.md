# Phase 08 USB CDC 板级占位

TERP 核心不包含 USB、UART、GPIO、时钟或 RT-Thread 细节。当前固件以 USART3 的
`PD8/PD9` 作为唯一二进制字节通道：接收字节进入 `terp_service`，响应从同一 UART 发出；
PA9/PA10 的 USART1 继续只作 FinSH/调试文本。

板载 Type-C 的 D- 直连 PA11、D+ 直连 PA12。当前仍未实现 USB CDC 的引脚复用、VBUS、
VID/PID、48 MHz USB 时钟或插拔恢复；不得把 UART3 或主机测试结果称为 USB 成功。

`PB10/PB11` 不能用于 UART3：PB10 是板载 U3 QSPI Flash 片选。UART3 实板接线固定为
`PD8 (板 TX) -> USB-TTL RX`、`PD9 (板 RX) -> USB-TTL TX`、`GND -> GND`，USB-TTL
必须为 3.3 V 逻辑。它不能运行 FinSH 或输出系统报告。

USB 实板前必须确认 D+/D-/VBUS/连接器路径、48 MHz 时钟误差、合法 VID/PID 与 UID 策略、
断开时发送任务不阻塞，以及基于板载 U2 Flash 的 20 次插拔和断续下载测试。
