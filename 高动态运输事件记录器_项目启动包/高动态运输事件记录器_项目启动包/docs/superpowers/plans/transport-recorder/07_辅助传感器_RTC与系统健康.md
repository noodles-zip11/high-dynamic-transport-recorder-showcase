# 辅助传感器、RTC与系统健康 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 接入SHT40、内部RTC和系统健康监测，使每条事件具备环境、时间和设备状态上下文，并能区分正常、降级和故障。干簧管开箱检测明确延后为可选升级。

**Architecture:** 各设备驱动只负责可靠读写，health service聚合错误计数、时间有效性、复位原因、存储余量和任务心跳。看门狗只由健康监督线程喂，不由任意任务无条件喂。

**Tech Stack:** SHT40 I2C、STM32 RTC/LSE/VBAT、IWDG/WWDG、PVD/BOR、RT-Thread、可选INA226测试模块。

---

## 0. 开发前Worktree门禁

在修改RTC、看门狗、复位、电源或健康状态机前，先阅读仓库根目录的[AGENTS.md](../../../../../../AGENTS.md)，并创建本阶段专用工作树：

```powershell
git rev-parse --is-inside-work-tree
git status --short
git worktree add ..\高动态运输事件记录器-worktrees\system-health -b feature/system-health main
Set-Location ..\高动态运输事件记录器-worktrees\system-health
git branch --show-current
git status --short
```

只有第一条输出`true`、当前分支为`feature/system-health`且两次状态检查均无输出时，才允许开发。若Git无效、`main`不存在、目录或分支已存在，立即停止并先运行`git worktree list`排查；不得删除或硬重置来绕过。复位和看门狗策略需要故障注入证据，提交、推送和PR仍需显式授权。

## 1. 定义健康状态

统一状态：

```text
STARTING -> HEALTHY -> DEGRADED -> FAULT
```

- `HEALTHY`：所有必需链路正常；
- `DEGRADED`：可继续记录，但某项辅助能力失效，例如温湿度不可用或UTC未校时；
- `FAULT`：无法可信记录，例如IMU采集停滞、存储不可写或关键内存损坏。

每次状态变化写入RAM错误环和Flash系统记录，包含时间、模块、错误码和恢复结果。

## 2. SHT40驱动

```text
firmware/components/sensor/sht40.h
firmware/components/sensor/sht40.c
firmware/services/environment/environment_service.c
firmware/tests/native/test_sht40.c
```

- [ ] 使用I2C1（PB8/PB9）依次探测SHT4x定义的`0x44`和`0x45`，只接受唯一应答地址；确认测量命令、等待时间和CRC-8多项式。
- [ ] 在本机测试温度、湿度换算的黄金向量。
- [ ] 测试每个返回字的CRC错误。
- [ ] 限制输出范围，但同时保留原始值与错误标记，不把异常静默裁剪成正常值。
- [ ] 采样周期先设1秒；环境传感器不进入1 kHz高频路径。

板上验收：连续读取30分钟，和室内参考温湿度计比较趋势。目标不是实验室校准，而是识别断线、明显偏差和CRC错误。

## 3. RTC与时间模型

项目同时使用两类时间：

- `monotonic_us`：启动后单调递增，用于采样间隔、触发前后关系和超时；
- `utc_time`：人类时间，用于跨设备和物流时间线，可能无效或被重新校准。

禁止用RTC直接计算高频采样间隔，也禁止UTC校时后让事件内部时间倒退。

- [ ] 确认板卡LSE晶振与VBAT电池座是否实际存在。
- [ ] 首次启动设置备份域magic，后续启动不无条件重置RTC。
- [ ] 读取RTC时使用硬件推荐的一致性方法。
- [ ] 每条事件保存UTC值、UTC有效标志、时区无关的Unix时间以及单调时间。
- [ ] 上位机显示本地时区，但介质和协议统一保存UTC。

新增命令：

```text
time status
time get
time set 2026-07-13T08:00:00Z
time invalidate
```

`time set` 校验ISO 8601格式和日期合法性，并记录校时前后偏差及来源。

## 4. RTC掉电保持试验

- [ ] 装入正确规格纽扣电池，确认极性。
- [ ] 设置UTC并记录值。
- [ ] 断开主电源10分钟，保留VBAT。
- [ ] 再上电，确认RTC继续运行、备份magic存在。
- [ ] 取下VBAT再次断电，确认系统报告UTC无效而不是伪造默认时间。
- [ ] 保存结果到 `evidence/phase07/rtc-retention.md`。

## 5. 延后升级：干簧管与开箱事件

干簧管不属于07首期范围：不选型、采购、接线、分配GPIO、配置EXTI，也不实现开箱状态机或把开箱字段写入事件格式。首期记录中必须把“未安装/未知”与“箱体关闭”区分开，不得把缺失的门磁能力伪造为关闭状态。

后续需要开箱证据时，作为独立升级任务重新开启，并在开始前完成：

- [ ] 确认干簧管型号、机械安装方案、GPIO、上下拉和有效电平。
- [ ] 中断只记录边沿和时间，线程中做20到50 ms去抖。
- [ ] 定义打开、关闭、持续时间和“能力未安装”的稳定数据编码，再决定事件格式版本升级。
- [ ] 测试毛刺、连续弹跳、跨单调计时回绕、快速开关，以及磁铁长时间靠近时的中断风暴。
- [ ] 复位后读取当前电平，建立初始状态。

## 6. 复位原因与电源异常

- [ ] 启动最早阶段读取并保存RCC复位标志，保存后再清除。
- [ ] 区分上电、引脚复位、软件复位、独立看门狗、窗口看门狗和低电压复位。
- [ ] 根据供电设计配置BOR；PVD只用于提前发出电压下降信号，不能假设有足够时间完成大块Flash写入。
- [ ] 电压下降时停止新事件写入，尽快完成当前可安全提交的步骤或保持未提交状态。

不要在PVD中断里擦除Flash、打印日志或等待外设。

## 7. 看门狗与任务心跳

每个关键任务维护心跳：

| 任务 | 预期周期 | 健康条件 |
|---|---:|---|
| acquisition | 由FIFO水位决定 | 样本数持续增加、无长期DMA挂起 |
| event | 块持续消费 | 队列未永久堵塞 |
| storage | 写请求或空闲 | 状态机有进展、无永久BUSY |
| protocol | 有连接时 | 收发状态机有进展 |

- [ ] supervisor每个周期检查心跳是否在允许窗口内。
- [ ] 只有所有必需任务健康时才喂IWDG。
- [ ] 调试器暂停时明确处理看门狗冻结策略，发布构建不能依赖调试器。
- [ ] 故意挂起每个关键任务，验证系统最终看门狗复位并保存原因。
- [ ] 不把“线程还存在”等同于“线程有进展”。

## 8. 系统健康快照

`health_snapshot_t` 包含：

```text
state
reset_reason
uptime_us
utc_valid
imu_error_counts
storage_error_counts
free_log_bytes
sample_pool_free_min
thread_stack_minima
temperature_centi_c
humidity_milli_rh
power_state
last_fault_code
```

- [ ] 事件头保存触发时快照。
- [ ] `health show` 以人可读格式显示。
- [ ] 协议层以后传输稳定的字段编码，不直接发送结构体。

## 9. 可选功耗测试夹具

INA226不是首批产品必需件，可作为测试夹具加入：

- 测量启动、待机、持续采样、写Flash、USB传输五种状态；
- 记录平均电流、峰值、电源电压和测试条件；
- 不在第一版固件里强行集成INA226驱动。

若没有INA226，使用万用表和示波器完成基础测量，并标注仪器带宽限制。

## 10. 故障注入与验收

- [ ] 断开SHT40：系统进入DEGRADED，IMU事件仍可记录。
- [ ] RTC无效：事件明确标记UTC无效，单调时间仍正确。
- [ ] 挂起采集线程：看门狗复位，启动报告为IWDG。
- [ ] 让Flash持续BUSY：存储故障可见，不阻塞高频采集ISR。
- [ ] 连续运行12小时，保存health快照趋势。

干簧管的连续弹跳、开箱持续时间和中断风暴验证属于第5节所述的后续升级验收，不作为07首期门禁。

你应能解释：

- 单调时间和UTC为什么不能互相替代？
- 看门狗为何不能在定时器中无条件喂？
- PVD中断为什么不能保证完成一次Flash写入？
- 辅助传感器坏了为何通常是DEGRADED而不是FAULT？

下一步：[08_USB_CDC_TERP与命令行工具.md](08_USB_CDC_TERP与命令行工具.md)
