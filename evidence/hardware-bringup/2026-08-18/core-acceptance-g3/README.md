# G3 并发与长稳验收 — 2026-08-18

状态：**CURRENT CORE: 2H PREACCEPTANCE PENDING / POST-CORE RELIABILITY DEFERRED**。

当前核心交付只保留 2 小时并发预验收。13 次物理断连补测和 72 小时连续运行均降级为后续可靠性验证，
不阻塞本轮收尾；G3 的物理断连/重连子门与长稳是分开的证据项。

## 仍需的硬件操作

### 物理断连/重连补足到 20 轮（后续可靠性验证）

历史证据报告已有 7 次真实 USB-TTL 拔插，但其中存在端口枚举竞争记录；同一串口句柄重开不算物理拔插。按台账还需要补足到合同累计 20 轮，即计划新增 13 轮。

每轮由操作者实际拔下并插回 USB-TTL，板卡主电源保持开启；完成后我执行 UART3 `info`、`health`、`events list` 和同一事件下载，核对长度、设备 CRC 与 SHA-256。该操作不是 G2 断电，不能切断板卡主电源。

正式矩阵禁止使用 `scripts/terp_physical_matrix.ps1 -SkipPhysicalPrompt`；该参数只能生成 `DIAGNOSTIC_NOT_PHYSICAL`，不能计入 20 轮合同。正式每轮必须留下 `physical_confirmation=true`。

当前一根 USB-TTL 不能同时连接 UART1/FinSH 与 UART3/TERP。若要在长稳阶段同时触发测试事件并执行 TERP 下载，建议准备第二根 3.3 V USB-TTL；单根线反复换接不能证明“并发”门禁。

### 当前核心：2 小时并发预验收

执行时必须同时运行 1.6 kHz 采集、U2 写入、AI 推理、TERP 下载/结果查询和模型切换，记录每 60 秒的
健康、事件、结果、错误、pool minimum、backpressure、任务栈高水位和 AI queue drop。判定要求无
HardFault/assert/意外复位，EV03 丢样和 AI queue drop 为 0，无 pool/栈耗尽，所有已接受事件和结果可追溯。

### 72 小时长稳（后续可靠性验证）

恢复执行时，保持同一最终镜像和接线连续运行 72 小时；期间不以软件复位代替连续运行，不以 10 分钟 smoke 替代长稳。

## 后续执行的项目

- 13 次物理断连补测：`DEFERRED_POST_CORE`。
- 72 小时连续运行：`DEFERRED_POST_CORE`。
- 这些项目不计入当前核心交付；当前核心只在 2 小时预验收通过后收尾。

正式证据文件：`metadata.json`、`summary.md`、物理拔插逐轮记录、原始 UART 日志、60 秒心跳日志、机器判定和 `SHA256SUMS.txt`。
