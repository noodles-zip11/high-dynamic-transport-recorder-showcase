# V1 正式版候选设计

> **历史设计快照：** 本页的“等待实施”“验收前不建 tag”描述的是 2026-08-24 的入口状态；
> 设计后来已经实施并形成 `v1.0.0`。当前结论见
> [`evidence/releases/v1.0.0/`](../../../evidence/releases/v1.0.0/README.md)，下文保留用于
> 追溯候选范围和当时的安全约束。

**日期：** 2026-08-24
**工作树：** `LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\v1-release-candidate-20260824`
**分支：** `feature/v1-release-candidate-20260824`
**基线：** `main@58477d38b700e3edad3a7d144a6916f5f574ffce`
**状态：** 用户已选择方案 1，等待按本设计实施

## 1. 目标

形成可封板验证的 V1 正式版候选固件：设备持续监测 IMU，由碰撞或自由落体自然触发事件，保存固定长度的原始事件并执行真实四分类 AI；空闲时采用已通过实板功能门禁的普通 Cortex-M7 WFI 降低 CPU 活动。

V1 的完成结论分为两个门禁：

1. 软件候选完成：独立工作树内的代码、测试、Release 构建、产物身份和回滚信息全部通过。
2. V1 验收完成：用户完成静置/书包、敲击和软垫掉落实板验证，事件可以下载、校验并取得四分类结果。

验收完成前不修改或合并受保护的 `main`，不创建 `v1.0.0` 正式标签。

## 2. 版本隔离与来源

V1 只允许在 `feature/v1-release-candidate-20260824` 工作。来源按提交或文件级审查进入，禁止合并实验工作树：

- 从 `feature/real-ai-integration-20260823` 按顺序集成四个已提交、工作树干净的提交：`6f0838f`、`9b3f88b`、`af51ff9`、`8b6b2d1`。
- 从现场测试分支只集成独立 FIFO 积压修复 `fea2d6b`。
- 低功耗来源工作树含未提交改动，不执行 merge 或整树复制；按模块和测试逐项移植并重新验证。
- 正式触发从现有 event/trigger 模块最小扩展；采集活动、按键 campaign、定时触发和现场临时分区代码均不进入 V1。

每个逻辑单元形成原子提交。提交、构建和测试只发生在 V1 分支；`main` 的 HEAD、索引和工作区内容保持原样。

## 3. 正式运行行为

### 3.1 启动与监测

设备启动后挂载既有事件日志、验证模型、启动 1.6 kHz 六轴 IMU FIFO/DMA 采集并进入 `MONITOR`。V1 不运行按键采集 campaign，不运行每五分钟触发，也不为了凑样本周期保存背景事件。

蓝色 LED 初始化后保持熄灭；它不作为持续心跳。启动失败、存储不可用等错误继续通过状态命令和现有诊断链暴露。

### 3.2 触发判定

同一监测器支持两种自然触发事实：

- `IMPACT`：合加速度不小于 `5120 counts`，连续 2 个样本。ICM45686 当前为 ±16 g、2048 counts/g，因此正式门限为 2.5 g。受控采集曾使用的 `2500 counts` 只用于提高隔箱采集命中率，不进入 V1。现场背景事件最大值为 1.911 g，确认冲击为 4.788 g 和 7.074 g，2.5 g 位于现有背景与确认冲击之间。
- `DROP`：合加速度不高于 `1536 counts`（0.75 g），连续 8 个样本；落地冲击也可通过 `IMPACT` 入口触发，再由 AI 判断为 drop 或 impact。

正式实现使用一个明确的 production trigger profile，平方运算以 64 位完成，覆盖 `INT16_MIN`，并在 profile 切换、事件完成和重新武装时清除连续计数。

触发门限必须作为编译期常量或只读配置集中定义，测试中明确 counts、g 值和连续点数，不能散落在运行时代码中。

### 3.3 单次事件与冷却

第一次自然触发后立即锁存 one-shot：事件后触发窗口内的后续峰值只更新诊断信息，不创建第二条事件，也不延长事件。

每条事件固定为 2400 个样本：25 个完整历史块（800 个样本）和 50 个从“包含首次触发点的块”开始的捕获块（1600 个样本）。触发点在该首个捕获块内的真实偏移由事件头的 `trigger_sample_index`、`trigger_sequence` 和 pre/post sample counts 精确记录，因此不能把物理触发点一律伪装成第 800 个样本。该长度与真实四分类模型的 `AI_EVENT_SAMPLE_COUNT` 合同一致；75 个块必须全部为连续的 32-sample blocks，任何短历史、序列空洞、丢样、长度异常或资源不足都必须显式失败，不得导出或伪造 AI 成功结果。

自然事件无论成功完成还是失败/无效，都必须清理已持有资源和事件状态并回到 `ARMED`，随后统一启动 30 秒安全冷却。冷却期间继续采集和维护 pretrigger 历史，但拒绝新的自然事件；冷却结束后自动清除 detector 连续计数并重新武装。测试/维护命令的手工触发能力保留，但不能绕过存储和资源安全检查。

### 3.4 存储满行为

V1 沿用主线事件日志布局与格式，不采用现场测试的临时分区。不自动覆盖最旧事件，不在后台格式化。

空间不足时：

- 已保存事件保持不变；
- 新事件不提交到 Flash；
- 记录明确的 storage/export error 与 full 状态；
- 设备继续监测和提供 UART 状态/下载能力；
- 清空只能由现有带确认的维护命令执行。

30 秒冷却用于抑制连续拖行或振动造成的写满风险，但不改变存储满的保守策略。

## 4. 真实四分类 AI

V1 运行时模型固定四类，类别顺序为：`background`、`impact`、`continuous_vibration`、`drop`。集成已完成实板门禁的真实模型、4 类运行时合同、golden vectors、OTA parser/exporter 兼容和 2560 B AI worker 栈保护。OTA validator 保留 `class_count=2..4` 的旧模型包兼容；该兼容只针对 OTA 输入，不改变 V1 运行时固定四类的合同。

AI 只处理满足固定 2400 样本合同的已完成事件。每个结果与 event ID 绑定并持久化 sidecar；模型不可用、特征失败、推理失败或结果写入失败均保持可诊断状态，不回退成虚假的 background。

现场测试已经证明采集、存储、下载和板载四分类链路可用，并识别了两个明显冲击。V1 可以据此形成产品候选，但不宣称覆盖所有运输环境的生产级泛化准确率。

## 5. 低功耗范围

V1 集成普通 Cortex-M7 Sleep/WFI：RT-Thread 无可运行线程且策略允许时执行 `clear SLEEPDEEP -> DSB -> WFI -> ISB`，由 SysTick、IMU INT1、DMA 或 UART 中断唤醒。

电源策略具有 `BOOT`、`MONITOR`、`EVENT_ACTIVE`、`MAINTENANCE` 和 `FAULT_FALLBACK` 状态，并对 DMA、事件、Flash、AI、TERP、OTA/诊断活动使用引用计数 blocker。`MAINTENANCE` 和 `FAULT_FALLBACK` 状态本身禁止 WFI；事件激活在当前状态暂不可切换时保持 `pending`，待可切换时进入 `EVENT_ACTIVE` 并获取对应 blocker。idle hook 必须常数时间、不可打印、不可动态分配、不可等待锁。

V1 同时包含：

- `power status` 的模式、睡眠、阻塞和唤醒计数；
- CPU Sleep 的可启用、可禁用和故障回退；
- LED 心跳关闭；
- 1 Hz 健康/OTA 判断保留，详细监测打印降为 60 秒；
- AI worker 2560 B 栈保护。

STOP/Standby、时钟切换、RTOS tickless、IMU Wake-on-Motion 和关闭陀螺仪均不进入 V1。未使用测流仪器前，V1 只能声明“低功耗软件和功能链完成”，不能声明节电百分比或电池续航。

## 6. 错误处理与回退

- 电源策略初始化或状态不可信：进入 sticky `FAULT_FALLBACK`，只允许普通运行，不尝试更深睡眠。
- FIFO 积压：按完整 16 B 包、每块最多 64 个样本分批排空；单次唤醒最多处理 8 块，仍有积压时自唤醒继续。
- 事件资源不足：释放已持有 block，增加资源错误计数，保持服务可继续运行。
- 固定长度不满足：事件可以保留诊断信息，但 AI submission 必须失败并记录原因。
- 存储或 sidecar 写失败：保持已提交记录一致性，通过 abort/recovery 路径恢复下一 event ID；无法恢复则停止新写入。
- UART/ST-Link 调试影响 WFI：功能测试可以连接，低功耗量化时必须断开 ST-Link；本设计不把调试器下的 WFI 计数当作节电证明。

## 7. 验证门禁

### 7.1 软件门禁

- 真实 AI 四个提交逐个集成并运行其 focused tests。
- FIFO 4112 B 积压回归：1024/1024/1024/1024/16 五块、257 samples、无 capacity error。
- trigger detector：impact 边界、drop 边界、连续点数、`INT16_MIN`、profile 重置。
- event service：one-shot、固定 2400 样本、30 秒冷却、冷却后重新武装、存储不可用时不产生伪成功。
- low power：合法状态转换、嵌套 blocker、WFI 窗口唤醒归因、AI/Flash/UART 生命周期。
- 完整 `scripts/test_native.ps1`、Host/AI pytest、bootloader/image/vector/memory-map/ICM alignment、Debug 和 Release 固件构建、`git diff --check`。
- Release 产物记录 ELF/BIN/MAP 大小与 SHA-256，固件 revision 使用 V1 候选提交，device serial 使用正式值。

### 7.2 用户实板验收

不要求再次真实路测。候选固件刷板后执行：

1. 静置和正常拿取 10 分钟，确认没有密集误触发。
2. （按 hardware checklist 可有理由 `SKIP`）放入书包正常活动约 30 分钟，确认事件数量合理、无 FIFO/DMA/pool/storage 错误。
3. 敲击外壳 10 次，每次间隔超过冷却时间；核对事件、CRC 和 AI 结果。
4. 在软垫上做 5 次低高度可控掉落，每次间隔超过冷却时间；核对 drop/impact 结果和设备完整性。
5. 下载全部新增事件，确认每条 EV03、2400 samples、event ID 连续、sidecar 可读取。
6. 读取 `power status`，确认 WFI entries/wakes 递增、blocker 无泄漏、STOP 未启用。

敲击和掉落实验用于验证触发与端到端链路，不作为新的训练集或泛化准确率证明。

本清单必做项为 1、3、4、5、6；书包约 30 分钟项目可按 hardware checklist 的允许规则有理由标记 `SKIP`，不将 `SKIP` 记为 PASS 或 FAIL。

## 8. 发布与回滚

软件门禁完成后冻结 `v1.0.0-rc1` 候选信息和固件哈希，但在用户实板验收前不创建正式标签。刷板前备份当前内部 Flash并记录板上 revision；候选失败时使用该备份或当前已验证现场固件恢复。

实板验收通过后，先审查 V1 分支相对 `main` 的完整提交列表和差异，再由用户单独授权是否合并 `main`、创建 `v1.0.0` 标签和清理旧工作树。

## 9. 明确排除

- 按键/定时/类别采集 campaign；
- 现场测试临时 Flash 分区和每五分钟调度；
- 自动循环覆盖事件；
- 深度 STOP/Standby 与 tickless；
- 未实测的节电比例、平均电流和续航承诺；
- 为提高模型指标而要求新的真实路测；
- V2 的远程通信、扩容、动态阈值或自适应学习功能。
