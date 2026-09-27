# Phase 05 无硬件收尾 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在没有 ICM45686 硬件的条件下，完成 Phase 05 所有可重复的软件验证、协议文档和离线故障证据，并把硬件结论明确留空。

**Architecture:** 保持现有 C 事件链不变。Python 参考实现独立复现 C 的整数平方阈值与连续计数规则，并以 CSV 黄金向量约束其行为。原生测试模拟导出写入失败，以确认事件块最终全部释放。EV01 文档以 C 编码器和 Python 解析器的共同格式为准。

**Tech Stack:** C11、RT-Thread native stubs、Python 3.12、pytest、SCons、STM32CubeCLT。

---

### Task 1: 独立触发参考算法与黄金 CSV

**Files:**
- Create: `host/tools/trigger_reference.py`
- Create: `host/tests/data/trigger_reference_golden.csv`
- Create: `host/tests/test_trigger_reference.py`

- [ ] 编写 CSV，字段为 `sequence,ax,ay,az,expected_trigger`；固定阈值平方为 `100`、连续次数为 `2`。覆盖阈值下 (`9,0,0`)、阈值等于 (`10,0,0`)、连续计数被零样本打断，以及 `-32768`。
- [ ] 编写 pytest：读取 CSV，调用 `detect_trigger_facts(samples, threshold_magnitude_sq=100, consecutive_samples=2)`，断言触发序号为 `3` 和 `6`，并断言最后一次强度为 `1073741824`、轴掩码为 `TRIGGER_AXIS_X`。
- [ ] 先运行该 pytest，确认因模块不存在而失败。
- [ ] 实现无硬件依赖的 Python 整数参考算法：`magnitude_sq = ax * ax + ay * ay + az * az`；低于阈值清零；达到连续次数时产生事实；轴掩码规则与 C 相同。
- [ ] 重跑该 pytest，确认通过。

### Task 2: 导出写入失败的原生释放验证

**Files:**
- Modify: `firmware/tests/native/test_event_export_debug.c`

- [ ] 增加一个总是返回 `-RT_ERROR` 的写入回调。
- [ ] 构造一个处于 `EVENT_EXPORTING` 的单块事件，调用 `event_export_debug_write()`，断言返回错误、状态回到 `EVENT_ARMED`、空闲块数恢复为 `SAMPLE_BLOCK_POOL_SIZE`。
- [ ] 运行单个 `test_event_export_debug.exe`，确认新测试在现有实现下通过；该测试是对既有失败清理路径的覆盖，不改变生产逻辑。

### Task 3: EV01 协议文档

**Files:**
- Create: `docs/protocol/debug_event_v1.md`

- [ ] 写明 EV01 仅用于 Phase 05 调试导出，不能作为长期 Flash/GUI 协议。
- [ ] 用字节偏移表说明 64 字节小端 header：magic、版本、header 长度、事件 ID、触发 tick、采样率、触发序号、前/后样本数、子触发数、flags、峰值、阈值、payload 长度、CRC32。
- [ ] 写明每条 16 字节 payload 样本的字段和偏移。
- [ ] 写明接收端验证顺序：magic、版本/长度、payload 长度、样本计数、CRC32，随后才能解析样本。

### Task 4: 无硬件验证证据

**Files:**
- Create: `evidence/phase05/no-hardware-verification.md`

- [ ] 运行 `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/run_tests.ps1`。
- [ ] 运行 `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/build_firmware.ps1 -RequireElf`。
- [ ] 运行 `git diff --check`。
- [ ] 记录命令、通过结果、构建内存摘要，以及以下明确未验证项：实际 ICM45686 流、真实阈值标定、串口/RTT 实收 EV01、主机断开、100 次板上触发和三类实物动作。
- [ ] 不提交；待用户显式确认后再进行提交操作。

## Scope exclusions

- 不移动 `app/` 文件或整理 RT-Thread 任务入口；该项在 Phase 05 提交后进入独立 `feature/app-structure` 工作树。
- 不设置生产阈值；没有硬件数据时保持自动物理触发关闭。
- 不声明任何板上、串口或真实事件验收已经完成。
