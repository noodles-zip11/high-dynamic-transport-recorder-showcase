# 当前黑匣子 ICM 姿态只读探针

日期：2026-08-16
目的：把当前连接的 ICM45686 输出作为 AI 数据的传感器正方向基准；不烧录、不复位、不改设备参数。

## 连接与设备身份

- 串口：`COM8`（USB-SERIAL CH340）
- `info`：`STM32H743` / `openmv4-h743-pd8-pd9` / `phase08-terp-uart3`
- `health`：`storage_ready=true`，`storage_error_count=0`，`event_export_error_count=0`
- 固件采集基线：`main` revision `203bfaa55ab905375c640216e78b8d438e25a89c`

## 原始事件读取

通过只读 TERP `events list`/`events download` 读取设备中已有事件，并用
`host.transport_recorder.analysis.event_record.load_event` 完整校验：

| event | 格式 | 样本 | 采样率 | 丢样 | IMU 传输/DMA 错误 | 静止段 accel 平均 (counts) | 临时下载 SHA-256 |
|---:|---:|---:|---:|---:|---:|---|---|
| 1 | EV02 | 2400 | 1600 Hz | 0 | 0 / 0 | `(-70,-72,+2058)` | `05D43F7F53226B21B792EB76601BE4D7971687C73D54C933DDBC8F934D8CEC90` |
| 2 | EV03 | 2400 | 1600 Hz | 0 | 0 / 0 | `(+28,-67,+2059)` | `DB96D6E27F887B18E8F5250BA825B1F93632E8D876056DCC79A6416CFC143E54` |

16g 量程下约 2048 counts 为 1g；两份静止段的 Z 轴均约为 `+1g`，所以当前安装
的 ICM `Z+` 朝箱盖/上方。X/Y 静止值接近零，不能由重力单独判定水平正方向。

## 冻结结论

1. 数据列 `ax/ay/az` 永远表示 ICM 芯片自身的 `+X/+Y/+Z`，不做隐式轴交换或取反。
2. 当前箱体映射固定为 `box_bottom_right_xright_yrear_zup`：模块在箱底右下角，芯片面朝箱盖，X+ 向右侧壁，Y+ 向后/铰链侧，Z+ 向箱盖。
3. 在第一次正式采集前，必须按此映射给模块或固定板贴 X+/Y+/Z+ 标记并拍照。若实物方向改变，创建新的 `mounting_id`。

## 限定

这次读取的是设备中已经保存的事件，不是新增实时三轴命令；因此它验证了当前数据轴格式
和 Z+ 重力方向，但不能替代第一次正式 session 的安装照片，也不能单独证明 X+/Y+ 的
物理指向。当前面包板/杜邦线结构只允许受控轻放、轻敲和轻微搬运，不用于高能碰撞或跌落。
