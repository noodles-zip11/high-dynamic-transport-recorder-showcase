# 高动态运输事件记录器：项目链路图谱

> 用途：下次复习时先看本页，不按目录逐文件翻。目标是能说清“谁通知谁、数据在哪里、谁持有它、失败后去哪”。

## 0. 总览：一条证据从硬件到可恢复记录

```text
IMU INT1
  -> FIFO watermark semaphore
  -> IMU acquisition thread
  -> SPI DMA + dma_done semaphore
  -> sample_block_pool (READY)
  -> event thread / pretrigger ring / event assembler
  -> EV03 encoder
  -> event_export_sink_t
  -> storage_service
  -> EL01 in SPI NOR
  -> next power-on: event_log_mount() recovers valid records
```

这不是连续原始数据记录器：只把一次触发前后拼成的 **event evidence** 写入 Flash。

## 1. 启动大链路：先给能力，再启动消费者

```text
firmware/app/main.c
  -> app_runtime_start()
      -> storage_service_start()
          -> U2 SPI2 init -> W25Q probe -> event_log_mount()
      -> storage_service_get_event_sink(&event_sink)
      -> imu_acquisition_start()
      -> event_service_start(&event_sink)
      -> led_hb / sysreport threads
```

`storage_service_get_event_sink()` 不写 Flash；它把 `begin/write/abort/get_status` 等函数指针装入 `event_sink`，交给事件层。事件层因此只会调用“写入能力”，不直接认识 W25Q 或 EL01。

## 2. 采集小链路：INT1 到 READY block

| 步骤 | 通知/数据 | 关键函数 | 失败去向 |
| --- | --- | --- | --- |
| 水位到达 | **通知**：`fifo_watermark_sem` | `imu_fifo_watermark_callback()` | ISR 只释放信号量。 |
| 采集线程醒来 | 读取 FIFO 字节 | `imu_acquisition_entry()` -> `imu_acquisition_service()` | 读取失败时不 publish。 |
| DMA 发起/完成 | **通知**：`dma_done_sem` | `imu_read_fifo_with_dma()` / `imu_dma_callback()` | 超时先 abort DMA；完成错误直接返回错误。 |
| 解析样本 | 数据：`imu_samples[]` | `imu_acquisition_service()` | 无有效样本时不产生 block。 |
| 发布 handoff | 数据：pool 内的 `sample_block_t` | `imu_publish_samples()` -> `sample_block_pool_publish()` | publish 失败则 abandon。 |
| 通知事件线程 | **通知**：`sample_ready_sem` | `imu_acquisition_take_ready_block()` | 线程会取最早 READY block。 |

Block 状态只记住这一行：

```text
FREE -> FILLING -> READY -> CONSUMING -> FREE
```

`sample_ready_sem` 不是 block 本身；它只是“pool 里可能有 READY block”的门铃。

## 3. 事件大链路：历史、触发、后触发、导出

```text
event_service_entry()
  -> imu_acquisition_take_ready_block()
  -> event_service_find_trigger(block, &trigger)
  -> event_assembler_consume(assembler, pool, block, trigger_or_null)
  -> state == EVENT_READY_FOR_EXPORT ?
       yes + sink ready -> event_service_export_ready_locked()
       yes + sink not ready -> event_assembler_clear()
```

### 状态和 block 去向

| 当前状态 | 输入 | block 去向 | 下一状态 |
| --- | --- | --- | --- |
| `EVENT_ARMED` | 无 trigger | `pretrigger_ring` 保存最近历史 | 仍是 armed |
| `EVENT_ARMED` | 有 trigger | 快照历史；当前 block 成为第一个 posttrigger block | `EVENT_POST_TRIGGER_CAPTURING` |
| `EVENT_POST_TRIGGER_CAPTURING` | 新 block | 加入 `event.blocks[]`；V1 自然触发已 one-shot 锁存，不接纳二次 trigger | 固定 50 个 post blocks 后 ready |
| `EVENT_READY_FOR_EXPORT` | sink 可用 | 开始 EV03 导出 | `EVENT_EXPORTING` |
| `EVENT_READY_FOR_EXPORT` | sink 不可用 | 释放 event blocks | `EVENT_ARMED` |
| `EVENT_EXPORTING` | 完成或失败 | 释放 event blocks | `EVENT_ARMED` |

### 所有权口诀

```text
CONSUMING block 的 ref = 1
交给 pretrigger ring / event record：先 retain，再 release 当前持有者
最终 export、clear 或资源中止：release 到 FREE
```

预触发环保存的是 block 的引用，不复制样本；事件开始时 `pretrigger_ring_snapshot()` 为 event 增加引用，再由 `pretrigger_ring_reset()` 交还环自己的引用。

## 4. 导出和存储：EV03 通过 sink 变成 EL01

```text
event_service_export_ready_locked()
  -> event_assembler_begin_export()
  -> event_export_debug_write(..., event_sink.write, ...)
      -> encode EV03 header (160 B)
      -> encode samples (16 B each, buffered up to 256 B)
      -> event_sink.write(encoded bytes)
  -> event_assembler_finish_export()

event_sink.write
  -> storage_service_event_write()
  -> event_log_append_write()
  -> SPI NOR page programming
```

开始导出时，`event_sink.begin(event_id, encoded_length)` 会进入 `storage_service_event_begin()` -> `event_log_append_begin()`：擦出 span，写 EL01 header，记录“正在追加”。代码中的 `ev01_length` 是为兼容冻结布局保留的历史字段名，V1 实际内层记录为 EV03。随后每个 `write()` 都只是一个分批写入成功；当最后一批 EV03 字节到达，`event_log_append_write()` 才验证内层事件、写外层 CRC/footer，并最后写 commit。

**不要混淆：** `event_export_debug_write()` 负责编码和调用回调，不直接接触 Flash；`storage_service` 才把通用字节写入落到 EL01/NOR。

## 5. 掉电恢复小链路：只接受完整证据

```text
storage_service_start()
  -> event_log_mount()
      -> 读取并选择有效 A/B superblock
      -> recover_from(saved offset, saved next ID)
      -> read_record() 逐条验证 EL01 + 内层事件 + footer/CRC/commit
```

恢复规则：

1. header 全 `0xFF`：从未写过，停止扫描。
2. 完整有效 EL01：推进 write offset、next event ID 与计数。
3. 无效或未 commit 的尾记录：不算 event，停止；下次从它的起点重写。

因此“最后一次 `append_write()` 返回成功”才可能形成 committed record；中间批次成功不等于事件已经安全保存。

## 6. 异常链路：必须能说出的三条

| 场景 | 当前代码的处理 |
| --- | --- |
| DMA 超时/完成失败 | `imu_read_fifo_with_dma()` 返回错误；采集线程不 publish block。 |
| pool 余量不足或 event 太大 | event assembler 释放已持有的 blocks，回到 armed，并计资源拒绝。 |
| sink 不可用/写失败 | 事件层 clear 或 abort/remount storage；event blocks 最终释放，避免内存永久被占。 |

## 7. 验证链路：证明什么，不能证明什么

| 证据 | 可证明 | 不能证明 |
| --- | --- | --- |
| native tests：`test_sample_block_pool`、`test_pretrigger_ring`、`test_event_assembler`、`test_event_export_debug`、`test_event_log` | 状态、引用计数、编码、模拟掉电恢复等软件逻辑 | 真正板卡的 SPI 时序、DMA/缓存、Flash JEDEC、断电波形。 |
| H743 SCons build | 固件能按当前配置编译链接 | 运行时任务时序和外设行为。 |
| 板上测试 | 真实硬件行为 | 仍需长时间与压力场景覆盖。 |

## 8. 复习顺序

1. 先从第 0 节默写总览箭头。
2. 忘记“数据在哪里”时看第 2 节；忘记“谁持有”时看第 3 节。
3. 忘记“如何写 Flash”时看第 4、5 节。
4. 最后再看异常和测试边界，绝不把 native test 当硬件验收。

相关源码：

- `firmware/app/runtime/app_runtime.c`
- `firmware/app/acquisition/imu_acquisition.c`
- `firmware/app/pipeline/sample_block_pool.c`
- `firmware/app/event/event_service.c`、`event_assembler.c`、`event_export_debug.c`
- `firmware/app/storage/storage_service.c`
- `firmware/components/storage/event_log.c`
