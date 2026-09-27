# EV03 事件记录格式 v3

## 适用范围

EV03 是新增数据丢失证据后的事件 payload。EL01 分区、原子提交、外层 CRC 和
TERP 原始字节传输均不改变。读取端继续接受 EV01、EV02；新固件只写 EV03。
所有多字节字段均为 little-endian。

记录由固定 160 字节头和 16 字节样本 payload 组成。偏移 0..127 的字段语义与
EV02 完全一致，但 magic、版本和头长度分别为 `EV03`、3、160。

## 触发样本边界

`trigger_sequence` 和 `trigger_monotonic_us` 表示真正越过阈值的那一个样本，
而不是触发样本所在块的首样本。`pretrigger_samples` 不包含触发样本，
`posttrigger_samples` 从触发样本开始计数；因此解析后的触发索引永远等于
`pretrigger_samples`。这一边界对块内第 0、中间和最后一个样本都相同。

## EV03 扩展（偏移 128..159）

| 偏移 | 长度 | 类型 | 字段 |
|---:|---:|---|---|
| 128 | 4 | u32 | `lost_sample_count` |
| 132 | 4 | u32 | `first_lost_sequence` |
| 136 | 4 | u32 | `last_lost_sequence` |
| 140 | 4 | u32 | `loss_episode_count` |
| 144 | 8 | u64 | `first_loss_monotonic_us` |
| 152 | 8 | u64 | `last_loss_monotonic_us` |

无缺失时六个字段全部为零，event flags 的 `DATA_LOSS` 位（bit 3）清零。有缺失
时 `lost_sample_count` 是事件保存块之间缺失的样本总数，episode 是不连续缺口
段数，首末序号和时间覆盖第一到最后一个缺失样本，`DATA_LOSS` 必须置位。

## 缺口计算和损坏拒绝

每个样本块的 `sequence` 表示块内第一个样本的 32 位全局序号。对于相邻块：

```text
expected = previous.sequence + previous.sample_count
gap = current.sequence - expected       # 32-bit unsigned difference
```

`gap == 0` 表示连续。非零差值只有不超过一个有效事件窗口
`EVENT_MAX_BLOCK_COUNT * SAMPLE_BLOCK_SAMPLE_CAPACITY` 时才作为缺口；更大差值视为
乱序或损坏，导出失败。该规则允许序号在事件内正常回绕，避免将倒序块解释成
巨量丢样。

缺口起点为前一块最后一个样本之后的一个采样周期位置；缺口终点为起点加
`(gap - 1) * sample_period`。所有加法在写头前做溢出检查。

## 长度和完整性

payload 长度必须等于：

```text
(pretrigger_samples + posttrigger_samples) * 16
```

完整记录长度必须等于 `160 + payload_length`。EL01 recovery 和显式 verify 同时
检查 EV03 magic、版本、固定头长、event ID、样本派生长度、payload CRC、外层
记录 CRC 和提交标记。未知版本或不一致字段必须拒绝。
