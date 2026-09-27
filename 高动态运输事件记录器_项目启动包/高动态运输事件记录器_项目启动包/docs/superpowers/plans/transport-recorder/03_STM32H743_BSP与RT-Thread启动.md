# STM32H743 BSP与RT-Thread启动 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让实际购买的STM32H743开发板稳定启动RT-Thread，并形成可解释、可复现的板级支持包，而不是只会在CLion或厂商工具中下载参考示例。

**Architecture:** 以RT-Thread已有H743 BSP为参考，实际板卡的时钟、引脚和存储布局以原理图与芯片手册为准。启动层只负责时钟、内存、Cache、串口和操作系统入口；业务代码不得直接依赖开发板名称。

**Tech Stack:** STM32H743VIT6、RT-Thread Standard、C11、SCons、GNU Arm Embedded Toolchain、OpenOCD或STM32CubeProgrammer、SWD。

---

## 0. 开发前Worktree门禁

在修改BSP、时钟、链接脚本或RT-Thread配置前，先阅读仓库根目录的[AGENTS.md](../../../../../../AGENTS.md)，并创建本阶段专用工作树：

```powershell
git rev-parse --is-inside-work-tree
git status --short
git worktree add ..\高动态运输事件记录器-worktrees\h743-bsp -b feature/h743-bsp main
Set-Location ..\高动态运输事件记录器-worktrees\h743-bsp
git branch --show-current
git status --short
```

只有第一条输出`true`、当前分支为`feature/h743-bsp`且两次状态检查均无输出时，才允许开发。若Git无效、`main`不存在、目录或分支已存在，立即停止并先运行`git worktree list`排查；不得删除或硬重置来绕过。所有地址、引脚和时钟变更还需满足AGENTS的嵌入式安全约束，提交、推送和PR仍需显式授权。

## 1. 本阶段边界

本阶段只证明“平台活着且基础配置正确”，暂不接IMU、Flash、USB和AI。

完成后必须得到：

- 可重复构建的固件；
- SWD可下载、可断点；
- 串口能打印启动报告并进入FinSH/MSH；
- SysTick或RT-Thread时基正确；
- 两个不同优先级的线程能稳定调度；
- 看门狗、DMA、Cache的最终策略有书面记录，尚未启用的部分也不能靠猜。

## 2. 先读资料再写配置

- [x] 打开开发板原理图，逐项填写 `docs/hardware/board_pinmap.md`。
- [x] 查STM32H743数据手册，确认具体封装为LQFP100以及可用引脚。
- [x] 查开发板晶振丝印或原理图，确认HSE和LSE频率。
- [x] 查供电与BOOT0电路，确认正常启动电平。
- [x] 查板载调试器类型；若没有板载ST-Link，准备独立ST-Link。

已固定 WeActStudio 官方资料快照并记录在
`docs/hardware/weact_h743_board_identity.md`与`docs/hardware/board_pinmap.md`。原理图与
ST 数据手册核对已完成。当前 BSP 是按 V1.2 原理图建立的**参考配置**；实物核验尚未
完成，不能宣称时钟、下载和引脚已经在目标板上确认。连接开发板后，先用实物照片或丝印
确认 MCU、PCB 版本和晶振器件与 V1.2 设计一致，再执行本章第 9 节硬件验收。

`board_pinmap.md` 至少记录：

| 功能 | MCU引脚 | 复用功能 | 电气说明 | 依据 |
|---|---|---|---|---|
| 调试串口TX/RX | 实板确认 | USARTx_AFx | 3.3 V | 原理图页码 |
| SWDIO/SWCLK/NRST | 实板确认 | SWD | 不得接5 V | 原理图页码 |
| LED/按键 | 实板确认 | GPIO | 有效电平 | 原理图页码 |
| HSE/LSE | 实板确认 | OSC | 实际频率 | 晶振型号 |

学习要求：先亲手在原理图上找到信号，再填表。CubeMX生成的结果只能用于交叉验证，不能反过来当硬件事实。

## 3. 建立BSP目录

- [x] 参考RT-Thread `bsp/stm32/stm32h743-st-nucleo` 的启动和构建结构。
- [x] 只复制所需启动文件和链接脚本，不复制无关外设配置。
- [x] 建立以下文件：

```text
firmware/
  SConstruct
  rtconfig.py
  bsp/weact_h743/
    SConscript
    board.c
    board.h
    board_pinmap.h
    link.lds
    stm32h7xx_hal_conf.h
  app/
    main.c
    system_report_format.c
  config/
    rtconfig.h
    project_config.h
  tests/native/
    test_memory_layout.c
```

- [x] 在 `board_pinmap.h` 中只写从原理图确认过的符号。
- [x] 在 `project_config.h` 中保存项目策略，不直接修改第三方HAL头文件表达业务选择。
- [ ] 用实物确认参考 BSP 的 PE3、PA9/PA10、25 MHz HSE 和下载连接后，再将其标记为硬件验收通过。

亲手完成：`SConstruct`、`SConscript`、启动串口引脚和LED定义至少手敲一遍，理解每个源文件为何参与编译。

## 4. 配置RT-Thread

在项目根目录执行：

```powershell
Set-Location firmware
scons --menuconfig
```

第一轮只开启：

- RT-Thread内核；
- FinSH/MSH；
- PIN与串口设备框架；
- 一个调试UART；
- 断言、线程栈溢出检查和内存使用统计。

第一轮关闭：文件系统、网络、USB、GUI、软件包和未使用外设。

- [x] 保存配置后运行`scons compile_commands.json`，在CLion中重新加载编译数据库并检查宏与头文件路径。
- [x] 用 `scons -j4` 完成真正构建。
- [x] 检查输出中的Flash/RAM占用，保留构建日志。

通过信号：生成 `.elf`、`.bin`、`.map`，命令退出码为0，且重复构建无随机变化。

## 5. 手工核对时钟树

- [x] 依据板级资料的25 MHz HSE计算PLL输入、VCO和系统时钟。
- [x] 将CPU频率设为240 MHz；480 MHz推迟到硬件测量后评估。
- [x] 核对APB分频；注意定时器时钟在部分分频条件下会倍频。
- [x] 核对Flash等待周期和电源电压等级。
- [ ] 用MCO或定时器翻转GPIO测量实际时钟，不只相信配置界面。

在 `docs/decisions/clock-tree.md` 写下：

```text
HSE实测/标称频率 -> PLL参数 -> SYSCLK -> HCLK -> APB1/APB2/APB3/APB4
为何先用该频率：
如何测量：
误差范围：
```

常见错误：把参考开发板的25 MHz HSE直接套到8 MHz或无HSE的板上，结果可能是串口波特率异常、USB失败或系统偶发HardFault。

## 6. 固定Flash与RAM布局

项目目标布局：

| 区域 | 地址 | 大小 | 用途 |
|---|---:|---:|---|
| Bootloader | `0x08000000` | 128 KiB | 后续安全启动与升级 |
| Application | `0x08020000` | 1792 KiB | RT-Thread应用 |
| Boot state | 内部Flash末尾独立扇区 | 128 KiB | 升级状态，具体扇区按RM确认 |

注意：上表是架构目标；写链接脚本前必须按STM32H743实际扇区表验证边界，不能仅按“每扇区128 KiB”想当然。

Phase 03 按已批准决策直接从 `0x08000000` 冷启动，避免在尚无Bootloader时留下空白启动区；`0x08020000` 仅作为后续Bootloader阶段的目标地址。

- [x] `link.lds` 将当前Phase 03应用入口设为 `0x08000000`，并记录未来 `0x08020000` 目标。
- [x] 将中断向量表地址同步到当前应用起始地址。
- [x] 从 `.map` 文件核对 `.isr_vector`、`.text`、`.data`、`.bss`。
- [x] 编写 `tools/check_memory_map.ps1`，构建后检查应用没有越过预留区域。

H743内存使用原则：

- DTCM：CPU快速访问的栈和算法临时数据；普通DMA不能访问。
- AXI SRAM/D1：通用大数据和模型工作区，需处理Cache一致性。
- SRAM1/2/3 D2：DMA采集缓冲区的优先位置。
- SRAM4/D3：低速或跨电源域的小状态数据。

## 7. 建立DMA与Cache规则

- [x] 在链接脚本增加 `.dma_buffer` 段，放入DMA可访问的D2 SRAM。
- [x] DMA缓冲区按32字节对齐。
- [x] 第一版使用MPU将DMA区设为non-cacheable，先保证正确性。
- [x] 记录未来采用Cache clean/invalidate时的方向和边界对齐规则。

必须理解：

- DMA写、CPU读：CPU读取前需要invalidate；
- CPU写、DMA读：启动DMA前需要clean；
- 操作范围要按Cache line向外对齐；
- 把缓冲区放进DTCM会导致DMA不可达，这不是驱动“偶尔失效”。

## 8. 最小启动程序

`app/main.c` 只完成：

1. LED心跳线程，每500 ms翻转；
2. 系统报告线程，打印一次启动信息后低频运行；
3. 一个MSH命令 `sysinfo`。

`sysinfo` 输出至少包括：

```text
firmware_version
git_revision_or_local
build_time
sysclk_hz
rt_tick_hz
reset_reason
heap_free_bytes
thread_count
```

- [x] 写入两个线程的创建、栈大小和优先级。
- [ ] 用 `list_thread` 查看状态，理解READY、SUSPEND和RUNNING。
- [ ] 故意把一个测试线程栈设得过小，观察并恢复，理解栈余量检查。

## 9. 下载与验收

示例命令，实际接口名称以第二阶段自检输出为准：

```powershell
pwsh scripts/program_firmware.ps1 -Program
```

- [ ] 冷启动10次，每次均在3秒内看到启动报告。
- [ ] 按复位键10次，线程与Shell均正常。
- [ ] 连续运行2小时，无HardFault、断言和异常复位。
- [ ] 用示波器或逻辑分析仪验证LED周期误差在预期范围。
- [ ] 保存串口日志到 `evidence/phase03/boot-smoke.log`。

## 10. 常见故障定位顺序

1. SWD连不上：先查供电、GND、NRST、BOOT0和SWD频率。
2. 下载后不运行：查应用地址、向量表、链接脚本和BOOT配置。
3. 串口乱码：查HSE、PLL、APB时钟和串口波特率。
4. 开Cache后崩溃：查MPU和DMA缓冲区，不先改业务逻辑。
5. 只在Debug运行：查启动代码、未初始化变量、优化级别和看门狗。

## 11. 阶段出口与自测题

阶段完成标准：构建、下载、启动、调度、复位和2小时运行全部有证据。

你应能不看答案解释：

- 为什么应用从 `0x08020000` 启动？
- 为什么H743的DMA缓冲区不能随便定义为普通全局数组？
- BSP、HAL、RT-Thread设备框架和业务代码分别负责什么？
- 串口乱码为什么可能是时钟问题而不是串口驱动问题？
- 为什么CLion编译数据库不是构建系统，真正构建仍由SCons负责？

下一步：[04_ICM42688P驱动与DMA采集.md](04_ICM42688P驱动与DMA采集.md)
