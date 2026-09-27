# V1 App 模块布局与 RT-Thread 运行架构

> 状态：V1 当前架构说明，2026-08-26 按 `firmware/app/SConscript` 和
> `firmware/app/runtime/app_runtime.c` 复核。本文取代只列五个目录和四个早期任务的旧快照；
> 代码、链接脚本和协议合同仍是实现层事实来源。

## 分层原则

`firmware/app` 只做产品服务和启动编排；可复用的介质格式、协议与 OTA 原语位于
`firmware/components`，器件访问位于 `firmware/drivers`，板级引脚、时钟、IRQ、DMA 和
WFI 原语位于 `firmware/bsp`。`main.c` 只调用 `app_runtime_start()`。

```text
firmware/app/
├─ main.c
├─ acquisition/    # ICM45686 FIFO/DMA、32-sample 发布块和采集统计
├─ pipeline/       # 192-block 固定池、状态和引用计数
├─ event/          # 自然触发、pretrigger、固定 EV03 组装和导出
├─ storage/        # U2 EL01 日志服务和 event_export_sink_t 适配
├─ ai_inference/   # 特征、四分类推理、结果队列和 U3 sidecar
├─ transport/      # UART3、TERP 会话、事件下载及固件/模型 OTA 编排
├─ ota/            # 应用/模型候选下载和生命周期适配
├─ power/          # WFI policy、mode、blocker 和 idle hook
├─ environment/    # SHT4x 环境快照服务
├─ health/         # 跨服务健康聚合和降级状态
├─ reset_power/    # reset reason 与电源诊断
├─ time/           # 单调时间及 RTC/epoch 服务边界
├─ diagnostics/    # FinSH、报告、HIL 和 UART3 echo 诊断入口
└─ runtime/        # 唯一的产品启动编排入口
```

目录按稳定业务职责划分，不按 C 文件类型划分。新任务的 `entry()`、信号量、队列和错误处理
继续由拥有状态的模块管理；`runtime` 只能调用公开 `*_start()`/绑定接口，不能吸收模块私有循环。

## 正常产品镜像的启动顺序

`app_runtime_start()` 的顺序具有依赖含义：

1. `power_runtime_start()` 安装 idle hook；失败时保留 `FAULT_FALLBACK`。
2. 初始化 health，并把 health provider 绑定给 event/TERP。
3. 启动 UART3/TERP，使维护和下载入口可诊断。
4. 选择经校验的 U3 runtime model 或内置四分类模型，启动 AI 和结果 sidecar。
5. 启动 U2 storage，并取得注入 event service 的 `event_export_sink_t`。
6. 启动 `imu_acq`；只有采集成功后才启动 `event`。
7. power、acquisition、event 都成功后进入 `MONITOR`，否则进入 `FAULT_FALLBACK`。
8. 初始化状态 LED，并启动 `sysreport`。

`TRANSPORT_PHASE11_QSPI_HIL`、`TRANSPORT_PHASE11_UART3_ECHO` 和
`TRANSPORT_PHASE11_OTA_ONLY` 是互斥诊断镜像分支，不代表正常产品镜像同时运行这些任务。

## V1 任务和阻塞方式

RT-Thread 数字越小优先级越高。

| 任务 | 优先级 | 栈 | 所有者 | 正常等待/唤醒 |
|---|---:|---:|---|---|
| `imu_acq` | 8 | 2048 B | `acquisition` | FIFO watermark semaphore；DMA 完成/超时 |
| `event` | 9 | 2048 B | `event` | READY block semaphore；固定事件完成后同步导出 |
| `sysreport` | 12 | 2048 B | `runtime` | 1 s delay，60 s 输出完整报告 |
| `ai` | 15 | 2560 B | `ai_inference` | 当前空闲时 10 ms 轮询；这是后续信号量化边界 |
| `terp_rx` | 18 | 4096 B | `transport` | UART RX semaphore 和 1 s 帧间超时 |
| `u3_echo` | 18 | 512 B | `diagnostics` | 仅 UART3 echo 诊断镜像 |

`storage` 当前没有独立 writer 线程；`power` 通过 RT-Thread idle hook 执行普通 Cortex-M7
WFI；environment、health、reset/time 主要提供同步快照或由 `sysreport` 周期评估。

## 数据与所有权

1. `acquisition` 是 sample block 的唯一生产者：`FREE -> FILLING -> READY`。
2. `event` 取得 READY block，pretrigger 和 active event 通过引用计数持有 block；资源不足必须
   显式拒绝，不能覆盖仍被引用的数据。
3. 固定事件为 25 个 pretrigger block + 50 个 post-trigger block，共 2400 samples。事件完成
   后，AI 提交路径为自己的异步处理额外 retain 引用。
4. `event` 在 event mutex 内通过注入的 sink 同步写 U2。192-block pool 中的 64-block export
   headroom 是 V1 对该同步设计的容量保护，不等同于异步 writer。
5. 写入成功或失败恢复后，export 路径释放自己的 block；AI worker 完成推理和结果排队后释放
   AI 引用。TERP 的事件下载和 AI 查询路径只读取已提交日志/sidecar，不直接访问 live pool；
   同一 transport 模块通过独立公开服务处理 OTA 和时间设置。

若实测 Flash 延迟、IRQ-off 尾延迟或 pool 水位超出预算，后续版本才引入有界
`storage_writer` 队列；届时必须重新定义 block 所有权转移、失败回滚和 power blocker 生命周期。

## 两片外部 Flash 的边界

- **U2 / SPI2 / W25Q64：** EL01 事件日志，保存 EV03；格式、commit marker、CRC、恢复和容量见
  `docs/storage/event_log_v1.md`。
- **U3 / QSPI / W25Q64：** OTA metadata、AI result sidecar、应用 candidate/recovery、模型 A/B
  槽。地址只能来自 `firmware/config/memory_layout.h`，不能在 app 模块重复硬编码。
- MCU 内部 Flash 保存 bootloader/application，不保存事件日志。

事件 format 是受保护的破坏性操作；OTA、模型切换和 U2/U3 写入必须分别持有对应 power
blocker。不得把 U2 事件日志和 U3 OTA/model 分区写成同一介质所有权。

## 错误降级与协议边界

- power、acquisition 或 event 启动失败会阻止进入 `MONITOR`；sysreport 创建失败也转入
  `FAULT_FALLBACK`。
- storage 未就绪时 event 不伪造成功；写失败执行 abort/recovery 并增加明确计数。
- AI 模型不可用、特征/运行时/sidecar 失败都生成可诊断状态，不回退为虚假 background。
- UART3/TERP 失败不改变事件介质格式；TERP 通过 storage/OTA/AI 的公开服务接口工作。
- `protocol/terp_messages.yaml` 固定消息 ID 和字段合同，当前生成器只生成 C/Python 消息 ID；
  payload 布局仍由跨语言 golden vectors 保护。扩展协议前必须先完成字段布局生成或校验，
  V1 不在文档修正中改变任何帧。

## 已知但不阻断 V1 的工程边界

- sample pool 仍在短关中断区扫描固定池，release/abandon 会清理 block；先用 DWT 测量最坏
  IRQ-off 周期，再决定是否改成 O(1) free/ready 索引。
- 同步 U2 exporter 已通过 V1 scoped pool-headroom 门禁，但不替代 72 小时、连续事件压力和
  100 次掉电矩阵。
- AI worker 的 10 ms 空闲轮询不影响 WFI 计数合同，但会限制真实节电潜力；后续改信号量后
  仍必须使用电流仪器验证，不能把更多 WFI 次数写成续航结论。

移动模块或改变上述任务/所有权边界时，必须同步更新本页、`firmware/app/SConscript`、native
测试、H743 构建和对应实板证据。
