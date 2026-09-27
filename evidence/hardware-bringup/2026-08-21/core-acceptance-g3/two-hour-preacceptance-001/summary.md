# G3 两小时并发预验收 — 准备摘要

状态：**COMPLETED / STRICT G3 FAIL — POOL BACKPRESSURE OBSERVED**。

本文件记录双 UART 基线和完整 7200 秒窗口。严格 G3 没有通过，原因是运行期间
`pool_backpressure` 最高为 26；该项已按台账要求保留为阻塞项。

UART1/COM6：`sysinfo`、`ps`、`event status`、`log status`、`log inspect` 五个命令均收到
`msh >`，未见 HardFault/assert/stack overflow/watchdog reset；该结果保存在
`uart1/baseline/baseline.json`。

UART3/COM7：只读 TERP HELLO 首次 2 秒超时，使用 5 秒超时重试仍无 RX 回帧；用户重插适配器后，
完整 TERP 基线通过，结果在 `uart3/retry-after-replug/`：设备身份匹配、健康状态正常、模型有效、
39 个事件全部读回。此前的超时保留为接线/枚举恢复证据，不再阻塞 T0。

已锁定板上归因镜像：`4c47fe69b544ef9327300910d66f04927871f0dc`，Release BIN
`2F0E676507D1591060BA759D8F81C6DE472D302988172A22DEF627365B5E878A`，应用地址
`0x08020000`。本轮工作树从 `main@ee588c47ffaf07436626b00f4a3884c908a5138f` 独立创建。

Full 环境自检和当前 `main` 源码 Release 构建检查已通过；源码构建产物因构建时间/嵌入修订
与板上精确产物分开保存，未刷写设备。

两根 TTL 的目标连接为 UART1/COM8（FinSH）和 UART3/COM7（TERP），两个 VCC 断开、TX/RX
交叉、GND 共地。准备阶段端口枚举只看到蓝牙 COM3/COM4，等待适配器实际出现后再复核。

双 UART 基线已完成，T0 于 `2026-08-21T10:34:00.767179+00:00Z` 启动，并于
`2026-08-21T12:34:00.791932+00:00Z` 完成。前 10 分钟每 30 秒详细采样，之后每 60 秒保留轻量
健康/结果心跳，完整 FinSH 状态每 5 分钟补采；G2 掉电、G3 物理断连和 72 小时长稳不在本轮执行。

## 窗口结果

- `7200 s`、`130` 次心跳；双 UART 原始日志、逐帧摘要和每次心跳 JSON 均保存于 `monitor/run-001/`。
- 事件 39/40/41 的 EV03 下载、CRC/解码和 AI 结果读回通过，EV03 `lost_sample_count=0`；两次安全触发把事件数从 39 增至 41。
- A→B→A→B 四次模型激活均校验 `510/510`、有效且非 pending；UART3 未掉线。
- 未观察到 HardFault/assert/意外复位、FIFO/DMA 错误、AI queue drop、存储/导出/特征/runtime 错误；任务栈最高使用率 86%。
- `pool_backpressure` 最高 26，严格 G3 PASS 标准不满足；运行时 `sample_pool_min_free` 仍不可用，
  因此结论为 `FAIL_POOL_BACKPRESSURE_OBSERVED`，不是 PASS。

机器汇总见 `monitor/run-001/run-summary.json`，指标判定见 `monitor/run-001/metrics-summary.json`。
