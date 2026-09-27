# Phase 05 无硬件验证记录

日期：2026-07-25
工作树：`pretrigger-event-loop`
分支：`feature/pretrigger-event-loop`

## 已验证的软件结论

| 命令 | 结果 |
|---|---|
| `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/run_tests.ps1` | 通过：12 个原生 C 测试、6 个主机 pytest、1 个 AI pytest |
| `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/build_firmware.ps1 -RequireElf` | 通过：生成 ELF、BIN、MAP；冷启动向量和内存映射检查通过 |
| `git diff --check` | 通过：无输出 |

H743 构建内存摘要：

```text
ROM:       131,632 B / 2 MiB   (6.28%)
AXI RAM:   162,576 B / 512 KiB (31.01%)
D2 SRAM1:   2,144 B / 128 KiB (1.64%)
```

本次增加的离线验证：

- Python 触发参考算法使用 CSV 黄金向量复现整数平方阈值、两次连续命中、计数中断和 `-32768` 输入；
- C 原生测试模拟第一次 EV01 写入立即失败，确认事件状态回到 `ARMED`，所有未写出块回到内存池；
- EV01 v1 协议字节偏移、长度与 CRC 验证顺序已写入 `docs/protocol/debug_event_v1.md`。

## 明确未验证（需要硬件）

- ICM45686 SPI/FIFO/INT/DMA 的真实连续采样；
- 实际加速度阈值、误触发率和漏触发率标定；
- RTT/串口对真实 EV01 的完整接收、超时和物理主机断开；
- 100 次板上测试触发后的长期内存池恢复；
- 静止、轻敲和保护盒小高度跌落三类实物事件及回放曲线。

因此，本记录只证明 Phase 05 的软件闭环与离线错误路径；不构成硬件验收。
