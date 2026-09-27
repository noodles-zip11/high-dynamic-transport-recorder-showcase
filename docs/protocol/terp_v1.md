# TERP v1 正式设备协议

TERP（Transport Event Recorder Protocol）是设备与主机之间的二进制协议。USB CDC 和 UART
只搬运字节；它们不得混入 FinSH/RTT 文本。GUI 只能调用 `transport_recorder` Python 库，不能
另写帧解析器。

## 帧布局

所有多字节整数为 little-endian；v1 不支持压缩。

| 偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 2 | sync | 固定 `0x54 0x52`（ASCII `TR`） |
| 2 | 1 | version | 固定 `1` |
| 3 | 1 | header_length | 固定 `20` |
| 4 | 2 | message_type | `protocol/terp_messages.yaml` 中的 ID |
| 6 | 2 | flags | `0x8000` 响应，`0x4000` 通知 |
| 8 | 4 | sequence | 请求序号；响应回显该值 |
| 12 | 4 | payload_length | `0` 至 `4096` |
| 16 | 4 | header_crc32 | 覆盖偏移 `2..15` |
| 20 | N | payload | 消息负载 |
| 20+N | 4 | payload_crc32 | 仅覆盖 payload |

CRC 使用 IEEE CRC-32：多项式 `0xEDB88320`、初值 `0xFFFFFFFF`、最终异或
`0xFFFFFFFF`。sync、`header_crc32` 和 `payload_crc32` 不在 CRC 覆盖范围内。
CRC 只能发现意外损坏，不提供身份认证、保密性或防重放。

最小 `GET_DEVICE_INFO` 请求的全部字节如下：

```text
54 52 | 01 | 14 | 02 00 | 00 00 | 01 00 00 00 | 00 00 00 00 |
FC CA 9B 18 | 00 00 00 00
sync    ver  hlen  type      flags    sequence        payload length
header CRC32 = 0x189BCAFC                         empty payload CRC32 = 0
```

该帧同时保存在 `protocol/golden/get_device_info_request.bin`。运行
`python protocol/generate_messages.py --check` 和
`python protocol/golden/generate.py --check` 可验证生成文件与向量未漂移。

## 消息与错误

消息 ID、请求字段和响应字段的唯一登记表为
[`protocol/terp_messages.yaml`](../../protocol/terp_messages.yaml)。普通请求的响应类型为
`request_type | 0x8000`。出错时返回 `ERROR (0xFFFF)`，payload 为：

```text
error_code u16 | request_message_type u16
```

`MALFORMED`、`UNSUPPORTED`、`NOT_FOUND`、`BUSY`、`INTERNAL`、`INCOMPATIBLE`、
`HANDSHAKE_REQUIRED`
彼此可区分。未知消息不会破坏解析器；设备返回结构化 `UNSUPPORTED`，随后正常消息仍可处理。

`HELLO` 请求 payload 为一个期望协议版本字节。响应返回协商版本、最大 payload、最大事件块和
能力位。主机不得把 Windows 中存在 COM 口视为设备就绪，必须完成 HELLO。
在 HELLO 成功前，除 HELLO 外的请求均返回 `HANDSHAKE_REQUIRED`；设备重启和传输会话重置
都会清除此状态。UART 适配层无法从普通 USB-TTL 线缆得知物理拔插，因此主机每次打开或
重连端口都必须重新发送 HELLO；重复的成功 HELLO 会刷新协商状态。

## 当前字节通道

首个固件通道是 OpenMV4 H743 的 `USART3`：PD8 为设备 TX，PD9 为设备 RX，AF7，使用
RT-Thread 串口默认的 115200、8N1、无流控。`terp_uart3` 的 RX 回调只释放信号量；低优先级
线程读取原始字节、驱动 parser 的 1 秒帧间超时，并调用 `terp_service`。TERP service 只经
`storage_service` 读取已提交的 EL01 事件，不接触 UART 寄存器或 Flash 格式。

该通道当前声明 DEVICE_INFO、HEALTH、EVENT_READ 三项能力。time service 尚未接进
runtime，因此 `GET_TIME` / `SET_TIME` 返回 `UNSUPPORTED`，而不是伪造时间。服务也记录
TX 成功帧数和 TX 错误数；这为未来 USB CDC 的 busy/短写诊断提供同一观察点。

## 事件读取与重试

`LIST_EVENTS` 请求为 `after_event_id u32 | maximum_count u32`。设备每页最多返回
16 条记录；`maximum_count=0` 或超过上限时同样按 16 处理。

`READ_EVENT_CHUNK` 请求为：

```text
event_id u32 | offset u32 | requested_length u32
```

响应为：

```text
event_id u32 | actual_offset u32 | total_length u32 | actual_length u32 |
chunk_crc32 u32 | data[actual_length]
```

最大请求块为 4000 字节，确保连同元数据仍小于 4096 字节 TERP payload 上限。相同
`event_id + offset + requested_length` 在事件未变时必须返回相同数据；下载成功不会删除
设备记录。主机仅在块 CRC 通过后追加 `.part` 文件，断开后重新 HELLO、复查
`GET_EVENT_INFO`，再从最后一个完整块继续；完整事件 CRC 通过后才原子改名。

序号在 `0xFFFFFFFF` 后回绕为 `1`；设备复位或主机重连后，旧请求序号不再有效。主机当前
请求超时为 2 秒；普通命令不自动重试，下载只对可幂等的读块执行重连重试。

## 存储边界

当前 EL01 日志是追加式、掉电可恢复的证据库，尚无安全的单条删除/垃圾回收格式。因此
`DELETE_EVENT` 已是正式 TERP 消息，但当前设备服务返回 `UNSUPPORTED`，而非暗中格式化或
覆盖旧证据。实现持久化删除前必须单独评审 Flash 回收、断电恢复、审计痕迹和保留策略。

`START_LIVE` / `STOP_LIVE` 的请求、响应和能力位已由通用 TERP 分发器支持，但当前
`terp_service` 不把采集样本扇出给传输层，故返回 `UNSUPPORTED`。低速预览须先单独设计
不会影响 1.6 kHz 记录链路的样本所有权、限速和背压策略，不能为了 USB 接入而复用或抢占
事件流水线的 block。

## 主机 CLI 诊断与退出码

`--verbose` 在 client 请求/响应边界向标准错误输出 TX/RX、消息类型、sequence、flags 和
payload 字节数。trace 回调收到的也是不含 payload 的只读摘要。verbose 模式不会输出帧
payload；连接成功后的独立摘要仍会显示端口和设备序列号。

| 退出码 | 含义 |
|---:|---|
| 0 | 成功 |
| 1 | 未分类的内部、文件系统或本地运行错误 |
| 2 | 传输错误或超时 |
| 3 | 设备拒绝请求，但不是版本不兼容 |
| 4 | 帧、CRC、长度或其他协议错误 |
| 5 | TERP 版本不兼容 |
