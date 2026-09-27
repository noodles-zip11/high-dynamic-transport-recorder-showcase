# U2 写入窗口与上电恢复结果

- 断电前 UART1 原始流明确出现 `event log write begins`，且同一流在信号前没有 `event log write complete`。
- 用户随后完成断电/重新上电；上电后普通固件 IMU 计数恢复，FIFO/DMA/解析/容量错误均为 0。
- U2 挂载最终恢复：`log state=1 events=1 next_id=2 next_offset=0x0000c000 jedec=ef4017`。
- `log inspect`：`recovery source=superblock_b generation=2 scanned=2 discarded=0 ticks=715`。
- `log list` 找到 event 1，`log verify 1` 返回 `event=1 verify=OK`。

## 判定

本轮证明了真实断电后的 U2 持久性、重新挂载、事件列表和 CRC 校验；`discarded=0` 且 event 1 已提交，说明断电发生在提交完成之后或未落在未提交窗口内，因此不能把本轮标成“未提交写入被丢弃”的完整通过。相关黄色边界保留，不能与软件复位恢复证据混为一谈。

证据：[断电前原始流](uart1-interruption-precut-console.log)、[上电后复核](../post-interruption-final-diagnostic-console.log)、[列表/CRC](../recovery-verify-console.log)。
