# Bootloader、固件OTA与模型OTA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在升级中断、镜像损坏或新应用无法启动时仍能回到可工作的固件，并让模型可以独立、兼容地升级。

**Architecture:** 内部Flash保留小型Bootloader和主应用；外部QSPI保存固件下载槽与模型A/B槽；内部Flash独立状态页保存升级状态。Bootloader只做校验、复制、试运行和回滚，不包含业务与GUI协议全集。

**Tech Stack:** STM32H743内部Flash、外部QSPI NOR、CRC32、SHA-256、可选数字签名、IWDG、TERP升级消息、Python打包工具。

---

## 0. 开发前Worktree门禁

在修改Bootloader、Flash布局、升级状态或跳转序列前，先阅读仓库根目录的[AGENTS.md](../../../../../../AGENTS.md)，并创建本阶段专用工作树：

```powershell
git rev-parse --is-inside-work-tree
git status --short
git worktree add ..\高动态运输事件记录器-worktrees\ota -b feature/ota main
Set-Location ..\高动态运输事件记录器-worktrees\ota
git branch --show-current
git status --short
```

只有第一条输出`true`、当前分支为`feature/ota`且两次状态检查均无输出时，才允许开发。若Git无效、`main`不存在、目录或分支已存在，立即停止并先运行`git worktree list`排查；不得删除或硬重置来绕过。任何地址、向量表和状态迁移变化都要更新设计与断电测试，提交、推送和PR仍需显式授权。

## 1. 安全边界和阶段顺序

OTA是高风险后期功能，只有前十阶段稳定后开始。

第一版目标是可靠性：损坏检测、断电恢复、回滚。CRC只能检测随机损坏，不能阻止恶意固件。若产品需要安全升级，必须增加公钥签名、密钥管理、防回滚和调试口策略，不能把CRC描述为安全认证。

## 2. 重新核对存储布局

内部Flash架构目标：

- Bootloader从 `0x08000000` 开始，预算128 KiB；
- Application从 `0x08020000` 开始；
- 一个真实独立可擦除区域保存boot state。

外部QSPI按实际容量划分：

- firmware staging slot，容量不小于最大应用镜像加manifest；
- model slot A；
- model slot B；
- 保留擦除对齐和坏数据隔离空间。

- [ ] 根据H743参考手册确认内部Flash bank/sector边界。
- [ ] 根据实际QSPI JEDEC信息确认擦除和地址模式。
- [ ] 生成 `config/memory_layout.yaml` 作为打包、链接检查和固件共同输入。
- [ ] `tools/check_memory_map.ps1` 阻止区域重叠和镜像越界。

任何地址改变都先更新设计文档、链接脚本和测试，禁止只改一处魔数。

## 3. 独立Bootloader工程

```text
bootloader/
  SConstruct
  src/main.c
  src/boot_state.c
  src/image_verify.c
  src/image_copy.c
  src/jump_to_app.c
  link.lds
  tests/native/
```

Bootloader依赖最少化：时钟、看门狗、内部Flash、QSPI、校验、状态页和一个救援通信接口。它不启动RT-Thread。

- [ ] 独立构建并输出大小，超过预算立即失败。
- [ ] Bootloader版本可从救援命令读取。
- [ ] 发布构建关闭危险的任意地址读写命令。

## 4. 应用有效性检查

跳转前至少检查：

- manifest magic与版本；
- 镜像长度在应用区域内；
- 初始MSP落在允许RAM范围并正确对齐；
- reset handler落在应用Flash范围且Thumb位有效；
- 镜像CRC/SHA-256正确；
- 硬件版本和最低Bootloader版本兼容。

无效时进入救援模式，不跳到随机地址。

## 5. 正确跳转序列

- [ ] 禁止中断并停止SysTick。
- [ ] 关闭Bootloader启用的外设与DMA。
- [ ] 清除挂起中断。
- [ ] 设置VTOR到应用向量表。
- [ ] 加载应用MSP。
- [ ] 跳转reset handler。

为跳转检查写本机测试；实板分别验证冷启动、软件复位、看门狗复位和升级后启动。若只在调试器连接时成功，阶段不通过。

## 6. 固件包格式

打包工具生成：

```text
manifest
image bytes
optional signature
```

manifest包含：产品ID、硬件兼容范围、固件版本、构建ID、镜像长度、目标地址、CRC32、SHA-256、最低Bootloader版本和签名算法标识。

- [ ] `tools/package_firmware.py` 输入ELF/BIN和版本，输出不可变升级包。
- [ ] `tools/inspect_package.py` 在不连接设备时验证包。
- [ ] 相同输入和版本生成相同核心内容。
- [ ] 打包拒绝镜像越界、空版本和产品ID不匹配。

## 7. 下载与暂存

TERP增加：开始升级、写块、查询进度、完成下载、取消升级、查询结果。

- 每块有offset、长度和CRC；
- 重复块幂等；
- 设备维护已验证连续区间；
- 完成前验证整包hash；
- 只有校验通过才设置 `PENDING_INSTALL`。

下载中断不会触碰当前应用。

## 8. Boot状态机

建议状态：

```text
NORMAL
PENDING_INSTALL
INSTALLING
TRIAL
CONFIRMED
ROLLBACK_REQUIRED
RECOVERY
```

状态页采用A/B副本、代次、CRC和最后写入提交语义。每个状态写清允许转移、触发原因和断电后的恢复动作。

- [ ] 不依赖单个可被半写的布尔标志。
- [ ] 每次启动递增trial启动计数。
- [ ] trial应用在健康运行一段时间并完成关键自检后才确认。
- [ ] trial连续复位或超时进入回滚。

## 9. 安装、复制与恢复

若应用从内部Flash执行，安装流程会擦除并复制应用区：

- 按内部Flash擦除单元执行；
- 分块复制并在状态页保存可恢复进度；
- 每块写后验证；
- 任意断电后Bootloader根据状态重新验证并继续或重新开始安全单元；
- 复制完成后验证整个内部镜像，再进入TRIAL。

当前H743内部Flash无法同时永久保存两个完整大应用时，真正“回滚”需要外部QSPI保留上一版或可靠重装旧包。必须在布局文档中证明容量满足，不凭概念图声称双分区。

## 10. 模型独立OTA

模型A/B槽不修改应用Flash：

1. 下载到非活动槽；
2. 校验manifest、CRC/hash和兼容字段；
3. 运行一组内置黄金输入自检；
4. 原子切换活动槽状态；
5. 新模型连续失败时回退旧槽；
6. 两槽均无效时使用固件内置规则基线。

模型manifest兼容字段与第十阶段一致。模型升级不能绕过特征版本和输入形状检查。

## 11. 看门狗和复位策略

- Bootloader长时间擦除/复制时按受控检查点喂IWDG；
- 每次喂前确认操作有进展；
- 外设超时不能无限循环；
- 记录最后升级阶段和错误地址；
- Bootloader不信任应用留下的RAM状态。

## 12. 故障注入矩阵

本机仿真：每个状态字段写入、每个镜像块和每个提交点模拟断电。

实板至少：

- [ ] 正常升级和降级各一次；
- [ ] 错产品、错硬件、错长度、错CRC/hash全部拒绝；
- [ ] 下载断连10次并续传；
- [ ] 安装期间随机断电30次；
- [ ] trial应用立即HardFault；
- [ ] trial应用不确认并被看门狗复位；
- [ ] 模型槽写入断电和模型黄金自检失败；
- [ ] 最终每种情况都能启动旧应用、安装新应用或进入可通信救援模式。

保存每次的旧版本、新版本、断电阶段、启动结果、回滚结果和日志。

## 13. 阶段出口与学习检查

通过标准：30次安装断电测试无变砖，错误包全部拒绝，trial失败可回滚，模型独立升级不影响应用启动。

你应能解释：

- 校验、认证和授权有什么区别？
- 为什么应用确认不能在刚进入main时立即执行？
- 为什么VTOR、MSP和中断清理是跳转关键？
- 内部Flash单应用布局如何利用外部QSPI完成回滚？
- 状态页为何也需要掉电一致性？

下一步：[12_CAN_FD_ISOTP与故障恢复.md](12_CAN_FD_ISOTP与故障恢复.md)
