# V1 Release Candidate Implementation Plan

> **HISTORICAL / COMPLETED:** this plan records how the V1 candidate was built;
> its unchecked-looking task instructions are not the current project status.
> The implemented and accepted result is indexed at
> [`evidence/releases/v1.0.0/`](../../../evidence/releases/v1.0.0/README.md).

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a clean V1 release-candidate branch with real four-class AI, packet-safe FIFO draining, production natural event triggering, and conservative Cortex-M7 WFI low-power behavior.

**Architecture:** Start from protected `main@58477d3` in the isolated V1 worktree. Cherry-pick only reviewed clean commits, then add production trigger behavior and selectively port the already hardware-gated WFI implementation using TDD. Keep collection campaigns, periodic field capture, temporary partitions, STOP mode, automatic overwrite, and private raw data outside the branch.

**Tech Stack:** STM32H743, RT-Thread, C11, SCons, native MinGW tests, Python 3.12/pytest, PowerShell, Git worktrees.

---

## File map

### Reviewed integrations

- `ai/`, `docs/ai/`, `firmware/app/ai_inference/`, `firmware/app/ota/`, AI native tests and AI evidence: clean four-class AI commits.
- `firmware/app/acquisition/imu_acquisition.c`: repeated FIFO drain per wake.
- `firmware/app/acquisition/imu_acquisition_service.c`: packet-aligned bounded chunks.
- `firmware/tests/native/test_imu_acquisition.c`: 4112-byte backlog regression.

### Production trigger

- Modify `firmware/app/event/trigger_detector.h`: comparison direction contract.
- Modify `firmware/app/event/trigger_detector.c`: ABOVE/BELOW 64-bit-safe comparison.
- Modify `firmware/app/event/event_service.h`: production trigger/cooldown stats and test seam.
- Modify `firmware/app/event/event_service.c`: dual detector admission, one-shot fixed-length behavior, cooldown and status.
- Modify `firmware/tests/native/test_trigger_detector.c`: detector boundary tests.
- Modify `firmware/tests/native/test_event_service.c`: end-to-end admission, fixed 2400 samples and cooldown tests.

### Low power

- Create `firmware/app/power/power_policy.{h,c}`: pure state/blocker/counter logic.
- Create `firmware/app/power/power_status_format.{h,c}`: bounded read-only status formatting.
- Create `firmware/app/power/power_runtime.{h,c}`: RT-Thread idle hook and lifecycle adapter.
- Create `firmware/bsp/openmv4_h743/board_power.{h,c}` and `firmware/bsp/weact_h743/board_power.{h,c}`: WFI-only BSP primitive.
- Modify application lifecycle files: acquisition, event, storage, AI, OTA state store, UART3 transport and `app_runtime.c`.
- Modify SCons files and native stubs/runners to compile the modules and tests.
- Create native power tests already proven in the low-power worktree, then resolve them against the V1 AI and trigger code.

### Release evidence

- Create `evidence/v1/2026-08-24/software-gate.md`.
- Create `evidence/v1/2026-08-24/hardware-acceptance-checklist.md`.

## Task 1: Integrate the reviewed four-class AI commits

**Files:** Use the exact committed changes in `6f0838f`, `9b3f88b`, `af51ff9`, and `8b6b2d1`; do not edit private raw data or the dirty collection worktree.

- [ ] **Step 1: Prove the V1 worktree starts clean and on the protected base**

Run:

```powershell
git status --short --branch
git merge-base --is-ancestor 58477d38b700e3edad3a7d144a6916f5f574ffce HEAD
```

Expected: only branch header, then exit 0.

- [ ] **Step 2: Cherry-pick the four AI commits in order**

Run:

```powershell
git cherry-pick 6f0838f 9b3f88b af51ff9 8b6b2d1
```

Expected: four new commits and no conflicts. If a conflict occurs, stop and report it; do not merge the source branch.

- [ ] **Step 3: Run focused AI and firmware-native checks**

Run:

```powershell
$env:TRANSPORT_VENV_ROOT='LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv'
& "$env:TRANSPORT_VENV_ROOT\Scripts\python.exe" -m pytest ai/tests -q
pwsh -NoProfile -File .\scripts\test_native.ps1
git diff --check
```

Expected: AI tests and all native executables PASS; diff check exit 0.

## Task 2: Integrate the packet-aligned FIFO backlog fix

**Files:**

- Modify: `firmware/app/acquisition/imu_acquisition.c`
- Modify: `firmware/app/acquisition/imu_acquisition_service.c`
- Test: `firmware/tests/native/test_imu_acquisition.c`

- [ ] **Step 1: Cherry-pick only the independent FIFO commit**

Run:

```powershell
git cherry-pick fea2d6b
```

Expected: one commit changing only the three listed files.

- [ ] **Step 2: Verify the 4112-byte regression**

Run:

```powershell
$env:TRANSPORT_VENV_ROOT='LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv'
& "$env:TRANSPORT_VENV_ROOT\Scripts\scons.exe" -Q -C firmware/tests/native build/test_imu_acquisition.exe
& .\firmware\tests\native\build\test_imu_acquisition.exe
```

Expected: `imu acquisition: PASS`, including five reads of 1024/1024/1024/1024/16 bytes, 257 samples, empty FIFO and zero capacity errors.

## Task 3: Add detector comparison direction with TDD

**Files:**

- Modify: `firmware/app/event/trigger_detector.h`
- Modify: `firmware/app/event/trigger_detector.c`
- Modify: `firmware/tests/native/test_trigger_detector.c`

- [ ] **Step 1: Write failing BELOW and boundary tests**

Add tests that initialize:

```c
trigger_detector_config_t config = {
    .threshold_magnitude_sq = UINT32_C(2359296),
    .consecutive_samples = 8U,
    .comparison = TRIGGER_COMPARISON_BELOW,
};
```

Assert that 1 g does not trigger, seven consecutive samples at or below 0.75 g do not trigger, the eighth triggers, an interrupted run resets the counter, and `INT16_MIN` remains overflow-safe. Also assert the zero-initialized/default comparison remains ABOVE for source compatibility.

- [ ] **Step 2: Run RED**

Run:

```powershell
$env:TRANSPORT_VENV_ROOT='LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv'
& "$env:TRANSPORT_VENV_ROOT\Scripts\scons.exe" -Q -C firmware/tests/native build/test_trigger_detector.exe
```

Expected: compile/test failure because comparison direction is absent.

- [ ] **Step 3: Implement the minimal compatible enum**

Add:

```c
typedef enum
{
    TRIGGER_COMPARISON_ABOVE = 0,
    TRIGGER_COMPARISON_BELOW,
} trigger_comparison_t;
```

Append `trigger_comparison_t comparison` to `trigger_detector_config_t`. Compute squared magnitude in `uint64_t`; ABOVE hits on `>= threshold`, BELOW hits on `<= threshold`. Reset `consecutive_count` whenever the configured condition is false.

- [ ] **Step 4: Run GREEN and commit**

Run the focused executable, then `git diff --check`.

Commit:

```powershell
git add firmware/app/event/trigger_detector.c firmware/app/event/trigger_detector.h firmware/tests/native/test_trigger_detector.c
git commit -m "feat(event): support impact and drop comparisons"
```

## Task 4: Implement production one-shot trigger and cooldown with TDD

**Files:**

- Modify: `firmware/app/event/event_service.h`
- Modify: `firmware/app/event/event_service.c`
- Modify: `firmware/tests/native/test_event_service.c`

- [ ] **Step 1: Add failing production admission tests**

Cover these exact facts:

```c
#define EVENT_IMPACT_THRESHOLD_COUNTS 5120U
#define EVENT_IMPACT_CONSECUTIVE_SAMPLES 2U
#define EVENT_DROP_THRESHOLD_COUNTS 1536U
#define EVENT_DROP_CONSECUTIVE_SAMPLES 8U
#define EVENT_NATURAL_TRIGGER_COOLDOWN_US UINT64_C(30000000)
```

Tests must prove:

- 1 g and the recorded 1.911 g background boundary do not trigger impact.
- 2.5 g requires two consecutive samples.
- 0.75 g requires eight consecutive samples.
- the first natural trigger latches admission; POST blocks cannot create subtriggers or extend capture.
- the event contains exactly 75 blocks/2400 samples with 25 pretrigger and 50 posttrigger blocks.
- a second qualifying event during 30 seconds is rejected while pretrigger history continues.
- the first qualifying event at or after the deadline is accepted.
- successful export, failed export, clear and restart reset detector counters without bypassing storage readiness.

- [ ] **Step 2: Run focused RED**

Run:

```powershell
$env:TRANSPORT_VENV_ROOT='LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv'
& "$env:TRANSPORT_VENV_ROOT\Scripts\scons.exe" -Q -C firmware/tests/native build/test_event_service.exe
& .\firmware\tests\native\build\test_event_service.exe
```

Expected: failure on absent dual-detector/cooldown behavior.

- [ ] **Step 3: Implement minimal production trigger state**

Use separate impact/drop detector instances with squared thresholds `26214400` and `2359296`. Natural detector feeding is allowed only while the assembler is `EVENT_ARMED`, the sink is ready and the cooldown deadline has elapsed. Manual test triggers remain explicit maintenance operations.

When the first natural detector fires:

```c
event_natural_trigger_latched = RT_TRUE;
impact_detector.consecutive_count = 0U;
drop_detector.consecutive_count = 0U;
```

While POST capture/export is active, return no natural trigger so `event_assembler_record_subtrigger()` is never reached from production admission. After export/clear returns the assembler to ARMED, set a saturating deadline of completion time plus 30 seconds, clear the latch and reset both counters. Maintain explicit stats for accepted natural triggers and cooldown rejections.

- [ ] **Step 4: Run GREEN, AI contract regression and commit**

Run:

```powershell
& .\firmware\tests\native\build\test_event_service.exe
& .\firmware\tests\native\build\test_ai_inference_service.exe
pwsh -NoProfile -File .\scripts\test_native.ps1
git diff --check
```

Expected: fixed 2400-sample event, no natural subtrigger, all native tests PASS.

Commit:

```powershell
git add firmware/app/event/event_service.c firmware/app/event/event_service.h firmware/tests/native/test_event_service.c
git commit -m "feat(event): add production natural trigger policy"
```

## Task 5: Port the WFI policy core and BSP primitive

**Files:**

- Create: `firmware/app/power/power_policy.{h,c}`
- Create: `firmware/app/power/power_status_format.{h,c}`
- Create: `firmware/app/power/power_runtime.{h,c}`
- Create: `firmware/bsp/openmv4_h743/board_power.{h,c}`
- Create: `firmware/bsp/weact_h743/board_power.{h,c}`
- Create tests: `firmware/tests/native/test_power_policy.c`, `test_power_status_format.c`, `test_power_runtime.c`, `test_board_power.c`
- Modify build plumbing: `firmware/app/SConscript`, both BSP `SConscript` files, `firmware/tests/native/SConstruct`, `firmware/tests/native/include/rtthread.h`, `firmware/tests/native/include/stm32h7xx.h`, `scripts/test_native.ps1`

- [ ] **Step 1: Add the proven native tests before production sources**

Port the tests from the low-power worktree using `apply_patch`, preserving coverage for legal/sticky modes, nested blocker references, saturating counters, first concrete wake attribution, bounded status formatting, one-time/retry-safe idle registration and `SLEEPDEEP` clearing.

- [ ] **Step 2: Run RED**

Run focused SCons targets. Expected: missing power headers/sources.

- [ ] **Step 3: Port and review the minimal production modules**

Port source text with `apply_patch`; do not apply the dirty worktree diff wholesale. Confirm the BSP primitive contains only:

```c
SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
__DSB();
__WFI();
__ISB();
```

Confirm `stop_compiled=0` and `stop_allowed=0`, and that the idle hook prints nothing, allocates nothing and waits on no lock.

- [ ] **Step 4: Run GREEN and commit the isolated policy core**

Run the four power executables and full native runner.

Commit:

```powershell
git add firmware/app/power firmware/bsp/openmv4_h743/board_power.c firmware/bsp/openmv4_h743/board_power.h firmware/bsp/weact_h743/board_power.c firmware/bsp/weact_h743/board_power.h firmware/app/SConscript firmware/bsp/openmv4_h743/SConscript firmware/bsp/weact_h743/SConscript firmware/tests/native scripts/test_native.ps1
git commit -m "feat(power): add observable WFI policy"
```

Before committing, inspect the staged file list so unrelated generated `.o/.exe` files are excluded.

## Task 6: Connect WFI blockers to the V1 runtime

**Files:**

- Modify: `firmware/app/acquisition/imu_acquisition.c`
- Modify: `firmware/app/event/event_service.c`
- Modify: `firmware/app/storage/storage_service.c`
- Modify: `firmware/app/ai_inference/ai_inference_service.c`
- Modify: `firmware/app/ota/ota_state_app_store.c`
- Modify: `firmware/app/transport/terp_uart3.c`
- Modify: `firmware/app/runtime/app_runtime.c`
- Modify corresponding native tests.

- [ ] **Step 1: Port lifecycle tests and run RED**

Add assertions that DMA holds/releases `POWER_BLOCKER_DMA`, event transitions publish `EVENT_ACTIVE`, storage writes hold `POWER_BLOCKER_STORAGE`, queued/in-flight/persistence-retry AI work holds `POWER_BLOCKER_AI`, UART activity owns a maintenance lease, and OTA state writes own the OTA/diagnostic blocker.

- [ ] **Step 2: Port integrations one subsystem at a time**

Use the already hardware-gated low-power implementation as the source, resolving against the four-class AI and production trigger code. After each subsystem, run its focused native executable. Preserve current priorities, protocol frames, Flash addresses, pins, clocks, DMA/cache policy and event ownership.

- [ ] **Step 3: Update runtime behavior**

Start `power_runtime` before services, move to MONITOR only after acquisition and event services start, otherwise enter FAULT_FALLBACK. Remove the 500 ms LED heartbeat thread, initialize LED off, keep 1 Hz health/OTA work and print detailed reports every 60 seconds.

- [ ] **Step 4: Run full native and firmware Debug gate**

Run:

```powershell
$env:TRANSPORT_VENV_ROOT='LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv'
pwsh -NoProfile -File .\scripts\test_native.ps1
pwsh -NoProfile -File .\scripts\run_tests.ps1 -WithFirmware -FirmwareProfile Debug -FirmwareRevision v1-rc-local -FirmwareSerial recorder-001
git diff --check
```

Expected: all checks PASS, AI worker requested stack at least 2560 B, no memory-map/vector/alignment regression.

- [ ] **Step 5: Commit runtime integration**

Commit:

```powershell
git add firmware/app firmware/tests/native scripts/test_native.ps1
git commit -m "feat(power): integrate WFI lifecycle blockers"
```

Inspect the staged list first; it must not include build products or collection/field code.

## Task 7: Freeze the V1 software candidate

**Files:**

- Create: `evidence/v1/2026-08-24/software-gate.md`
- Create: `evidence/v1/2026-08-24/hardware-acceptance-checklist.md`

- [ ] **Step 1: Run final full Debug and Release gates**

Run the full suite with the actual candidate revision and serial. Build both Debug and Release using repository scripts; do not flash yet. Record exact exit codes, test counts, ROM/RAM usage and warnings.

- [ ] **Step 2: Record artifact identity**

For the final Release ELF/BIN/MAP, record absolute path, byte length and SHA-256. Confirm the embedded revision matches the V1 candidate commit and that no `field`, `timed`, `collection`, `pending` or periodic-capture marker exists in the Release binary.

- [ ] **Step 3: Write evidence and acceptance checklist**

The software gate must state:

- real AI commit ancestry;
- FIFO, trigger, cooldown and WFI behavior;
- full test results;
- current/power/runtime still unmeasured;
- no real road test is required;
- `main` is unchanged;
- exact rollback source.

The hardware checklist must contain the six user actions from the approved design and explicit PASS fields for event IDs, 2400-sample contract, CRC, AI result, errors and `power status`.

- [ ] **Step 4: Final review and evidence commit**

Run:

```powershell
git diff --check
git status --short
git log --oneline main..HEAD
git diff --stat main...HEAD
git -C 'LOCAL_USER_HOME\Desktop\高动态运输事件记录器' rev-parse HEAD
git -C 'LOCAL_USER_HOME\Desktop\高动态运输事件记录器' status --short --branch
```

Expected: V1 contains only reviewed scope; main HEAD remains `58477d3`. Preserve the user's existing dirty files on main exactly as found.

Commit:

```powershell
git add evidence/v1/2026-08-24
git commit -m "docs(v1): freeze release candidate gate"
```

## Task 8: Hardware handoff, not automatic execution

Do not flash, erase QSPI, merge main, tag, push, delete branches or remove worktrees as part of software execution.

After Task 7, report the exact Release BIN and hash and ask the user to connect ST-Link/UART for a backed-up flash operation. Hardware validation uses static/backpack, 10 spaced enclosure taps and 5 soft-drop events; it does not require another road test. Formal `v1.0.0` integration remains a separate explicit authorization after hardware PASS.
