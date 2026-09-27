# SPI Flash可靠日志与掉电恢复 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将完整事件可靠写入板载SPI NOR，并在任意写入时刻掉电后恢复到“旧记录保留、未完成记录可识别”的一致状态。

**Architecture:** 底层Flash驱动只提供读、页编程、扇区擦除和状态查询；其上使用追加式原始日志、双元数据副本和提交标记。事件记录先写头与载荷，最后写提交字段，不依赖通用文件系统提供事务语义。

**Tech Stack:** SPI NOR、STM32H743 SPI+DMA、CRC32、RT-Thread mutex/event、C11、本机Flash仿真测试。

---

## 0. 开发前Worktree门禁

在修改Flash格式、提交语义、恢复算法或擦除策略前，先阅读仓库根目录的[AGENTS.md](../../../../../../AGENTS.md)，并创建本阶段专用工作树：

```powershell
git rev-parse --is-inside-work-tree
git status --short
git worktree add ..\高动态运输事件记录器-worktrees\reliable-event-log -b feature/reliable-event-log main
Set-Location ..\高动态运输事件记录器-worktrees\reliable-event-log
git branch --show-current
git status --short
```

只有第一条输出`true`、当前分支为`feature/reliable-event-log`且两次状态检查均无输出时，才允许开发。若Git无效、`main`不存在、目录或分支已存在，立即停止并先运行`git worktree list`排查；不得删除或硬重置来绕过。介质格式和掉电测试必须随改动更新，提交、推送和PR仍需显式授权。

## 1. 先识别实际器件

- [ ] 从开发板原理图或芯片丝印确认板载Flash型号。
- [ ] 读取JEDEC ID并与数据手册核对厂商、容量。
- [ ] 记录页大小、最小擦除单元、典型/最大擦除时间、写寿命和3/4字节地址模式。
- [ ] 确认上电后是否可能处于Quad、深度休眠或写保护状态。
- [ ] 测量实际SPI时钟，并从低速开始提升。

建立 `docs/hardware/spi_nor_study.md`，所有几何参数来自实际器件，不在代码中散落魔数。

## 2. 驱动与日志分层

```text
firmware/components/storage/
  nor_flash.h
  nor_flash_rt_spi.c
  nor_geometry.h
  event_log_format.h
  event_log.c
  event_log_recovery.c
  event_log_gc.c
firmware/tests/native/
  fake_nor_flash.c
  test_nor_flash_rules.c
  test_event_log.c
  test_event_log_power_cut.c
tools/
  inspect_event_log.py
```

接口边界：

- `nor_flash` 不理解事件；
- `event_log` 不直接操作HAL；
- `event_log_recovery` 只根据介质内容恢复状态；
- 上层只有记录追加、枚举、读取、标记导出等语义。

## 3. 先用假Flash写规则测试

假Flash必须模拟真实约束：

- 擦除后每位为1；
- 编程只能把1变为0；
- 页编程不能越过页边界；
- 擦除按扇区；
- 忙状态期间拒绝新操作；
- 可在第N个字节后模拟断电。

- [ ] 测试非法0到1写入会失败。
- [ ] 测试跨页写入由驱动正确拆分。
- [ ] 测试超时和忙状态。
- [ ] 测试地址与长度溢出。
- [ ] 测试写后读回校验。

命令：

```powershell
pwsh tools/test_native.ps1 -Suite event_log
pwsh tools/test_native.ps1 -Suite event_log_power_cut
```

## 4. 定义介质格式

将字节布局写入 `docs/storage/event_log_v1.md`，至少包含：

```text
log superblock A
log superblock B
data erase block 0
data erase block 1
...
```

每条事件记录建议为：

| 字段 | 作用 |
|---|---|
| magic/version/header_length | 识别格式和向后兼容 |
| record_length | 跳到下一条记录 |
| event_id | 唯一标识 |
| UTC与单调时间 | 排序与时间有效性 |
| sensor/config/model versions | 可追溯性 |
| payload_length/payload_crc32 | 验证数据 |
| header_crc32 | 验证头部 |
| payload | 事件数据 |
| commit_marker | 最后写入，表示整条有效 |

实现要求：

- 所有整数明确为little-endian；
- 长度包括/不包括哪些字节必须写清；
- 结构体填充不进入介质格式；
- 解析任何长度前先检查上界；
- 未提交记录永远不作为有效事件返回。

## 5. 双元数据副本

超级块保存格式版本、代次、写指针、最旧记录位置和CRC。

- [ ] A/B位于不同擦除扇区。
- [ ] 更新时写入较旧副本，读回校验成功后才成为新代次。
- [ ] 启动时选择CRC正确且代次更新的副本。
- [ ] 两份均坏时扫描数据区重建，但不自动把随机字节解释为记录。
- [ ] 代次回绕比较有单元测试。

不要高频更新超级块。可按事件提交或检查点更新，权衡恢复扫描时间与写寿命，并把选择写入决策文档。

## 6. 事件写入事务

严格顺序：

1. 检查空间与单条记录上限；
2. 确保目标扇区已擦除；
3. 写未提交头部；
4. 分页写payload，同时计算CRC；
5. 回读抽检或完整校验；
6. 写footer/CRC；
7. 最后将commit marker从擦除态编程为有效值；
8. 更新RAM写指针；
9. 按策略更新超级块。

- [ ] 任一步失败都返回具体错误和失败地址。
- [ ] 不覆盖上一条已提交记录。
- [ ] 写任务使用低优先级线程，采集线程只投递完成事件。
- [ ] 写队列满时产生可见的存储背压错误。

## 7. 启动恢复算法

恢复时：

- 先验证两个超级块；
- 从可信检查点按记录边界扫描；
- magic、版本、长度、头CRC、载荷CRC和提交标志全部有效才接受；
- 遇到未提交记录，停止或按格式规定跳过；
- 遇到明显损坏，限制扫描范围并报告恢复降级。

- [ ] 启动报告输出扫描耗时、有效记录数、丢弃记录数和原因。
- [ ] 恢复过程不能向Flash写数据，除非用户执行显式修复命令。
- [ ] `log inspect` 命令以只读方式显示索引。

## 8. 环形空间与擦除策略

- [ ] 用整擦除块推进头尾指针。
- [ ] 擦除前确认块内记录已过保留期或已导出。
- [ ] 第一版空间满时停止记录并报警，不默认覆盖证据。
- [ ] 后续若允许循环覆盖，必须成为显式配置并记录删除原因。
- [ ] 统计每个擦除块的擦除次数，识别磨损集中。

可在设备空闲时预擦除下一个块，避免冲击事件后同步等待数秒擦除。

## 9. 板上命令

```text
flash id
flash geometry
log status
log list
log verify <event_id>
log read <event_id>
log format --confirm
```

`log format` 必须二次确认，调试构建也不得用一个误触按键清空记录。

## 10. 掉电故障注入

本机假Flash先做穷举：对一条事件写入的每个关键字节位置模拟断电，重启恢复后验证：

- 断电前已提交的事件仍存在；
- 当前半写事件不可见；
- 不越界、不死循环；
- 下一次追加不会覆盖旧事件。

实板测试至少100次：

- [ ] 用可控MOS负载开关或带开关电源切断供电，禁止频繁拔USB损伤接口。
- [ ] 在头部、payload中段、footer、提交和超级块阶段随机断电。
- [ ] 每次重启运行 `log verify`。
- [ ] 汇总到 `evidence/phase06/power-cut-results.csv`。

若没有自动断电治具，先用固件在指定写入步骤主动复位，覆盖逻辑故障；真实断电测试仍需保留为最终硬件验证。

## 11. 验收与学习检查

通过标准：本机故障注入全通过，实板100次断电无旧记录丢失，CRC损坏能检出，Flash满有明确行为。

你应能解释：

- 为什么写完payload后还需要最后的commit marker？
- NOR为什么不能把0直接改回1？
- 双超级块解决了什么，不能解决什么？
- 文件系统和事务日志不是同一概念，为什么本项目先用原始日志？
- 为什么空间满时默认停止比静默覆盖更适合证据记录器？

下一步：[07_辅助传感器_RTC与系统健康.md](07_辅助传感器_RTC与系统健康.md)
