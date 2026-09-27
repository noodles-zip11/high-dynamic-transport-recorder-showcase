# Phase 05 预触发缓存与最小事件闭环 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 STM32H743 上以 128 个固定样本块实现 1 秒预触发、2 秒标准后触发、EV01 调试导出和 PC 校验回放的单事件闭环。

**Architecture:** `imu_acquisition` 继续负责 DMA/FIFO 和不可变样本块发布；事件线程接收块后驱动触发检测、预触发环和事件组装器。块池用引用计数保护环和事件同时需要的数据；导出器按块发送 EV01 并逐块释放事件引用。

**Tech Stack:** C11、RT-Thread IPC、固定内存池、SCons、原生 C 测试、Python 3.12、pytest、标准库 `struct`/`zlib`、Matplotlib。

---

## 已批准的固定边界

- 块池总容量为 128，每块 64 样本；事件系统不能占尽最后 8 个采集保留块。
- 预触发窗口为 25 块（1 秒）；标准后触发为 50 块（2 秒）。
- 重复触发延长后触发截止时间，但总窗口最多 100 块（4 秒）。
- 触发使用合加速度平方值，连续 2 个样本命中；阈值以原始整数配置，实物校准前不宣称物理阈值正确。
- 同一时间仅处理一个事件；导出使用 921600 波特率；长事件置 `DURATION_CAPPED` 标记。
- 本计划不提交、不推送；需要提交时由用户单独授权。

### Task 1: 将样本块池升级为 128 块、引用计数池

**Files:**
- Modify: `firmware/app/sample_block_pool.h`
- Modify: `firmware/app/sample_block_pool.c`
- Modify: `firmware/tests/native/test_sample_block_pool.c`

- [ ] **Step 1: 写失败的块所有权测试**

增加以下场景：池大小为 128；`take_ready()` 后块有一个消费者引用；`retain()` 增加引用；第一次 `release()` 不释放仍被第二个持有者使用的块；最后一次 `release()` 才返回 `FREE`；`free_count()` 正确报告数量。

```c
rt_err_t sample_block_pool_retain(sample_block_pool_t *pool,
                                  sample_block_t *block);
rt_err_t sample_block_pool_release(sample_block_pool_t *pool,
                                   sample_block_t *block);
uint16_t sample_block_pool_free_count(const sample_block_pool_t *pool);
```

- [ ] **Step 2: 运行测试确认失败**

Run: `pwsh scripts/test_native.ps1`

Expected: `test_sample_block_pool` 因缺少引用计数 API 或旧的 8 块预期而失败。

- [ ] **Step 3: 最小实现引用计数池**

将 `SAMPLE_BLOCK_POOL_SIZE` 设为 `128U`，保留 `SAMPLE_BLOCK_HANDOFF_COUNT 8U` 作为事件系统的最低空闲保留。为每块添加 `uint16_t ref_count`。`publish()` 只允许 `FILLING -> READY`；`take_ready()` 只允许 `READY -> CONSUMING` 并设 `ref_count = 1U`；`retain()` 只允许持有中的块且拒绝溢出；`release()` 递减计数，减至零时清零并回到 `FREE`。所有状态检查和引用变更用最小临界区保护。

- [ ] **Step 4: 运行块池测试**

Run: `pwsh scripts/test_native.ps1`

Expected: `sample block pool: PASS`，且现有采集测试仍通过。

### Task 2: 定义触发事实与纯触发检测器

**Files:**
- Create: `firmware/app/trigger_detector.h`
- Create: `firmware/app/trigger_detector.c`
- Create: `firmware/tests/native/test_trigger_detector.c`
- Modify: `firmware/tests/native/SConstruct`
- Modify: `scripts/test_native.ps1`

- [ ] **Step 1: 写触发边界测试**

覆盖阈值下、等于阈值、阈值上、连续计数被打断、连续第二个样本触发、最大负数绝对值和峰值更新。使用传入的配置值，不将未经实物验证的阈值写死。

```c
typedef struct {
    uint32_t threshold_magnitude_sq;
    uint8_t consecutive_samples;
} trigger_detector_config_t;

typedef struct {
    uint32_t sample_sequence;
    uint32_t magnitude_sq;
    uint32_t threshold_magnitude_sq;
    uint8_t axis_mask;
} trigger_fact_t;

bool trigger_detector_feed(trigger_detector_t *detector,
                           const icm45686_fifo_sample_t *sample,
                           uint32_t sample_sequence,
                           trigger_fact_t *out);
```

- [ ] **Step 2: 运行测试确认失败**

Run: `scons -C firmware/tests/native build/test_trigger_detector.exe -Q; firmware/tests/native/build/test_trigger_detector.exe`

Expected: 目标或符号不存在。

- [ ] **Step 3: 实现整数检测器**

用 `int64_t` 扩展三个加速度轴后求平方和，避免 `INT16_MIN` 和平方溢出。轴掩码由达到单轴阈值分量的方向组成；合加速度达到配置阈值时仅在连续次数满足后输出事实。检测器不分配内存、不访问块池、不改变事件状态。

- [ ] **Step 4: 运行完整原生测试**

Run: `pwsh scripts/test_native.ps1`

Expected: 新检测器和既有套件全部通过。

### Task 3: 实现只保存引用的预触发环

**Files:**
- Create: `firmware/app/pretrigger_ring.h`
- Create: `firmware/app/pretrigger_ring.c`
- Create: `firmware/tests/native/test_pretrigger_ring.c`
- Modify: `firmware/tests/native/SConstruct`
- Modify: `scripts/test_native.ps1`

- [ ] **Step 1: 写环回和快照测试**

测试空环、按序读取、写入第 26 块时释放最旧块、快照保留 25 块、预触发不足返回短窗口、快照后的块即使环释放也不会被池复用。

```c
#define PRETRIGGER_RING_BLOCK_CAPACITY 25U

rt_err_t pretrigger_ring_push(pretrigger_ring_t *ring,
                              sample_block_pool_t *pool,
                              sample_block_t *block);
size_t pretrigger_ring_snapshot(pretrigger_ring_t *ring,
                                sample_block_pool_t *pool,
                                sample_block_t **out,
                                size_t capacity);
void pretrigger_ring_reset(pretrigger_ring_t *ring,
                           sample_block_pool_t *pool);
```

- [ ] **Step 2: 运行测试确认失败**

Run: `scons -C firmware/tests/native build/test_pretrigger_ring.exe -Q; firmware/tests/native/build/test_pretrigger_ring.exe`

Expected: 目标或符号不存在。

- [ ] **Step 3: 实现环与转移语义**

`push()` 为块增加环引用；满时先释放最旧环引用再写入新块。`snapshot()` 按时间顺序为事件增加引用，并返回块指针数组；随后调用方可 `reset()` 释放环引用，使事件成为这批历史的唯一持有者。禁止向调用方返回环的内部数组。

- [ ] **Step 4: 运行完整原生测试**

Run: `pwsh scripts/test_native.ps1`

Expected: 环回、引用和原有块池测试全部通过。

### Task 4: 实现单事件组装器和长事件上限

**Files:**
- Create: `firmware/app/event_assembler.h`
- Create: `firmware/app/event_assembler.c`
- Create: `firmware/tests/native/test_event_assembler.c`
- Modify: `firmware/tests/native/SConstruct`
- Modify: `scripts/test_native.ps1`

- [ ] **Step 1: 写状态机测试**

覆盖 `ARMED -> POST_TRIGGER_CAPTURING -> READY_FOR_EXPORT -> EXPORTING -> ARMED`，1 秒预触发不足标记、2 秒标准后触发、重复触发延长截止时间、4 秒窗口上限和 `DURATION_CAPPED`、导出失败释放所有引用、导出中触发增加忙碌计数。

```c
typedef enum {
    EVENT_ARMED = 0,
    EVENT_POST_TRIGGER_CAPTURING,
    EVENT_READY_FOR_EXPORT,
    EVENT_EXPORTING,
} event_state_t;

rt_err_t event_assembler_consume(event_assembler_t *assembler,
                                 sample_block_pool_t *pool,
                                 sample_block_t *block,
                                 const trigger_fact_t *trigger_or_null);
```

- [ ] **Step 2: 运行测试确认失败**

Run: `scons -C firmware/tests/native build/test_event_assembler.exe -Q; firmware/tests/native/build/test_event_assembler.exe`

Expected: 目标或符号不存在。

- [ ] **Step 3: 实现事件块所有权**

预触发快照固定最近 25 块；触发块属于后触发数据的第一个块。后触发默认收集 50 块，延长最多收集到总共 100 块。每个事件块保留其引用直到导出器确认发送或异常清理。组装器在可用块数低于 8 时停止保留更多块、置资源不足错误并走释放路径，绝不复用受保护块。

- [ ] **Step 4: 运行完整原生测试**

Run: `pwsh scripts/test_native.ps1`

Expected: 所有状态、长事件和异常释放用例通过。

### Task 5: 定义 EV01 编码器和 PC 解码器

**Files:**
- Create: `firmware/app/event_export_debug.h`
- Create: `firmware/app/event_export_debug.c`
- Create: `firmware/tests/native/test_event_export_debug.c`
- Create: `host/tools/capture_debug_event.py`
- Create: `host/tools/plot_event.py`
- Create: `host/tests/test_debug_event_parser.py`
- Create: `docs/protocol/debug_event_v1.md`
- Modify: `firmware/tests/native/SConstruct`
- Modify: `scripts/test_native.ps1`

- [ ] **Step 1: 写 EV01 黄金向量测试**

构造一个包含 2 个已知样本的事件；断言 64 B 头的魔数、little-endian 字段、16 B 样本布局、载荷长度和 CRC32。Python 测试读取相同字节串，并分别拒绝错误魔数、错误长度和单字节 CRC 损坏。

- [ ] **Step 2: 运行测试确认失败**

Run: `scons -C firmware/tests/native build/test_event_export_debug.exe -Q; firmware/tests/native/build/test_event_export_debug.exe`

Expected: 目标或符号不存在。

- [ ] **Step 3: 实现逐字段 EV01 编码**

实现固定 64 B 头和 16 B 样本编码函数；禁止 `memcpy` 发送 C 结构体。编码器接收写回调并按块、按时间顺序写出；每成功写完一个块即释放其事件引用。Python 解析器使用 `struct` 和 `zlib.crc32`，仅在完整校验后保存 `.ev01` 并生成 JSON、CSV 与 PNG。

- [ ] **Step 4: 运行 C 和 Python 测试**

Run: `pwsh scripts/test_native.ps1; python -m pytest host/tests -q`

Expected: 原生测试通过；Python 解析器通过黄金和损坏文件测试。

### Task 6: 将事件线程接入已有采集服务与 MSH

**Files:**
- Modify: `firmware/app/imu_acquisition.h`
- Modify: `firmware/app/imu_acquisition.c`
- Create: `firmware/app/event_service.h`
- Create: `firmware/app/event_service.c`
- Modify: `firmware/app/main.c`
- Modify: `firmware/app/imu_acquisition_stats.h`
- Modify: `firmware/app/imu_acquisition_stats.c`
- Modify: `firmware/tests/native/test_imu_acquisition_stats.c`
- Modify: `firmware/tests/native/include/rtthread.h`

- [ ] **Step 1: 写采集到事件线程的接口测试**

在原生替身中验证：发布块会唤醒事件消费者；事件消费者释放块后空闲数恢复；状态报告包含事件状态、预触发深度、事件忙碌数和导出错误数。

- [ ] **Step 2: 运行测试确认失败**

Run: `pwsh scripts/test_native.ps1`

Expected: 新状态字段和事件服务符号尚不存在。

- [ ] **Step 3: 最小接入实现**

在 `imu_acquisition` 中增加块就绪信号和受限的消费 API，不向外暴露整个池：

```c
sample_block_t *imu_acquisition_take_ready_block(rt_int32_t timeout);
rt_err_t imu_acquisition_release_block(sample_block_t *block);
```

`event_service` 创建独立线程，等待块、运行检测器和组装器；MSH 实现 `event status`、`event arm`、`event trigger_test`、`event export`、`event clear`。`event export` 仅在事件就绪时发送二进制；文本输出在发送前后进行，不能插入载荷。将事件统计并入每秒健康报告。
原生 `rtthread.h` 替身同步增加 `rt_int32_t` 及事件线程接入所需的最小类型，不能让主机测试因缺少 RT-Thread 名称而失去编译能力。

- [ ] **Step 4: 运行构建和测试**

Run: `. scripts/project_env.ps1; & $SConsExe -C firmware -j4; pwsh scripts/test_native.ps1`

Expected: H743 固件构建成功，原生 C 测试全部通过。

### Task 7: 故障注入、文档与硬件验证清单

**Files:**
- Modify: `docs/decisions/pretrigger-memory-budget.md`
- Create: `docs/decisions/minimal-trigger.md`
- Modify: `高动态运输事件记录器_项目启动包/高动态运输事件记录器_项目启动包/docs/superpowers/plans/transport-recorder/05_预触发缓存与最小事件闭环.md`
- Create: `evidence/phase05/README.md`

- [ ] **Step 1: 写故障注入测试**

增加预触发不足、块边界触发、10 次重复触发、导出中断、资源保留和 100 次触发后空闲块恢复的原生测试。每个异常路径断言块数和状态恢复，而非只检查返回码。

- [ ] **Step 2: 实现并运行测试**

Run: `pwsh scripts/test_native.ps1; python -m pytest host/tests -q`

Expected: 所有故障场景通过，损坏 EV01 被 Python 拒绝。

- [ ] **Step 3: 更新决策和证据说明**

记录 128 块、8 块保留、1+2 秒窗口、4 秒上限、921600 波特率和“长事件明确截断”的理由。列出待实物校准的原始阈值、静止/轻敲/保护盒跌落实验和 100 次板上触发的证据文件名。

- [ ] **Step 4: 最终验证**

Run: `. scripts/project_env.ps1; & $SConsExe -C firmware -j4; pwsh scripts/test_native.ps1; python -m pytest host/tests -q; git diff --check`

Expected: 构建、C 测试、Python 测试和格式检查全部通过；硬件项目仍明确标为待实物完成。
