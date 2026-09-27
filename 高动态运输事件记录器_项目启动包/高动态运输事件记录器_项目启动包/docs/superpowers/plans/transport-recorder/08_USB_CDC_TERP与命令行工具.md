# USB CDC、TERP与命令行工具 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立可版本化、可校验、可从乱码和断连中恢复的正式设备通信链路，并先用命令行工具证明协议正确。

**Architecture:** USB CDC和UART只是字节传输通道；TERP负责帧、版本、序号、CRC和消息语义；C与Python共用黄金向量。GUI只能调用Python协议库，不能另写一套解析。

**Tech Stack:** STM32 USB Device CDC、UART、TERP v1、CRC32、C11、Python 3.12、pyserial、pytest。

---

## 0. 开发前Worktree门禁

在修改USB、UART、TERP帧或消息语义前，先阅读仓库根目录的[AGENTS.md](../../../../../../AGENTS.md)，并创建本阶段专用工作树：

```powershell
git rev-parse --is-inside-work-tree
git status --short
git worktree add ..\高动态运输事件记录器-worktrees\terp-usb -b feature/terp-usb main
Set-Location ..\高动态运输事件记录器-worktrees\terp-usb
git branch --show-current
git status --short
```

只有第一条输出`true`、当前分支为`feature/terp-usb`且两次状态检查均无输出时，才允许开发。若Git无效、`main`不存在、目录或分支已存在，立即停止并先运行`git worktree list`排查；不得删除或硬重置来绕过。协议字节布局变化必须同步黄金向量和兼容性说明，提交、推送和PR仍需显式授权。

## 1. 传输通道顺序

1. 先在UART上验证TERP流式解析；
2. 再启用USB CDC并复用同一TERP层；
3. UART保留为救援与调试通道；
4. 协议日志与FinSH文本不能混在同一字节流，除非有明确复用层。

USB枚举成功不代表协议可靠，串口工具能收字符也不代表帧边界正确。

## 2. USB CDC板级配置

- [ ] 从原理图确认USB D+/D-、VBUS检测、连接器和ESD器件。
- [ ] 确认48 MHz USB时钟来源和误差。
- [ ] 配置唯一的VID/PID策略；开发阶段可使用合法的开发用途方案，不冒用他人量产VID/PID。
- [ ] 字符串描述符包含产品名、固件版本和基于芯片UID的序列号。
- [ ] Windows设备管理器中反复插拔20次均正常枚举。
- [ ] USB断开时发送任务不永久阻塞。

把枚举截图、设备实例ID和20次结果保存到 `evidence/phase08/usb-enumeration.md`。

## 3. TERP v1帧格式

在 `docs/protocol/terp_v1.md` 写逐字节偏移表。建议字段：

```text
sync           2 bytes
version        u8
header_length  u8
message_type   u16 little-endian
flags          u16
sequence       u32
payload_length u32
header_crc32   u32
payload        N bytes
payload_crc32  u32
```

必须固定：

- 最大payload长度；
- 字节序；
- CRC算法参数与覆盖范围；
- 请求、响应、通知和错误的对应关系；
- 超时与重试；
- 未知版本、未知消息、非法长度的处理；
- 序号回绕；
- 是否允许压缩，v1默认不允许。

同步字不能替代长度与CRC；CRC提供误码检测，不提供身份认证。

## 4. 流式解析器

解析器必须接受任意分块输入：一次一个字节、半帧、多帧相连都应正确。

- [ ] 找同步字，不足头长度则保留状态。
- [ ] 验证版本、头长、payload上限和头CRC。
- [ ] 收齐payload与CRC后才交付消息。
- [ ] 错误时丢弃最少必要字节并重新同步。
- [ ] 设最大缓存，持续垃圾输入不能无限占RAM。
- [ ] 统计CRC错、长度错、未知消息、重同步字节和超时。

测试输入：空流、逐字节、随机切片、粘包、前置垃圾、截断帧、错误同步、超长长度、头CRC错、payload CRC错和1000轮随机破坏。

## 5. C/Python黄金向量

```text
protocol/
  terp_messages.yaml
  golden/
    get_device_info_request.bin
    get_device_info_response.bin
    event_chunk.bin
firmware/components/protocol/
  terp_codec.c
  terp_parser.c
host/transport_recorder/protocol/
  codec.py
  parser.py
host/tests/
  test_protocol_golden.py
  test_protocol_fuzz_cases.py
```

- [ ] 手工计算一个最小帧的每个字段和CRC，写在协议文档中。
- [ ] C编码、Python解码同一黄金文件。
- [ ] Python编码、C解码同一黄金文件。
- [ ] 修改黄金文件一个字节，双方必须拒绝。
- [ ] `terp_messages.yaml` 只描述消息ID和字段，生成代码需可复现；核心解析逻辑仍亲手实现和测试。

## 6. v1消息集合

第一版只实现：

| 消息 | 用途 |
|---|---|
| HELLO | 协商协议和设备能力 |
| GET_DEVICE_INFO | 型号、固件、硬件、序列号 |
| GET_HEALTH | 健康快照 |
| GET_TIME / SET_TIME | UTC管理 |
| LIST_EVENTS | 分页列出事件 |
| GET_EVENT_INFO | 事件元数据 |
| READ_EVENT_CHUNK | 分块读取 |
| DELETE_EVENT | 显式删除单条记录 |
| START_LIVE / STOP_LIVE | 低速预览，不替代原始记录 |
| ERROR | 结构化错误响应 |

删除和时间设置必须是请求/响应操作，不能用无确认通知。

## 7. 大事件分块、重试与续传

- [ ] 主机指定 `event_id + offset + requested_length`。
- [ ] 设备返回实际offset、长度、总长度和块CRC。
- [ ] 重复请求同一块返回相同内容，保证幂等。
- [ ] 主机写入临时文件，并维护已验证范围。
- [ ] 断连后重新HELLO和GET_EVENT_INFO，再从最后完整块续传。
- [ ] 完整文件CRC通过后原子重命名为正式文件。
- [ ] 不因下载成功自动删除设备记录。

## 8. Python协议库与CLI

CLI入口建议：

```powershell
.\.venv\Scripts\python.exe -m transport_recorder.cli ports
.\.venv\Scripts\python.exe -m transport_recorder.cli info --port COM7
.\.venv\Scripts\python.exe -m transport_recorder.cli health --port COM7
.\.venv\Scripts\python.exe -m transport_recorder.cli events list --port COM7
.\.venv\Scripts\python.exe -m transport_recorder.cli events download --port COM7 --id 42 --output data/events/42.terp-event
```

CLI要求：

- 默认输出人可读，`--json` 输出稳定机器格式；
- 超时、设备拒绝、CRC、版本不兼容有不同退出码；
- `--verbose` 打印帧摘要但不倾倒超大payload；
- 端口自动发现只做建议，实际操作显示使用的序列号；
- Python库没有PySide6依赖。

## 9. 连接状态机

主机状态：

```text
DISCONNECTED -> OPENING -> HANDSHAKING -> READY -> TRANSFERRING
                     \-> INCOMPATIBLE
任意状态 -> ERROR -> DISCONNECTED
```

- [ ] 每个状态有进入、退出、超时和用户取消处理。
- [ ] 不把Windows COM端口存在等同于设备就绪。
- [ ] HELLO确认协议版本和最大块长度。
- [ ] 设备复位后旧请求序号作废，主机重新握手。

## 10. 验收

- [ ] UART与USB CDC均通过同一黄金向量测试。
- [ ] Windows反复插拔20次后CLI仍能重连。
- [ ] 10 MB模拟事件传输CRC正确。
- [ ] 传输过程中随机断连20次均可续传。
- [ ] 1000轮帧破坏不会让设备崩溃或解析器无限等待。
- [ ] 未知消息返回结构化错误，后续正常消息仍可处理。

你应能解释：帧、消息和传输通道的区别；为什么需要头CRC和payload CRC；为什么CLI应先于GUI；为什么重试操作要幂等；CRC为什么不是安全认证。

下一步：[09_PySide6上位机与数据回放.md](09_PySide6上位机与数据回放.md)
