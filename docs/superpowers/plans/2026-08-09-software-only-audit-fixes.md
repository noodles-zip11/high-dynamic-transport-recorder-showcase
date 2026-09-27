# Software-only Audit Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close every approved A-class audit finding without changing hardware-dependent timing, pins, task priorities, DMA/FIFO parameters, or physical acceptance claims.

**Architecture:** Preserve the existing directed firmware and host layers. Correct event semantics with EV03, put destructive storage rules at the service boundary, bound event-log lookup work with a sparse index, publish consistent snapshots, and serialize host device operations. Every behavior follows RED → GREEN → focused regression before the next task.

**Tech Stack:** C11, RT-Thread, SCons, native MinGW tests, STM32 ARM GCC, Python 3.12, pytest, pytest-qt, PySide6.

---

### Task 1: Extend 32-bit RT ticks into monotonic microseconds

**Files:**
- Create: `firmware/app/time/monotonic_clock.h`
- Create: `firmware/app/time/monotonic_clock.c`
- Create: `firmware/tests/native/test_monotonic_clock.c`
- Modify: `firmware/tests/native/SConstruct`
- Modify: `scripts/test_native.ps1`

- [ ] **Step 1: Write the failing wraparound test**

Define a pure `monotonic_clock_observe()` API and assert observations `0xFFFFFFFE`, `0xFFFFFFFF`, `0`, `1` at 1 kHz produce strictly increasing microseconds across the wrap. Also assert repeated observation of one tick is stable and a null argument is rejected.

- [ ] **Step 2: Run RED**

Run `pwsh scripts/test_native.ps1`; expect compilation to fail because `monotonic_clock.h` and its implementation do not exist.

- [ ] **Step 3: Implement the minimal tick extender**

Store `last_tick`, `epoch_ticks`, and `initialized`. Add `2^32` ticks only when the new unsigned tick is numerically lower than the last observation. Convert ticks to microseconds with quotient/remainder arithmetic so non-1000 Hz rates remain exact without overflow.

- [ ] **Step 4: Run GREEN**

Run the new executable directly, then `pwsh scripts/test_native.ps1`; expect all native tests to pass.

### Task 2: Correct block sample identity and trigger position

**Files:**
- Modify: `firmware/app/pipeline/sample_block_pool.h`
- Modify: `firmware/app/acquisition/imu_acquisition.c`
- Modify: `firmware/app/event/trigger_detector.h`
- Modify: `firmware/app/event/trigger_detector.c`
- Modify: `firmware/app/event/event_service.c`
- Modify: `firmware/app/event/event_assembler.h`
- Modify: `firmware/app/event/event_assembler.c`
- Modify: `firmware/app/event/event_export_debug.c`
- Modify: `firmware/tests/native/test_trigger_detector.c`
- Modify: `firmware/tests/native/test_event_assembler.c`
- Modify: `firmware/tests/native/test_event_export_debug.c`

- [ ] **Step 1: Write failing trigger-boundary tests**

For a 64-sample trigger block, cover trigger indices 0, 17, and 63. Assert `trigger_sequence == block.sequence + index`, `pretrigger_samples == history samples + index`, the payload sample at that index is the trigger sample, and `trigger_monotonic_us == first_monotonic_us + index * sample_period_ns / 1000`.

- [ ] **Step 2: Run RED**

Run the three affected native executables; expect assertions to fail because the current code multiplies block sequence by 64, counts the entire trigger block as post-trigger, and timestamps the block start.

- [ ] **Step 3: Implement minimal semantic changes**

Make block `sequence` mean first sample sequence; add `first_monotonic_us`; add `sample_index` to `trigger_fact_t`; store exact `trigger_monotonic_us` and `trigger_sample_index` in `event_record_t`; compute pre/post counts from the trigger index without copying or splitting blocks.

- [ ] **Step 4: Run GREEN and regressions**

Run the affected executables and the complete native suite.

### Task 3: Add EV03 and real DATA_LOSS evidence

**Files:**
- Modify: `firmware/app/event/event_assembler.h`
- Modify: `firmware/app/event/event_export_debug.h`
- Modify: `firmware/app/event/event_export_debug.c`
- Modify: `firmware/components/storage/event_log.c`
- Modify: `docs/protocol/event_record_v2.md`
- Create: `docs/protocol/event_record_v3.md`
- Modify: `host/transport_recorder/analysis/event_record.py`
- Modify: `host/tools/capture_debug_event.py`
- Modify: `host/tests/fixtures/events/build_fixtures.py`
- Create: `host/tests/fixtures/events/valid-v3-loss-43.terp-event`
- Modify: `firmware/tests/native/test_event_export_debug.c`
- Modify: `firmware/tests/native/test_event_log.c`
- Modify: `host/tests/unit/test_event_record.py`

- [ ] **Step 1: Write failing C tests for one and multiple gaps**

Build events whose adjacent block sequences contain exact gaps. Assert EV03 header length 160, `DATA_LOSS`, total lost samples, first/last lost sequence, episode count, and first/last loss microseconds. Add a no-gap EV03 case with all extension fields zero.

- [ ] **Step 2: Run C RED**

Expect the exporter golden test and recovery test to fail because only EV02 is emitted/accepted.

- [ ] **Step 3: Implement EV03 exporter and recovery validation**

Keep offsets 0..127 compatible with EV02, emit the approved 32-byte extension, use unsigned sequence deltas bounded by the event maximum, and update EL01 validation for version 3/header 160.

- [ ] **Step 4: Write and run Python RED**

Generate the EV03 fixture with production-equivalent bytes and assert `load_event()` exposes immutable loss metadata. Expect unknown-version failure until parser support is added.

- [ ] **Step 5: Implement Python EV03 parsing and run GREEN**

Parse all approved fields, validate payload length/CRC, retain EV01/EV02 support, rebuild fixtures, and run C plus host parser tests.

### Task 4: Make format confirmation non-bypassable

**Files:**
- Modify: `firmware/app/storage/storage_service.h`
- Modify: `firmware/app/storage/storage_service.c`
- Modify: `firmware/tests/native/test_storage_format_confirmation.c`
- Create: `firmware/tests/native/test_storage_service_format.c`
- Modify: `firmware/tests/native/SConstruct`
- Modify: `scripts/test_native.ps1`

- [ ] **Step 1: Write failing service-boundary tests**

Call the public service API without request, after timeout, twice after one request, and after successful consumption. Assert no fake NOR erase occurs except once for the valid request/confirm pair.

- [ ] **Step 2: Run RED**

Expect compile/API failure because the service does not expose guarded request/confirm operations and exposes raw format.

- [ ] **Step 3: Hide raw erase and expose guarded operations**

Move the actual `event_log_format()` call behind a file-private function. Public request and confirm use the existing confirmation state; confirm consumes pending state on every outcome. Update MSH to call the same public guard.

- [ ] **Step 4: Run GREEN**

Run service format tests, existing confirmation tests, event log tests, and the full native suite.

### Task 5: Validate full JEDEC identity and derive geometry

**Files:**
- Modify: `firmware/components/storage/nor_flash_w25q.c`
- Modify: `firmware/tests/native/test_nor_flash_w25q.c`
- Modify: `docs/storage/event_log_v1.md`

- [ ] **Step 1: Write failing identity tests**

Test `EF 40 17` succeeds and each of wrong manufacturer, wrong memory type, wrong capacity, `00 00 00`, and `FF FF FF` fails without setting capacity.

- [ ] **Step 2: Run RED**

Expect wrong-manufacturer and wrong-type cases to be incorrectly accepted by current code.

- [ ] **Step 3: Implement a fixed device table**

Match all three bytes and populate capacity/page/sector geometry from the matching table entry. Do not infer geometry from the capacity byte alone.

- [ ] **Step 4: Run GREEN**

Run W25Q and event-log tests, then the full native suite.

### Task 6: Bound EL01 list and chunk-read work

**Files:**
- Modify: `firmware/components/storage/event_log.h`
- Modify: `firmware/components/storage/event_log.c`
- Modify: `firmware/app/storage/storage_service.h`
- Modify: `firmware/app/storage/storage_service.c`
- Modify: `firmware/app/transport/terp_service.c`
- Modify: `firmware/tests/native/fake_nor.h`
- Modify: `firmware/tests/native/fake_nor.c`
- Modify: `firmware/tests/native/test_event_log.c`
- Modify: `firmware/tests/native/test_terp_service.c`

- [ ] **Step 1: Instrument real fake-NOR reads**

Count read calls and bytes in `fake_nor_t` without changing read behavior.

- [ ] **Step 2: Write failing cost-bound tests**

Create enough committed records to cross multiple 16-record checkpoints. Assert one page list is a single forward metadata traversal, a cached event's successive chunk reads do not rescan payload/history, and random lookup examines at most 15 record headers after its sparse anchor.

- [ ] **Step 3: Run RED**

Expect functional results to match but read-call/byte limits to fail with the current full-payload rescans.

- [ ] **Step 4: Split validation from committed metadata lookup**

Keep full payload validation for recovery and explicit verify. Build a bounded sparse index after mount/append, add sequential list, and cache the last event metadata for chunk reads. Never treat uncommitted space as queryable.

- [ ] **Step 5: Run GREEN and power-loss regressions**

Run event-log fault-injection tests and TERP service tests, then the complete native suite.

### Task 7: Publish consistent health and IMU snapshots

**Files:**
- Modify: `firmware/app/health/health_service.h`
- Modify: `firmware/app/health/health_service.c`
- Modify: `firmware/app/acquisition/imu_acquisition.c`
- Modify: `firmware/app/runtime/app_runtime.c`
- Modify: `firmware/tests/native/test_health_service.c`
- Modify: `firmware/tests/native/test_imu_acquisition_stats.c`
- Modify: `firmware/tests/native/include/rtthread.h`

- [ ] **Step 1: Write failing consistency and wrap tests**

Use native RT-Thread synchronization instrumentation to assert health update/copy is guarded and IMU stats copy enters one short interrupt critical section. Assert runtime monotonic time uses the Task 1 extender across wrap.

- [ ] **Step 2: Run RED**

Expect guard counters to remain zero and wrap output to regress.

- [ ] **Step 3: Implement bounded synchronization**

Use one health mutex for whole-snapshot publish/copy. Copy IMU scalar counters with interrupts disabled on the single-core target. Route runtime/acquisition timestamps through the shared monotonic clock.

- [ ] **Step 4: Run GREEN and firmware build**

Run health, acquisition, event export tests and build the H743 ELF to confirm memory/layout impact.

### Task 8: Serialize host device operations and correct failure states

**Files:**
- Modify: `host/transport_recorder/device/session.py`
- Modify: `host/transport_recorder/ui/main_window.py`
- Modify: `host/tests/integration/test_device_session.py`
- Modify: `host/tests/ui/test_main_window.py`

- [ ] **Step 1: Write failing session-state tests**

Assert a second list/download/connect operation during transfer is rejected without touching transport. Assert cancel returns READY, while timeout, protocol error, CRC error, and reconnect failure leave no usable client and do not report READY.

- [ ] **Step 2: Run RED**

Expect concurrent operations to enter the shared client and generic failure to reset session READY.

- [ ] **Step 3: Implement serialized transitions**

Guard state with `threading.RLock`, narrow legal entry states, distinguish `DownloadCancelled`, and close/reset the client on non-cancellation failures.

- [ ] **Step 4: Write UI RED and implement control gating**

Assert connect/download actions are disabled while a device worker runs and restored from actual session state. Keep local replay workers independent.

- [ ] **Step 5: Run GREEN**

Run device integration and UI tests offscreen, then all host tests.

### Task 9: Complete deterministic Phase 09 desktop gaps

**Files:**
- Modify: `host/transport_recorder/ui/main_window.py`
- Modify: `host/transport_recorder/ui/replay_widget.py`
- Modify: `host/transport_recorder/repository/event_repository.py`
- Modify: `host/transport_recorder/analysis/export.py`
- Modify: `host/tests/ui/test_main_window.py`
- Modify: `host/tests/unit/test_event_repository.py`
- Modify: `host/tests/unit/test_export.py`

- [ ] **Step 1: Write failing pagination tests**

Return two simulated pages, assert the second appends without clearing the first, and disable load-more when `next_event_id == 0`.

- [ ] **Step 2: Write failing annotation/export tests**

Update a stored event's label/note through the window-facing operation and assert JSON export reads persisted values rather than hard-coded nulls.

- [ ] **Step 3: Write failing warning tests**

Load EV03 loss, saturated samples, UTC-invalid, and storage-error fixtures; assert each produces a distinct visible warning.

- [ ] **Step 4: Implement the minimal UI/repository flow**

Track pagination cursor, current `StoredEvent`, and annotations without putting protocol or SQL in the UI. Derive warnings from decoded immutable metadata/samples.

- [ ] **Step 5: Run GREEN**

Run unit, integration, UI, and complete host suites offscreen.

### Task 10: Add warning gates and perform complete verification

**Files:**
- Modify: `firmware/app/SConscript`
- Modify: `firmware/components/SConscript`
- Modify: `firmware/drivers/SConscript`
- Modify: `firmware/tests/native/SConstruct`
- Modify: `scripts/run_tests.ps1`
- Modify: `docs/protocol/event_record_v3.md`

- [ ] **Step 1: Add project-source warning flags**

Enable `-Wall -Wextra` for project-owned C sources and native tests without applying new warnings to external RT-Thread/HAL sources. Fix only concrete warnings in files touched by this plan; do not refactor unrelated BSP code.

- [ ] **Step 2: Run generators and focused suites**

Run protocol generator checks, golden checks, native tests, and host tests. Expect no warnings or failures.

- [ ] **Step 3: Run a clean firmware build**

Remove only the worktree's ignored firmware build directory after resolving its absolute path, rebuild with `-RequireElf`, and verify vector/memory-map checks and size output.

- [ ] **Step 4: Run repository checks**

Run `git diff --check`, inspect `git status`, confirm no build products or `.venv` files are tracked, and compare every changed path against the approved A-class scope.

- [ ] **Step 5: Produce before/after evidence**

Report behavior changes, test counts, read-cost bounds, firmware size delta, and all hardware-dependent items that remain pending. Do not claim hardware acceptance.
