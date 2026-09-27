# 无硬件 A 类审查问题修复设计

## 目标

在不依赖开发板、传感器、真实 SPI 时序或物理链路的前提下，修复当前审查中能够由确定性软件测试证明正确的缺陷。修复完成后，硬件返还阶段只验证 BSP、吞吐和电气行为，不再重新定义事件语义、存储安全边界或主机状态机。

本设计采用向后兼容的 EV03。EV01 和 EV02 继续可读；新固件只写 EV03。

## 范围

### 本次包含

1. 块内触发样本边界、触发序号和单调时间。
2. 原始样本丢失的可追踪性，以及 EV03 `DATA_LOSS` 证据。
3. 不可绕过的日志格式化二次确认。
4. 完整 JEDEC 器件匹配和由器件表派生的几何参数。
5. EL01 查询的有界元数据索引，避免分页和分块读取反复校验历史 payload。
6. health/IMU 一致快照和 32 位 RT-Thread tick 回绕扩展。
7. 桌面设备会话串行化、错误状态、分页、标签/备注和缺口警告。
8. 项目源码编译告警门禁，以及新增行为的真实回归测试。

### 明确排除

- SPI 分频、擦除时延预算和是否引入异步 storage writer。
- RT-Thread 任务优先级、栈大小、FIFO 水位和 DMA 块大小。
- RTC/LSE/VBAT、I2C/SHT4x、PVD/IWDG 的板级接线与时序。
- USB CDC、CAN FD、ISO-TP、Bootloader、OTA 和 TinyML。
- 两套 BSP 的公共化重构或旧 BSP 删除。

## 设计原则

- 不改变 EL01 的原子提交顺序、Flash 分区或掉电恢复规则。
- 不用动态内存承载事件、索引或高频数据。
- 测试调用真实生产接口；fake NOR 只替代物理介质，不替代 event log 算法。
- 每个生产行为先由一个能在旧代码上正确失败的测试定义。
- 不用文件长度驱动重构，只处理已经有错误证据的边界。

## 事件序号、时间和块内触发

`sample_block_t.sequence` 改为该块第一个样本的全局样本序号，而不是“成功发布块号 × 64”。采集端在每次获得 FIFO 样本后都推进全局样本序号；即使样本池耗尽或 publish 失败，下一成功块的序号也会形成真实缺口。

块记录 `first_monotonic_us` 和 `sample_period_ns`。触发事实增加块内 `sample_index`。事件的精确触发时间为：

```text
block.first_monotonic_us + sample_index * sample_period_ns / 1000
```

导出时 `pretrigger_samples` 等于历史完整块样本数加触发块中位于触发样本之前的样本数；`posttrigger_samples` 包含触发样本及其后的样本。于是触发样本的 payload 索引严格等于 `pretrigger_samples`，不需要复制或拆分样本块。

## EV03 数据丢失格式

EV03 保留 EV02 偏移 `0..127` 的字段语义，修改：

- magic：`EV03`
- format version：`3`
- header length：`160`

新增固定扩展：

| 偏移 | 长度 | 字段 | 规则 |
|---:|---:|---|---|
| 128 | 4 | `lost_sample_count` | 本事件 payload 时间范围内缺失的原始样本总数 |
| 132 | 4 | `first_lost_sequence` | 首个缺失样本序号；无缺失时为 0 |
| 136 | 4 | `last_lost_sequence` | 最后一个缺失样本序号；无缺失时为 0 |
| 140 | 4 | `loss_episode_count` | 不连续缺口段数量 |
| 144 | 8 | `first_loss_monotonic_us` | 第一段缺口起点；无缺失时为 0 |
| 152 | 8 | `last_loss_monotonic_us` | 最后一段缺口终点；无缺失时为 0 |

event flags 新增 `DATA_LOSS` 位。导出器通过相邻块的样本序号和时间计算缺口；不依赖全局隐藏状态。若序号回绕，使用无符号差值，只有差值处于有效事件窗口上限内才接受为缺口，否则拒绝导出为损坏状态。

EL01 recovery 接受 EV01、EV02 和 EV03，并按各自固定头长、payload 长度和内部 CRC 校验。TERP 不改变帧结构，只把 EV03 当作事件原始字节传输。Python parser、repository 和 UI 同时支持三个版本。

## 存储安全边界

裸 `storage_service_format()` 不再是公共 API。storage service 只公开：

```text
storage_service_request_format(now)
storage_service_confirm_format(now)
```

确认必须发生在有效窗口内，并消费 pending 状态；失败、超时和成功后都不能复用。真正擦除函数保持文件内私有，因此未来 MSH、TERP 或其他模块不能绕过服务门禁。

W25Q 驱动使用器件表匹配完整三字节 JEDEC ID。当前只接受项目文档指定的 Winbond W25Q64 `EF 40 17`，容量、页大小和扇区大小从表项写入设备结构；未知厂商、类型或容量一律拒绝。该软件白名单可由 fake transport 完整验证，实板回来后仍需读取真实 ID 作为硬件门禁。

## EL01 查询复杂度

挂载后建立固定大小的稀疏元数据索引：每 16 条已提交记录保存 `event_id` 和 `record_offset`。8 MiB、4 KiB 最小跨度下最多需要 128 个索引项，内存有明确上界。

查询路径拆为：

1. recovery/verify 使用完整记录和 payload CRC 校验；
2. 已挂载日志的 list/get/read 使用已提交区间内的元数据读取；
3. 随机查找从最近稀疏索引点开始，最多遍历 15 个记录头；
4. 最近一次事件信息缓存供连续 `READ_EVENT_CHUNK` 复用；
5. LIST_EVENTS 使用一次顺序遍历填充一页，不为每个 ID 重新从头查找。

fake NOR 增加读取字节数和调用次数统计。测试必须断言查询成本上界，而不是仅测结果相等。完整事件 CRC 仍在挂载、显式 verify 和主机下载完成时验证，不在每个 4 KiB chunk 前重复读取整个历史。

## 一致快照和单调时钟

health snapshot 使用服务内部 mutex 发布和复制完整结构。health 更新频率低，事件触发和 TERP 查询只短暂复制，不在 IMU ISR 或 DMA 高频路径持有 mutex。

IMU stats getter 在单核 MCU 上使用短中断临界区复制计数器，覆盖任务和 ISR 写者；临界区只包含固定数量标量复制。

新增纯逻辑 32 位 tick 扩展器，记录上次 tick 和高位 epoch。运行时在短临界区内观察 `rt_tick_get()` 并返回 64 位微秒。测试直接注入 `0xFFFFFFFE, 0xFFFFFFFF, 0, 1`，证明输出严格单调。所有新事件块和 health 计时使用该统一时钟。

## 主机状态和桌面功能

一个 `DeviceSession` 同一时刻只允许一个设备操作。session 用锁保护状态转移；不允许在 `TRANSFERRING` 时启动 list、connect 或第二个 download。UI 在设备 worker 运行期间禁用连接和下载操作，完成后按真实 session 状态恢复。

只有用户取消下载可以回到 READY。协议错误、CRC 错误、传输失败或重连失败必须关闭不可用 client 并进入 ERROR/DISCONNECTED，不能伪装 READY。

Phase 09 的纯软件缺口按现有设计补齐：

- LIST_EVENTS 使用 `next_event_id` 追加分页，不清空上一页；
- 本地仓库事件可以编辑标签和备注；
- JSON 导出读取真实 annotation；
- EV03 `DATA_LOSS`、UTC 无效、饱和和存储错误形成可见警告；
- 使用 simulated device 和 pytest-qt 覆盖重复操作、错误恢复和分页。

## 测试策略

### C 原生测试

- 块首、块中、块尾触发，检查 payload 触发索引和微秒时间。
- 样本池耗尽和 publish 失败后序号形成缺口，EV03 精确记录多个 loss episode。
- EV01/EV02/EV03 recovery 和损坏字段拒绝。
- 未 request、超时、重复 confirm 和成功消费后的格式化拒绝。
- JEDEC 正确 ID 通过，三个字节分别错误时拒绝。
- 78 条最大事件和 2046 条最小记录的索引边界；list/read 的 fake NOR 读取次数受限。
- tick 回绕和 health 快照一致性。

### Python/Qt 测试

- EV03 fixture 的解析、CRC、loss 字段和警告。
- 两个并发设备操作中第二个被明确拒绝。
- 取消后 READY；协议/CRC/传输错误后不是 READY。
- 多页事件追加、annotation 编辑和 JSON 导出保持。
- 所有测试从当前工作树 editable install 导入，不引用主工作区。

### 完成门禁

```powershell
pwsh scripts/test_native.ps1
$env:QT_QPA_PLATFORM='offscreen'; .\.venv\Scripts\python.exe -m pytest host/tests -q
.\.venv\Scripts\python.exe protocol/generate_messages.py --check
.\.venv\Scripts\python.exe protocol/golden/generate.py --check
pwsh scripts/build_firmware.ps1 -RequireElf
git diff --check
```

新增测试必须在对应生产修改前运行并以预期断言失败。最终固件构建只证明软件和内存布局，不作为 SPI、RTC、USB 或长期运行硬件证据。

## 兼容与迁移

- 已存储的 EV01/EV02 不迁移、不重写。
- 新固件挂载旧 EL01 日志后继续读取旧事件，新写入记录使用 EV03。
- 主机同时读取 EV01、EV02、EV03；未知版本继续拒绝。
- EL01 分区、记录提交协议、TERP 帧和已有消息 ID 不变。

## 完成定义

本设计完成只表示 A 类软件缺陷由失败优先的自动测试、完整主机测试和固件构建关闭。SPI 吞吐、真实 JEDEC、掉电、采集压力、RTC、USB、CAN 和 OTA 仍保持未验证，不得在报告中标记通过。
