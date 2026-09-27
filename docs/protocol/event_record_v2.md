# EV02 事件记录格式 v2

## 适用范围

EV02 是 Phase 07 写入 EL01 日志的新事件 payload。EL01 的原子提交、外层
CRC 和恢复机制不变；EV01 记录继续可读。所有多字节字段均为 little-endian。

EV02 现为只读兼容格式。采用 EV03 的固件继续读取已有 EV01/EV02，但新事件只
写 EV03；扩展字段见 `event_record_v3.md`。

记录由固定 128 字节头和 16 字节样本 payload 组成。接收端必须先验证头长度、
样本数、payload 长度和 CRC32，才可以解释样本。

## 固定头（128 字节）

| 偏移 | 长度 | 类型 | 字段 |
|---:|---:|---|---|
| 0 | 4 | bytes | ASCII `EV02` |
| 4 | 2 | u16 | `format_version = 2` |
| 6 | 2 | u16 | `header_length = 128` |
| 8 | 4 | u32 | `event_id` |
| 12 | 8 | u64 | `trigger_monotonic_us` |
| 20 | 8 | i64 | `utc_unix_seconds`（仅 UTC 有效位为 1 时可信） |
| 28 | 4 | u32 | `time_epoch_id` |
| 32 | 4 | u32 | `sample_rate_hz` |
| 36 | 4 | u32 | `trigger_sequence` |
| 40 | 4 | u32 | `pretrigger_samples` |
| 44 | 4 | u32 | `posttrigger_samples` |
| 48 | 2 | u16 | `subtrigger_count` |
| 50 | 2 | u16 | event flags（沿用 EV01） |
| 52 | 4 | u32 | `peak_magnitude_sq` |
| 56 | 4 | u32 | `threshold_magnitude_sq` |
| 60 | 4 | u32 | `payload_length` |
| 64 | 4 | u32 | payload IEEE CRC-32 |
| 68 | 1 | u8 | health state |
| 69 | 1 | u8 | power state |
| 70 | 2 | u16 | context-valid flags |
| 72 | 4 | u32 | raw reset flags |
| 76 | 2 | i16 | temperature in centi-degrees Celsius |
| 78 | 2 | u16 | environment age in seconds, saturated |
| 80 | 4 | u32 | humidity in milli-percent RH |
| 84 | 4 | u32 | free log bytes |
| 88 | 2 | u16 | minimum free sample blocks |
| 90 | 2 | u16 | reserved, zero |
| 92 | 4 | u32 | last health fault code |
| 96 | 4 | u32 | IMU transport errors |
| 100 | 4 | u32 | IMU DMA errors |
| 104 | 4 | u32 | sample-pool backpressure count |
| 108 | 4 | u32 | storage errors |
| 112 | 4 | u32 | event-export errors |
| 116 | 8 | four u16 | acquisition/event/storage/health stack minimum free; zero when unavailable |
| 124 | 4 | u32 | health transition sequence |

### context-valid flags

| Bit | Meaning |
|---:|---|
| 0 | UTC valid |
| 1 | environment value valid |
| 2 | environment value fresh |
| 3 | LSE running |
| 4 | storage ready |
| 5 | power state known |
| 6 | stack-watermark values available |

Bits 7 through 15 are reserved and must be zero. In Phase 07, stack-watermark
values are zero and bit 6 is clear.

## Payload samples and validation

Each payload sample uses the unchanged EV01 16-byte sample layout. The payload
length must equal `(pretrigger_samples + posttrigger_samples) * 16`, and the
complete record length must equal `128 + payload_length`.

On-device EL01 recovery validates EV02 magic, version, fixed header length,
event ID, sample-derived payload length and inner payload CRC, in addition to
EL01 commit state and outer CRC. Unknown event versions are rejected during
recovery rather than treated as opaque data.
