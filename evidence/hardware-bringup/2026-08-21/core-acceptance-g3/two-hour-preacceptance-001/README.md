# G3 两小时并发预验收准备 — 2026-08-21

状态：**COMPLETED / STRICT G3 FAIL — POOL BACKPRESSURE OBSERVED**。

本轮已完成 7200 秒两小时并发预验收；双 UART 基线通过，但严格 G3 未关闭：
`pool_backpressure` 最高为 26，且运行时没有可用的 pool minimum 观测值。
G2 的 100 次受控掉电、G3 的物理断连补足和 72 小时连续运行仍是
`DEFERRED_POST_CORE`，不在本轮启动。

## 固定归因基线

- 本轮测试工作树：`fix/hardware-2h-run-20260821`。
- 工作树基线：`main` 当前提交
  `ee588c47ffaf07436626b00f4a3884c908a5138f`；工作树从该提交独立创建。
- 板上已通过 G0/G1 复核的 Release 镜像内嵌固件修订：
  `4c47fe69b544ef9327300910d66f04927871f0dc`。
- 精确板上镜像：128952 bytes，应用地址 `0x08020000`，BIN SHA-256
  `2F0E676507D1591060BA759D8F81C6DE472D302988172A22DEF627365B5E878A`。
- 同一已复核产物的 ELF SHA-256：
  `AC3B7CFFD5034E8D6E2669DE8D50793ED139985D4403A2D6E4BB9544FC78454E`。
- 同一已复核产物的 MAP SHA-256：
  `ADA8A0DAB124D6B584EED399E067465342327488598C4B44AED225E5F52F3089`。
- 设备：`STM32H743VIT6` / `recorder-001`；ST-Link serial
  `DEVICE_SERIAL_REDACTED__`；历史目标电压 `3.23 V`。

本轮不默认重刷。若开始前发现板上身份或镜像无法与上述记录对应，立即停在归因门，不进行
模型切换或压力运行；是否需要另做 SWD 备份/重刷必须单独确认。

## 两根 TTL 接线

两根适配器必须独立连接，两个 VCC 都断开：

| 适配器 | Windows 端口 | 板端 UART/作用 | 板端接线 |
| --- | --- | --- | --- |
| TTL-A | `COM8`（若系统重新分配，以实际枚举为准） | UART1 / FinSH：控制、事件触发、状态和栈观察 | `PA9/PA10/GND`；板 TX→TTL RX，板 RX→TTL TX，GND→GND |
| TTL-B | `COM7`（若系统重新分配，以实际枚举为准） | UART3 / TERP：健康、模型查询、事件下载、结果核对 | `PD8/PD9/GND`；板 TX→TTL RX，板 RX→TTL TX，GND→GND |

- 两路均为 `115200 8N1`，DTR/RTS 关闭或不参与控制。
- TTL 只能共地，不能给板卡供电；板卡主电源由原有电源独立提供。
- 上电前完成 TX/RX/GND 检查；两小时窗口内不换线、不拔 TTL、不切断板卡主电源。
- 端口号若不是 COM7/COM8，先记录实际端口再开始；不能把 COM 号变化当作固件故障。

## 收到“开始”后的顺序

1. 先枚举并确认两路 TTL，检查 `COM7/COM8` 或记录实际端口；此时仍不上电。
2. 开启 UART1 原始/时间戳日志和 UART3 TERP 原始日志，准备输出文件。
3. 我通知你上电；你上电后等待启动完成，我先做 UART1 `sysinfo`、`ps`、`event status`、
   `log status`、`log inspect`，再做 UART3 `info`、`health`、完整事件列表和模型查询。
4. 只有两路基线均可追溯、设备身份匹配、`storage_ready=true`、存储/导出错误为 0、模型有效且
   `pending_install=false` 时，才记录 `T0`，开始 120 分钟窗口。

## 120 分钟窗口

窗口内同时保持 1.6 kHz 采集、U2 写入、AI 推理和 TERP 读回；按源计划执行安全的事件闭环、
Model A/B 激活和每 60 秒状态记录。每个被接受的事件都关联事件 ID、设备 CRC、下载 SHA-256、
AI 结果/结果序号和模型身份。禁止 `board_flash_diagnostic=1`、`log format --confirm`、U2 擦除、
写中断掉电、物理断电和 USB-TTL 拔插压力。

达到 110 分钟后不再启动新的长操作，最晚在 120 分钟完成日志收尾。通过标准沿用台账 G3：
无 HardFault/assert/意外复位，EV03 丢样和 AI queue drop 为 0，无 pool/栈耗尽，存储/导出/
特征/runtime 错误不增加，已接受事件和 AI 结果持续可追溯。`health state=2` 仍按
`HEALTH_DEGRADED` 观测记录，不改写成整机 health PASS。

任何关键异常出现时立即停止新增负载，保留两路原始日志、最近一次完整读回和错误发生前后的
心跳，不重启测试来掩盖异常。

## 本次已准备的证据

- `firmware/normal/`：已复核的 ELF/BIN/MAP 副本及哈希。
- `logs/selfcheck-full.log`：Full 环境自检。
- `logs/build-source-check.log`：从当前 `main` 基线做的 Release 源码构建检查；该次构建因时间/嵌入修订
  字符串与板上已刷精确产物分开记录，未用于刷板。
- `logs/build-source-check-hashes.txt`：源码构建检查的产物哈希。
- `logs/ports-start-command.json`：开始前确认 `COM6`、`COM7` 均为 USB-SERIAL CH340。
- `git-status-tracked-baseline.txt`：基线提交与当前跟踪文件状态记录；本轮证据目录本身是随后新增的未跟踪内容。
- `metadata.json`：镜像、设备、端口和安全边界。
- `operator-checklist.md`、`command-runbook.md`：开始后按顺序执行的操作表和命令表。
- `monitor/run-001/`：两小时运行状态、130 个心跳 JSON、双 UART 原始日志/逐帧摘要、事件下载和模型切换证据。
- `monitor/run-001/metrics-summary.json`：严格门禁判定和指标汇总。

## 基线状态

UART1/COM6 的 FinSH 基线已通过；UART3/COM7 在首次 2 秒和复试 5 秒超时后，用户重插适配器，
再次执行完整 TERP 基线并通过：设备身份匹配、健康状态正常、模型有效、39 个事件全部读回。

T0 已于 `2026-08-21T10:34:00.767179+00:00Z` 启动，并于
`2026-08-21T12:34:00.791932+00:00Z` 完成。测试期间保持当前接线和电池供电；前 10 分钟每 30 秒
详细采样，之后每 60 秒轻量心跳，完整 FinSH 日志每 5 分钟补采。

## 实际结果

- 运行时长：`7200 s`；心跳 `130` 次（详细 `41`、轻量 `89`）。
- 事件：安全触发 2 次，事件数 `39 → 40 → 41`；EV03 事件 39/40/41 下载、CRC、解码和 AI 结果读回均通过，丢样为 0。
- 模型：A→B→A→B 四次激活均 `510/510`、`model_valid=true`、`pending_install=false`，物理槽为 `0→1→0→1`。
- 正常项：未见 HardFault/assert/意外复位；FIFO/DMA、EV03 丢样、AI queue drop、结果队列、存储导出/特征/runtime 错误均为 0；任务栈最高使用率 86%。
- 未通过项：`pool_backpressure` 计数最高 `26`，发生在事件触发期间；严格 G3 要求该项为 0，因此本轮结论为 `FAIL_POOL_BACKPRESSURE_OBSERVED`，不是 G3 PASS。
- 另有边界：`sample_pool_min_free` 运行时输入尚未接线，EV03 中的 0 是 unavailable/default，不能当作 pool 余量通过证据。

后续应先定位并修复/解释 sample pool backpressure 及 pool minimum 观测缺口，再重跑两小时门禁；本轮不再断电、换线或重刷。
