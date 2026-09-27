# CAN FD、ISO-TP与故障恢复 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 增加适合工业/车载现场的CAN FD通信，并让TERP部分消息通过ISO-TP复用，同时正确处理总线错误、拥塞和bus-off。

**Architecture:** STM32 FDCAN控制器连接外部3.3 V兼容收发器；底层驱动处理帧和错误，ISO-TP处理分段流控，TERP消息层不感知USB或CAN差异。CAN不是原始1 kHz波形的主要导出通道。

**Tech Stack:** STM32H743 FDCAN、外部CAN FD收发器、120 Ω终端、ISO 11898-1/2概念、ISO-TP、TERP、USB-CAN FD分析仪。

---

## 0. 开发前Worktree门禁

在修改FDCAN、ISO-TP、过滤器或bus-off策略前，先阅读仓库根目录的[AGENTS.md](../../../../../../AGENTS.md)，并创建本阶段专用工作树：

```powershell
git rev-parse --is-inside-work-tree
git status --short
git worktree add ..\高动态运输事件记录器-worktrees\can-fd -b feature/can-fd main
Set-Location ..\高动态运输事件记录器-worktrees\can-fd
git branch --show-current
git status --short
```

只有第一条输出`true`、当前分支为`feature/can-fd`且两次状态检查均无输出时，才允许开发。若Git无效、`main`不存在、目录或分支已存在，立即停止并先运行`git worktree list`排查；不得删除或硬重置来绕过。帧、时序和恢复策略变化必须同步协议文档与异常测试，提交、推送和PR仍需显式授权。

## 1. 为什么放到后期

CAN需要额外收发器、终端、电气布线和分析仪。首版开发板若没有收发器，FDCAN外设引脚不能直接接CAN_H/CAN_L。本阶段在USB协议稳定后开展，避免同时调试两套未知链路。

## 2. 硬件方案

- [ ] 选支持CAN FD数据速率、3.3 V逻辑且容易购买的收发器模块。
- [ ] 确认TXD/RXD与H743电平兼容。
- [ ] 确认STB/EN默认状态，加入可控引脚和安全上下拉。
- [ ] 总线两端各120 Ω，中间节点不额外终端。
- [ ] 使用双绞线，支线尽量短，共地或按场景采用隔离方案。
- [ ] 记录ESD/浪涌需求；原型模块不宣称工业防护等级。

第一版用模块和杜邦线做低速短距离验证；最终作品展示若需要整洁和可靠连接，再画转接PCB，见硬件计划中的PCB升级门槛。

## 3. 位时序亲手计算

在 `docs/communication/can_bit_timing.md` 记录：

- FDCAN内核时钟；
- 仲裁段目标速率，例如500 kbit/s；
- 数据段目标速率，例如2 Mbit/s；
- prescaler、TSEG1、TSEG2、SJW；
- 采样点百分比和误差；
- 是否开启BRS和原因。

先手算，再用配置工具验证。不同节点的时钟误差、传播延迟和采样点必须兼容。

## 4. 驱动文件与接口

```text
firmware/components/can/
  can_driver.h
  can_driver_fdcan.c
  can_health.c
  isotp_link.h
  isotp_link.c
firmware/tests/native/
  test_isotp.c
  test_can_recovery.c
host/tests/
  test_terp_over_isotp.py
```

底层统计：发送成功/失败、接收帧、仲裁丢失、错误警告、error passive、bus-off、恢复次数、RX FIFO溢出和过滤丢弃。

## 5. 从内部回环开始

- [ ] 开启FDCAN internal loopback，发送经典CAN帧并校验ID/DLC/data。
- [ ] 测试标准ID和扩展ID策略，项目第一版优先标准ID。
- [ ] 测试CAN FD 64字节帧和BRS。
- [ ] 配置消息RAM区域，检查元素大小与数量没有重叠。
- [ ] 过滤器只接收本设备所需ID，不默认接收全部总线流量。

内部回环通过只证明控制器配置基本正确，不证明收发器和总线正确。

## 6. 两节点实线测试

- [ ] 接入USB-CAN FD分析仪或第二节点。
- [ ] 断电测CAN_H到CAN_L约60 Ω，确认两个终端。
- [ ] 先经典CAN 500 kbit/s通信。
- [ ] 再启用CAN FD数据段和BRS。
- [ ] 用示波器观察差分波形、振铃和边沿。
- [ ] 逐步增加线长和负载，记录错误率。

禁止带电随意换终端或把逻辑引脚接到CAN总线。

## 7. ISO-TP实现与测试

支持单帧、首帧、连续帧和流控帧，明确：

- block size；
- STmin解释；
- N_As/N_Ar/N_Bs/N_Br/N_Cs/N_Cr超时；
- 序号回绕；
- 最大消息长度；
- busy/overflow流控；
- 会话和缓冲区所有权。

测试：0、1、单帧上限、上限+1、64、255、4095和项目最大长度；丢帧、重复帧、乱序、错误序号、超时、流控等待和接收缓冲不足。

不要把ISO-TP接收缓冲放在ISR栈上，也不要在ISR中等待下一帧。

## 8. TERP over ISO-TP

CAN通道只开放小而有价值的TERP子集：

- HELLO/设备信息；
- 健康和当前状态；
- 事件列表/摘要；
- 时间同步；
- 少量配置与诊断；
- 必要时的升级控制和小块传输。

大体积原始事件优先USB或TF卡导出。若通过CAN下载，必须限速并验证不会影响现场总线实时性。

## 9. Bus-off和错误恢复

- [ ] 读取并记录协议状态寄存器和错误计数。
- [ ] error passive时降低非必要发送并报警。
- [ ] bus-off时停止发送，通知health service。
- [ ] 按设计的自动/手动策略恢复，限制连续恢复次数。
- [ ] 反复bus-off进入DEGRADED或FAULT，不形成恢复风暴。
- [ ] 恢复后重新建立ISO-TP会话，旧分段消息作废。

故障注入：拔掉终端、速率不匹配、短时断线、持续发送无ACK、接收洪泛。不要故意将CAN_H/CAN_L直接短接电源。

## 10. 调度与带宽预算

- [ ] 计算每种周期消息的最坏总线占用。
- [ ] 诊断和事件摘要使用低优先级ID/队列，不抢占关键现场消息。
- [ ] 队列满时丢弃可丢的实时预览，保留错误与控制响应策略。
- [ ] CAN线程阻塞或洪泛不能影响IMU采集和Flash提交。
- [ ] 同时运行USB、CAN、存储和AI压力测试。

## 11. 验收与学习检查

- 两节点连续通信4小时无不可恢复错误；
- ISO-TP边界和异常测试全通过；
- bus-off至少20次可按策略恢复或进入可诊断状态；
- TERP同一消息在USB和CAN上的业务结果一致；
- 总线压力下1 kHz采样无丢样。

你应能解释：

- MCU的FDCAN控制器和CAN收发器分别做什么？
- 为什么总线两端需要120 Ω？
- 仲裁速率与数据速率有什么区别？
- ISO-TP为什么需要流控和超时？
- 为什么CAN不适合作为本项目原始波形的默认高速导出通道？

下一步：[13_系统验证_成果整理与面试.md](13_系统验证_成果整理与面试.md)
