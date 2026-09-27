# OpenMV4 STM32H743VIT6 板卡身份记录

## 已确认身份

| 项目 | 当前结论 | 依据 |
|---|---|---|
| MCU | STM32H743VIT6 | 用户实物芯片丝印确认 |
| HSE | 25 MHz | 用户提供的新板原理图 |
| LSE | 32.768 kHz | 用户提供的新板原理图 |
| SPI NOR U2 | W25Q64，SPI2 | PB12/PB13/PB14/PB15 原理图网络 |
| QSPI NOR U3 | W25Q64，QSPI | PB10/PB2/PD11/PD12/PE2/PD13 原理图网络 |
| USB | Type-C，PA11/PA12 | 用户提供的新板原理图 |

## 固定工程边界

- 当前构建选择 `firmware/bsp/openmv4_h743`，旧 `weact_h743` BSP 保留为历史板支持，
  不再由主构建选择。
- ICM45686 固定为 SPI1 PA5/PA6/PA7 + PB0/PB1；EL01 日志固定为 U2 SPI2。
- UART1 PA9/PA10 是 FinSH；UART3 PD8/PD9 是 TERP；PB10 不得作 UART。
- 使用 SHT4x 时摄像头 FPC 不接；LCD FPC 尚无驱动和验收计划。U3 QSPI 仍保留，未进入当前运行时。

## V1 板级状态与边界

V1 已取得 SWD/启动、U2 `EF 40 17` 与擦写、ICM45686 `WHO_AM_I`、INT1/DMA/FIFO、
UART1、UART3/TERP、1.6 kHz 采集、EV03/U2 事件日志持久化、自然触发、四分类推理和 WFI 状态链的
实板证据。发布口径和来源见
[`evidence/releases/v1.0.0/hardware/hardware-acceptance.md`](../../evidence/releases/v1.0.0/hardware/hardware-acceptance.md)。

USB CDC、RTC/VBAT/LSE 与物理 IWDG 不在 V1 核心功能声明内；100 次掉电、72 小时长稳和
完整物理断连矩阵属于延期可靠性验证。V1 也没有实测电流、节电百分比或电池续航结论。
这些边界不能反向改写为上述 V1 核心链路“尚未验证”。
