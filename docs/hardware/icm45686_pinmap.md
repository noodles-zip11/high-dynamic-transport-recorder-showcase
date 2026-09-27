# ICM45686 到 OpenMV4 H743 的接线

MCU 引脚的唯一来源是 [`board_pinmap.md`](board_pinmap.md)。当前 ICM45686 使用独立的
SPI1；板载 U2 W25Q64 独占 SPI2，二者不得共享总线。

| ICM45686 模块信号 | STM32H743 引脚 | 配置 | 说明 |
|---|---|---|---|
| SCLK / SCL | PA5 | SPI1_SCK，AF5 | MCU 输出时钟 |
| MISO / AD0 | PA6 | SPI1_MISO，AF5 | 传感器输出寄存器/FIFO 数据 |
| MOSI / SDA | PA7 | SPI1_MOSI，AF5 | MCU 写寄存器与发送空字节 |
| CS | PB0 | GPIO 输出，低有效 | BSP 手动控制，初始拉高 |
| INT1 | PB1 | GPIO 输入，EXTI1，上升沿 | FIFO 水位中断 |
| VCC | 3V3 | 模块供电 | 仅接 3.3 V |
| GND | GND | 共地 | 与 H743/USB-TTL 共地 |

模块资料确认 SPI 模式要求 VCC、GND、MISO、MOSI、SCLK、CS；INT1 在 FIFO 阶段启用。
CS 默认上拉，每个事务必须拉低但不能永久拉低。当前软件使用 Mode 0、MSB first，先以约
1 MHz 读取 `WHO_AM_I (0x72)`，预期 `0xE9`；通过后才启用 DMA、FIFO 与 INT1。

禁止使用 PB13/PB14/PB15 连接 IMU：它们已接到板载 U2 W25Q64。禁止使用 PB3/PB4/PD7：
它们连接 LCD FPC 或不属于此接线合同。

上板验收：断电导通检查 PA5/PA6/PA7/PB0/PB1；读取 WHO_AM_I；逻辑分析仪检查 CS/SPI
时序；启用 INT1 后检查 FIFO 水位、DMA 完成、序列连续性与至少 30 分钟稳定采样。
