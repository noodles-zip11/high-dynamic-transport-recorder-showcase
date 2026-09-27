# 主链路回忆练习（10 分钟）

> 不打开源码，先完成；卡住后只查 [`project-chain-atlas.md`](project-chain-atlas.md) 对应一节。

## A. 一分钟总图

默写并在每条箭头上标一个词：`通知`、`数据` 或 `回调`。

```text
INT1 -> ______ -> IMU thread -> ______ -> sample block
     -> ______ -> event -> ______ -> EL01 -> power-on recovery
```

标准答案：

```text
INT1 -> fifo_watermark_sem -> IMU thread -> SPI DMA / dma_done_sem
     -> sample_block_pool -> event assembler -> event_export_sink_t
     -> EL01 -> event_log_mount() recovery
```

## B. 三个必须分开的概念

| 问题 | 你的答案 |
| --- | --- |
| `fifo_watermark_sem` 传的是样本，还是“水位到了”的通知？ |  |
| `sample_ready_sem` 传的是 block，还是“pool 有 READY block”的通知？ |  |
| `event_sink.write` 是 Flash 驱动，还是由 storage 注入的通用写入能力？ |  |

答案：三个都是通知/能力边界；样本在 FIFO、DMA buffer、`sample_block_t` 或编码字节 buffer 中流动。

## C. 状态机口述

不看图，补全：

```text
ARMED + 无 trigger -> ______
ARMED + trigger -> ______
后触发采够 -> ______
begin_export() -> ______
finish/clear -> ______
```

答案：更新 pretrigger 历史；`EVENT_POST_TRIGGER_CAPTURING`；`EVENT_READY_FOR_EXPORT`；`EVENT_EXPORTING`；`EVENT_ARMED`。

## D. 所有权小题

一个 `CONSUMING` block 的 `ref_count = 1`。若预触发环要保存它，正确动作是什么？

```text
______ -> ______
```

答案：`retain` -> `release`。含义是环获得自己的引用，事件线程交出自己的引用；不是无缘无故“加一又减一”。

## E. 存储小题

为什么不能把每个 READY block 直接写 NOR？请同时说出两个原因：

1. 事件证据需要 ______、______ 和 ______。
2. NOR 写入延迟会 ______ 持续高频采集。

答案：触发前历史、触发点、触发后数据；干扰/阻塞。

## F. 掉电小题

一条 EL01 已写了 header 和部分 EV03，但未写最后 commit；重启扫描时它会怎样？

答案：不算 committed event，不推进 ID/offset，停止在这条记录的起点；下一次可从此处重写。

## G. 下次复习的最短路径

1. 只读本文件 3 分钟。
2. 打开 [`project-chain-atlas.md`](project-chain-atlas.md) 核对错题。
3. 若仍不清楚，只追一个入口函数：采集看 `imu_acquisition_entry()`；事件看 `event_service_entry()`；存储看 `event_log_mount()`。
4. 不要一次打开所有文件。
