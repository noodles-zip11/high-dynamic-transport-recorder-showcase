# OpenMV4 STM32H743VIT6 引脚表

## 使用范围

本表是当前目标板 `STM32H743VIT6` 的 MCU 引脚与板载器件分配唯一来源。依据用户提供的
`D:\BaiduNetdiskDownload\STM32H743\HDK\原理图.pdf`，并以实物芯片丝印
`STM32H743VIT6` 为前提。模块接线表只能引用本表，代码使用
`firmware/bsp/openmv4_h743/board_pinmap.h`。

该表定义软件接线合同，不替代导通、上电、逻辑分析仪或长期稳定性验收。

## 启动、调试与状态

| 功能 | MCU 引脚 | 复用/电气关系 | 有效电平/边界 |
|---|---|---|---|
| 状态 LED | PC2 | 板载 RGB 的绿色通道 | 低电平亮 |
| 用户按键 | PC13 | K_USER 接地 | 按下为低 |
| SWDIO / SWCLK | PA13 / PA14 | 板载 SWD 排针 | 3.3 V 调试接口 |
| 调试串口 | PA9 / PA10 | USART1，保留给 FinSH/文本诊断 | 3.3 V USB-TTL，不能与 TERP 混用 |
| TERP 串口 TX / RX | PD8 / PD9 | USART3，AF7，排针空闲信号 | 板 TX 接 USB-TTL RX；板 RX 接 USB-TTL TX |
| USB FS D- / D+ | PA11 / PA12 | 板载 Type-C 直连 | 当前软件未启用 USB CDC |
| HSE | PH0 / PH1 | 板载 25 MHz 晶振 | 系统时钟来源 |
| LSE | PC14 / PC15 | 板载 32.768 kHz 晶振 | RTC 专用，不再分配 GPIO |
| VBAT | VBAT | 板载 RTC 纽扣电池电路 | 仅 RTC 后备域使用 |

`PB10` 连接板载 U3 QSPI Flash 的片选，严禁分配给 USART3_TX。`PB11` 不单独使用，
USART3 固定成对使用 PD8/PD9。

## 采集与事件日志

IMU 与事件日志保持独立 SPI 总线，避免 1.6 kHz DMA 采集与 Flash 擦写/写入发生总线仲裁。

| 功能 | MCU 引脚 | 外设/板载关系 | 边界 |
|---|---|---|---|
| ICM45686 SCK / MISO / MOSI | PA5 / PA6 / PA7 | SPI1，AF5，外接模块 | IMU 专用总线 |
| ICM45686 nCS | PB0 | 普通 GPIO，低有效 | 上电初始拉高，手动片选 |
| ICM45686 INT1 | PB1 | GPIO / EXTI1 | 上升沿 FIFO 水位中断 |
| U2 NOR SCK / MISO / MOSI | PB13 / PB14 / PB15 | SPI2，AF5，板载 W25Q64 | EL01 事件日志专用 |
| U2 NOR nCS | PB12 | 板载 W25Q64 片选 | 不得分配外接模块 |
| U3 QSPI Flash | PB10、PB2、PD11、PD12、PE2、PD13 | 板载 W25Q64 | 为后续 staging/model 槽保留，当前不配置 |

U2 的完整 JEDEC ID 必须在首轮上板时记录；现有 W25Q 驱动只接受完整三字节
`EF 40 17`（Winbond W25Q64），并由该器件表项取得 8 MiB 容量、256 B 页和 4 KiB 扇区。

## 环境传感器边界

| 功能 | MCU 引脚 | 关系 | 边界 |
|---|---|---|---|
| SHT4x SCL / SDA | PB8 / PB9 | I2C1，AF4，开漏 | 与摄像头 FPC 的 SCCB 复用 |
| SHT4x VDD / GND | 3V3 / GND | 外接模块供电 | 禁止接 5 V |

板载摄像头 FPC 在 PB8/PB9 上已有 4.7 kΩ 上拉。使用 SHT4x 时必须不接摄像头，且不得再
并联未经计算的上拉电阻。LCD FPC 不属于当前 BSP 的功能范围；它不与本项目选定的 SPI1
引脚冲突，但若以后启用，须单独完成引脚分配、驱动和板级验收。

## 接线与验收前约束

- 外接 IMU、USB-TTL、调试器和传感器必须共地，所有逻辑电平为 3.3 V。
- TERP 的 UART3 仅传二进制协议帧；FinSH 仅使用 UART1。
- 使用 SHT4x 时不接摄像头 FPC；LCD FPC 当前不接入功能验证。不启用 U3 QSPI Flash，
  直到其独立驱动与验收计划获批。
- 第一轮依次验证：SWD 下载、UART1、UART3、U2 JEDEC、IMU WHO_AM_I、INT1/FIFO/DMA、
  1.6 kHz 写入，再做掉电恢复。

在这些验证完成前，构建和原生测试仅是软件证据，不构成新板硬件验收。
