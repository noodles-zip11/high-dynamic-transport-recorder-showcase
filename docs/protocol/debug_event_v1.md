# EV01 调试事件格式 v1

## 适用范围

EV01 是 Phase 05 用于串口/RTT 调试导出的临时二进制事件格式。它只服务于“采集、触发、导出、PC 回放”的最小闭环；不能作为 SPI Flash 长期日志格式、GUI 公共协议或后续 TERP 帧格式。

所有多字节整数均为 little-endian。接收端必须先完成完整性验证，才能解释样本。

## 文件布局

```text
+--------------------+-------------------------------+
| 64-byte header     | packed 16-byte samples...     |
+--------------------+-------------------------------+
```

`payload_length` 必须等于 `(pretrigger_samples + posttrigger_samples) * 16`，文件总长度必须等于 `64 + payload_length`。

## Header（固定 64 字节）

| 偏移 | 长度 | 类型 | 字段 | 含义 |
|---:|---:|---|---|---|
| 0 | 4 | bytes | `magic` | ASCII `EV01` |
| 4 | 2 | u16 | `format_version` | 当前为 1 |
| 6 | 2 | u16 | `header_length` | 固定为 64 |
| 8 | 4 | u32 | `event_id` | 单调递增的事件 ID |
| 12 | 8 | u64 | `trigger_monotonic_tick` | 触发块的 RT-Thread tick |
| 20 | 4 | u32 | `sample_rate_hz` | 当前为 1600 |
| 24 | 4 | u32 | `trigger_sequence` | 触发样本的全局序号 |
| 28 | 4 | u32 | `pretrigger_samples` | 触发前样本数 |
| 32 | 4 | u32 | `posttrigger_samples` | 触发块及其后样本数 |
| 36 | 2 | u16 | `subtrigger_count` | 后触发期间的再次触发次数 |
| 38 | 2 | u16 | `flags` | 事件标志位 |
| 40 | 4 | u32 | `peak_magnitude_sq` | 事件中记录的最大合加速度平方 |
| 44 | 4 | u32 | `threshold_magnitude_sq` | 触发阈值平方 |
| 48 | 4 | u32 | `payload_length` | payload 字节数 |
| 52 | 4 | u32 | `payload_crc32` | payload 的 IEEE CRC-32 |
| 56 | 8 | bytes | `reserved` | 当前全为 0，接收端忽略 |

### flags

| 位 | 名称 | 含义 |
|---:|---|---|
| 0 | `PRETRIGGER_SHORT` | 触发前历史不足 25 块 |
| 1 | `DURATION_CAPPED` | 持续触发已达到 75 个 post-trigger 块上限 |

## Payload 样本（每条固定 16 字节）

| 相对偏移 | 长度 | 类型 | 字段 |
|---:|---:|---|---|
| 0 | 2 | i16 | `accel_x` |
| 2 | 2 | i16 | `accel_y` |
| 4 | 2 | i16 | `accel_z` |
| 6 | 2 | i16 | `gyro_x` |
| 8 | 2 | i16 | `gyro_y` |
| 10 | 2 | i16 | `gyro_z` |
| 12 | 2 | u16 | `sensor_timestamp` |
| 14 | 1 | i8 | `temperature` |
| 15 | 1 | u8 | `fifo_header` |

样本按事件时间顺序排列：所有 pre-trigger 样本在前，包含触发点的块及后续样本在后。

## 接收端验证顺序

1. 文件长度至少为 64 字节，`magic` 必须为 `EV01`。
2. `format_version == 1` 且 `header_length == 64`。
3. `payload_length == 文件总长度 - 64`。
4. `payload_length == (pretrigger_samples + posttrigger_samples) * 16`。
5. 对 payload 计算 CRC-32，必须等于 `payload_crc32`。
6. 仅在以上步骤全部成功后，按 16 字节边界解析样本。

任一步失败都必须把文件视为无效事件；不得将部分 payload 当作正常采样数据展示。
