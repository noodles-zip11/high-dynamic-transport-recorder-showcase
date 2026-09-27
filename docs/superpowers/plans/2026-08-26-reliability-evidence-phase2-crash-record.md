# Reliability Evidence Phase 2 CrashRecord Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` and
> `test-driven-development`. Repository rules override normal commit cadence: do
> not commit, push, merge, create another worktree, or open a PR without explicit
> authorization.

**Goal:** Add a default-off CrashRecord v1 implementation that atomically records
processor faults in two fixed Backup SRAM slots, recovers the newest valid record
after reset, and exposes an internal readiness/query/ACK boundary for Phase 3.

**Architecture:** Keep encoding, validation, A/B selection, commit-last writes, and
ACK policy in a small RTOS-independent module. Put STM32H743 Backup SRAM enablement,
fixed-address storage, exception entry, SCB capture, barriers, and reset in a
feature-only target adapter. The normal V1 linker and RT-Thread fault path remain
the exact default-off path. An enabled Debug build adds a linker overlay and
recompiles only the existing CMSIS startup source with four preprocessor symbol
substitutions so its vector words point directly at feature-only CrashRecord entries.
All other objects, including the existing RT-Thread fault handlers, are compiled
unchanged.

**Tech Stack:** C11, Cortex-M7 GNU assembly, STM32H743 CMSIS registers, GNU ld,
SCons, PowerShell, native GCC, ARM GCC.

**Source of truth:**
`docs/superpowers/specs/2026-08-26-reliability-evidence-design.md`, especially
“CrashRecord v1”, “Fault context 规则”, and “Phase 2：CrashRecord native + linker”.

**Non-negotiable boundary:** Do not change EV03/EL01, QSPI, internal Flash,
Bootloader, OTA, model slots, TERP v1, AI sidecar v2, pins, clocks, task priorities,
or the Release lock. Do not add FaultInjection commands in this phase. With
`TRANSPORT_RELIABILITY_EVIDENCE_ENABLED=0`, do not compile CrashRecord code, add
fault wrapping, select the CrashRecord linker overlay, or alter the existing
RT-Thread fault entry.

---

## Fixed Phase 2 representation

- Backup SRAM is the STM32H743 4 KiB region beginning at `0x38800000`, as defined
  by the local CMSIS `D3_BKPSRAM_BASE` contract.
- Reserve only the first 512 bytes: slot A at offset `0x000`, slot B at `0x100`.
  Each slot is 256 bytes; no existing Flash, AXI SRAM, D2 SRAM, QSPI, stack, heap,
  or DMA range changes.
- `crash_record_v1_t` is 128 bytes. Fixed offsets are:
  `magic@0`, `format_version@4`, `header_length@6`, `record_length@8`,
  `sequence@12`, `fault_kind@16`, `capture_flags@20`, `exc_return@24`, `sp@28`,
  `r0@32`, `r1@36`, `r2@40`, `r3@44`, `r12@48`, `lr@52`, `pc@56`, `xpsr@60`,
  `cfsr@64`, `hfsr@68`, `shcsr@72`, `mmfar@76`, `bfar@80`, `reset_flags@84`,
  `build_id[32]@88`, `crc32@120`, `commit_marker@124`.
- `ack_marker` is a separate 32-bit word at slot offset `128`; bytes 132–255 are
  reserved and never interpreted by v1.
- `header_length=32`, `record_length=128`, `sequence!=0`. CRC32 covers bytes
  0–119 and excludes CRC, commit, ACK, and reserved bytes. All multi-byte values
  are native H743 little-endian and locked by native golden bytes/static asserts.
- Concrete magic/version/commit constants are internal implementation constants;
  tests freeze them before Phase 3 exposes raw records.

## Proposed file map

- Create `firmware/app/reliability/crash_record.h` and `.c`: fixed structure,
  CRC32, bounded validation, wrap-safe newest selection, target selection,
  commit-last transaction, recovery, and sequence-exact idempotent ACK. No
  RT-Thread, HAL, heap, logging, storage, TERP, or AI dependency.
- Create `firmware/bsp/openmv4_h743/crash_record_target.h` and `.c`: Backup SRAM
  clock/regulator initialization, non-destructive bounded selfcheck, fixed slot
  access, early recovery registration, direct SCB/RCC capture, barriers, and
  direct SYSRESETREQ fallback loop.
- Create `firmware/bsp/openmv4_h743/crash_fault_entry.S`: four feature-only wrapper
  entries for HardFault, MemManage, BusFault, and UsageFault. They select MSP/PSP,
  preserve `EXC_RETURN`, pass the exact `fault_kind`, and never return.
- Create `firmware/bsp/openmv4_h743/linker_scripts/crash_record_overlay.lds`:
  `NOLOAD` section at `0x38800000`, 512-byte exact-size/alignment assertions, and
  exported A/B/end symbols. It is added only to enabled builds.
- Modify `firmware/SConstruct`, `firmware/app/SConscript`, and
  `firmware/bsp/openmv4_h743/SConscript`: enabled-only sources, linker overlay,
  plus an enabled-only clone of the build environment used only for the CMSIS
  startup object. That clone renames the four startup vector/weak symbols to unique
  `crash_record_*` entries; default-off still builds the original startup object.
- Modify `firmware/app/runtime/app_runtime.c`: one enabled-only early init call;
  failure leaves V1 startup running and internal Crash capability readiness false.
- Create `firmware/tests/native/test_crash_record.c` and register it in the native
  build/test scripts.
- Create `scripts/check_crash_fault_context.ps1`: inspect enabled object/ELF
  undefined references, disassembly, symbols, map, and `.bin` size; reject forbidden
  fault-path calls and any default-off CrashRecord/wrap symbol.

### Task 1: Freeze the CrashRecord bytes and validation policy

**Files:** `crash_record.h`, `crash_record.c`, `test_crash_record.c`, native build
registration.

- [ ] Write failing static-offset and golden-byte tests for the 128-byte record,
  256-byte slot, separate ACK word, little-endian field placement, and CRC range.
- [ ] Add exact fault kinds `HARDFAULT`, `MEMMANAGE`, `BUSFAULT`, and
  `USAGEFAULT`; reject zero or unknown kinds.
- [ ] Implement bounded validation that first checks magic, version, header length,
  record length, nonzero sequence, fault kind, commit marker, and CRC before
  exposing any record.
- [ ] Add corruption tests for every length/version/magic/sequence/fault/CRC/commit
  rejection, including a torn record whose old commit was invalidated.
- [ ] Run only `test_crash_record.exe` until this task is green.

### Task 2: Implement A/B selection, transaction order, recovery, and ACK

**Files:** `crash_record.c`, `test_crash_record.c`.

- [ ] Test wrap-safe comparison: one valid slot, equal sequence chooses A,
  ordinary order, wrap from `UINT32_MAX` to `1`, and exact half-range ambiguity
  returns no visible record.
- [ ] Test target priority: invalid slot, then oldest ACKed slot, then oldest slot;
  never select the current newest slot under the v1 policy.
- [ ] Instrument fake-slot writes and assert exact order:
  invalid commit -> `ACK_UNACKED` -> fields -> CRC -> barrier -> valid commit.
- [ ] Implement the transaction with fixed-bound volatile writes and a supplied
  barrier primitive. Do not use struct assignment or an operation that may lower
  to `memcpy` on the fault path.
- [ ] Test recovery exposes only the newest fully valid record and returns none for
  two invalid slots or half-range ambiguity.
- [ ] Test sequence-exact ACK: first ACK writes only the independent marker,
  duplicate ACK is success without another write, stale sequence changes nothing,
  and record/CRC/commit bytes remain identical.

### Task 3: Add the feature-only Backup SRAM linker/backend boundary

**Files:** target adapter, linker overlay, SCons files, target/linker tests.

- [ ] Add the `NOLOAD` overlay with exported slot symbols and linker assertions:
  base `0x38800000`, A/B 256-byte aligned, total exactly 512 bytes, end no later
  than `0x38801000`.
- [ ] Select the overlay only when reliability evidence is enabled. Verify the
  default-off link command and ELF have no CrashRecord section/symbol.
- [ ] Implement early backend initialization using direct STM32H743/CMSIS register
  access in normal context. Enable the Backup SRAM clock and regulator, use a
  bounded non-destructive probe in an owned v1-reserved word after slot A's ACK
  marker (never in record/CRC/commit/ACK bytes), restore its previous word, and
  report ready only after fixed address/size/read-write checks pass.
- [ ] Backend init/selfcheck failure must return unavailable without clearing either
  slot, without blocking startup, and without changing any current TERP capability.
- [ ] Enabled build checks must prove `.crash_record` is `NOLOAD`, exactly 512 bytes,
  and does not inflate the firmware binary across the Backup SRAM address gap.
  Preserve the Application vector/Flash origin, RAM end, D2 region, stack, and DMA
  boundaries and retain at least the existing 16 KiB heap floor. Feature code may
  legitimately grow `_etext`/BSS and therefore move `__heap_start` within that budget.

### Task 4: Capture four faults without entering RT-Thread

**Files:** `crash_fault_entry.S`, target adapter, `SConstruct`, fault-context check
script, focused native capture tests.

- [ ] For enabled builds only, compile the existing CMSIS startup source with
  per-object preprocessor substitutions for `HardFault_Handler`,
  `MemManage_Handler`, `BusFault_Handler`, and `UsageFault_Handler`, targeting four
  unique `crash_record_*` entry symbols. Do not apply these defines to any other
  object. Off builds use the original startup source and symbols unchanged.
- [ ] Each assembly wrapper reads the active MSP/PSP from `EXC_RETURN[2]`, passes
  the unmodified `EXC_RETURN` and exact fault kind, and branches to a noreturn
  capture function. No wrapper may call the original RT-Thread handler.
- [ ] The capture function validates the basic frame address against the linker RAM
  bounds before dereference. If `EXC_RETURN[4]` indicates an FPU extended frame,
  mark it and advance by the fixed 18-word extension without reading FP registers.
  Invalid frame bounds produce zero basic registers plus a `FRAME_UNREADABLE` flag,
  not a second fault.
- [ ] Capture only fixed frame fields, SCB status registers, RCC reset flags, fixed
  build ID, and capture flags; then execute the A/B transaction.
- [ ] Request reset by direct AIRCR key/SYSRESETREQ write after DSB. If reset does
  not occur, stay in a local instruction-only infinite loop. The handler must never
  return to the faulting PC.
- [ ] The static checker must parse the final ELF vector words and prove that all
  four enabled vectors equal their `crash_record_*` symbol addresses; symbol
  presence alone is insufficient. It must also fail on RT-Thread/HAL/logging/heap/libc formatting,
  Flash/QSPI/storage/TERP/AI references, blocking calls, `memcpy`/`memset`, calls to
  the original wrapped handlers, or any unexpected undefined symbol in the wrapper
  and transitive capture/store objects.

### Task 5: Register early recovery without blocking V1 startup

**Files:** `app_runtime.c`, target adapter/public internal header, focused runtime
test seam if needed.

- [ ] Call CrashRecord backend init and recovery at the beginning of
  `app_runtime_start()` only in enabled builds, before threads/services can consume
  the result.
- [ ] Cache only readiness, visible-slot identity, and validated latest record.
  Do not clear, ACK, rewrite, print, transmit, or symbolize it during startup.
- [ ] Provide bounded internal getters and a sequence-exact ACK function for Phase 3;
  no TERP/MSH endpoint or capability bit is added yet.
- [ ] Prove backend failure, two invalid slots, ambiguous sequences, and valid
  recovery all allow the existing startup path to continue.

### Task 6: Phase 2 gate

Run the narrow checks during development. At completion, run one integrated gate:

```powershell
$env:TRANSPORT_VENV_ROOT='LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv'
pwsh scripts/test_native.ps1
python -m pytest host/tests ai/tests scripts/tests -q
pwsh scripts/build_firmware.ps1 -BuildProfile Debug -RequireElf `
  -SaveReliabilityOffBaseline
pwsh scripts/build_firmware.ps1 -BuildProfile Debug -ReliabilityEvidence -RequireElf
pwsh scripts/check_crash_fault_context.ps1
pwsh scripts/build_firmware.ps1 -BuildProfile Release `
  -GitRevision 0000000000000000000000000000000000000000 `
  -DeviceSerial phase2-lock-test -ReliabilityEvidence -RequireElf
git diff --check
git status --short
```

Acceptance requires:

- native CrashRecord format/CRC/torn-write/A/B/wrap/ACK matrix passes;
- default-off source list, link command, fault symbols, protocol, and behavior remain
  the Phase 1/V1 rollback path;
- enabled Debug build has the exact Backup SRAM section and four redirected fault
  entries, with no forbidden fault-context references;
- backend failure never advertises readiness and never blocks existing startup;
- Release-on remains rejected before compilation;
- no target-board reset-retention, vector, stack-frame, or reset claim is made from
  native/static evidence;
- no commit, push, merge, PR, or worktree cleanup occurs.

GNU ld `--wrap` was investigated and rejected because references emitted within the
startup object remained bound to the original symbols. Stop and return to architecture
review if the isolated startup-object substitutions do not deterministically replace
all four final ELF vector words, if any substitution leaks into another object, if
the overlay changes `.bin` span or any existing RAM/Flash symbol, if the capture path
needs RT-Thread/HAL/libc calls, or if backend selfcheck can destroy a valid slot.
