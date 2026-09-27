# SWD 运行窗口（提前停止，非 2 小时验收）

本轮因先处理需要用户操作的项目而提前停止，保留原始证据，不计为连续 2 小时通过。

- 实际采样约 `1020.015 s`（约 17 分钟），17 个 60 秒采样点。
- IMU services `13297 -> 64264`，samples `425504 -> 2056448`；FIFO/DMA/解析错误全为 `0`。
- 事件 processed blocks `13303 -> 64270`，export error `0`；U2 state `1`、committed events `100` 始终不变。
- `sysreport` TCB 栈大小始终 `2048`。
- 原始文件：`swd-runtime-raw.log`、`swd-runtime-samples.jsonl`；停止记录：`capture-abort.txt`。

稍后若继续严格验收，必须重新开始完整连续 `7200 s` 窗口；本段只能作为补充运行证据。
