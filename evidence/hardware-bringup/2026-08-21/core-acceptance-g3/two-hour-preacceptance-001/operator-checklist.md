# 操作表（收到开始指令后执行）

## A. 用户先确认

- [ ] 板卡主电源保持关闭，两个 USB-TTL 已接好。
- [ ] UART1 TTL-A：PA9/PA10/GND，TX/RX 交叉。
- [ ] UART3 TTL-B：PD8/PD9/GND，TX/RX 交叉。
- [ ] 两个 TTL 的 VCC 均悬空；两路均为 3.3 V 逻辑。
- [ ] 两个 TTL 在电脑中可见；历史端口为 UART1=`COM8`、UART3=`COM7`。
- [ ] 两小时窗口内不拔 TTL、不换线、不切断板卡主电源。

## B. 我先做（仍不上电）

- [ ] 枚举串口并记录实际端口、描述和序列号。
- [ ] 确认 `COM8`/`COM7`（或实际端口）分别只对应 UART1/UART3。
- [ ] 准备 UART1 原始日志、UART3 TERP 原始日志和 60 秒心跳目录。
- [ ] 校验本轮固件/模型哈希和工作树归因。

## C. 我通知用户上电后，用户做

- [ ] 用户给板卡上电。
- [ ] 用户只回复“已上电”；不重复断电，不操作 TTL。

## D. 上电后无计时基线

UART1/FinSH：

- [ ] 启动/横幅完整，无 HardFault、assert、stack overflow、重复复位。
- [ ] `sysinfo`
- [ ] `ps`
- [ ] `event status`
- [ ] `log status`
- [ ] `log inspect`

UART3/TERP：

- [ ] `info`
- [ ] `health`
- [ ] 完整分页 `events list`
- [ ] `MODEL_OTA_QUERY`
- [ ] 选取当时已存在事件做一次 `GET_EVENT_INFO`、完整下载、CRC/SHA 和 `GET_AI_RESULT`。

## E. 开始 T0

只有以下条件都满足才记录 T0：

- [ ] 两路 UART 身份和方向正确。
- [ ] 固件身份、设备 serial 和模型状态匹配。
- [ ] `storage_ready=true`。
- [ ] 存储/导出错误为 0。
- [ ] `model_valid=true` 且 `pending_install=false`。
- [ ] 采集计数开始增长，事件/AI 状态可读。

## F. 120 分钟窗口

- [ ] 同时保持采集、U2 写入、AI 推理和 TERP 读回。
- [ ] 每 60 秒记录 UART1 状态、UART3 health/模型/事件摘要。
- [ ] 安全触发事件并关联原始事件、CRC、下载 SHA、AI 结果和模型身份。
- [ ] 按计划执行 Model A/B 激活；每次核对 510/510、valid、not pending。
- [ ] 记录 lost samples、queue drop、pool minimum/backpressure、任务栈高水位和错误计数。
- [ ] 110 分钟后停止新增长操作，120 分钟内完成收尾。

## G. 立即停止条件

HardFault/assert/意外复位、UART 不可恢复、EV03 丢样非零、AI queue drop 非零、pool/栈耗尽、
存储/导出/特征/runtime 错误增加，均立即停止新增负载并保存原始证据。
