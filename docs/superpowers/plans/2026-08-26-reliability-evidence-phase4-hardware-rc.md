# Reliability Evidence Phase 4 Hardware/RC Implementation Plan

> **For agentic workers:** Use `executing-plans` and
> `test-driven-development`. Do not commit, push, merge, create a PR, flash a
> board, trigger a processor fault, erase media, or unlock a Release candidate
> without explicit authorization and the gates defined below.

**Goal:** Add the smallest isolated FaultInjection and evidence tooling needed to
validate the Phase 0–3 reliability work on STM32H743 hardware, while keeping
normal Debug, default-off, and V1 Release products free of injection paths. A
reliability-enabled Release candidate remains mechanically locked until a
same-revision physical-board gate manifest is complete and passes audit.

**Architecture:** Split Phase 4 into a software-preparation gate (`S0–S4`) and a
physical-board gate (`H0–H5`). Software work may produce a sealed
FaultInjection image and a dry-run evidence package, but it may not create a
Release reliability PASS. Hardware work consumes that exact image, records raw
UART/SWD/TERP evidence, and produces a machine-readable manifest. Only a strict
audit of a same-revision, `physical_board/PASS` manifest may authorize a
reliability-enabled Release build; injection sources remain excluded from that
Release build and are checked independently.

**Source of truth:**
`docs/superpowers/specs/2026-08-26-reliability-evidence-design.md`, especially
“FaultInjection profile”, “Phase 4：实板故障注入与发布候选”, the automated
non-regression matrix, the physical-board matrix, and the evidence/release rules.

**Current environment boundary (2026-08-26):** STM32CubeProgrammer 2.19.0 is
installed, but discovery found no ST-Link, J-Link, DFU device, or project UART;
only Bluetooth COM3/COM4 were present. Therefore `S0–S4` may be implemented and
verified now. `H0–H5`, final evidence audit, and Release reliability enablement
must remain `NOT_EXECUTED/BLOCKED_HARDWARE`, never PASS.

## Frozen safety and build contract

- Build profiles are exactly `Debug`, `Release`, and `FaultInjection`.
- `FaultInjection` uses Debug optimization/debug information, forces
  `TRANSPORT_RELIABILITY_EVIDENCE_ENABLED=1`, and is the only profile defining
  `TRANSPORT_FAULT_INJECTION_ENABLED=1`.
- Normal Debug and all Release builds exclude injection source files at the
  SCons source-list boundary; a function-body `#ifdef` alone is insufficient.
- The only processor-fault command is
  `reliability_inject <hardfault|memmanage|busfault|usagefault> CONFIRM_RESET`.
  An exact confirmation token is mandatory. The command prints one armed record,
  issues barriers, then triggers the real Cortex-M exception path; it must never
  call a CrashRecord handler directly.
- Event fault cases are one-shot and profile-only. They may exercise existing
  event-quality seams, but they do not count as physical IMU/DMA evidence.
- Debug/Release expose no injection command, case strings, registration symbol,
  source object, or reachable injection function.
- No V1 TERP IDs/payloads, EV03/EL01 bytes, AI sidecar v2, memory ownership,
  pin/clock/DMA/task priority, OTA/model assets, or `v1.0.0` tag may change.
- A Release reliability build requires an explicit Phase 4 gate manifest whose
  revision equals `TRANSPORT_GIT_REVISION`. Every required hardware gate must be
  `status=PASS` and `evidence_level=physical_board`; `SMOKE_PASS`, `PARTIAL`,
  `DIAGNOSTIC`, `NOT_EXECUTED`, dry-run, missing evidence, or revision drift
  rejects the build before SCons.
- Release injection absence is checked again after the Release ELF exists.
- CrashRecord v1 claims reset retention only; full power-loss persistence is not
  a release claim.

## Evidence contract

Use an external, read-only session staging root such as
`C:\transport-recorder-evidence\<date>\reliability-evidence-rc-<nnn>\` while
running the same-revision gate. This keeps the source worktree clean. After RC
audit, a separate evidence-only change may mirror the immutable package under
`evidence/hardware-bringup/...`; that later evidence revision is recorded
separately and never substitutes for `source_revision`.

```text
metadata.json
operator-checklist.md
firmware/{fault-injection.*,release.*,hashes.json}
baseline/{v1-tag.json,old-client/,default-off/}
faults/{hardfault,memmanage,busfault,usagefault}/
events/{sequence-gap,pretrigger-short,duration-cap,pool-pressure,queue-pressure}/
storage/{u2,qspi-ota-model-sidecar}/
reconnect/
results/{software-gates.json,hardware-gates.json,resource-report.json}
summary.md
manifest.json
SHA256SUMS.txt
```

Raw UART/SWD/TERP logs are immutable. Parsed JSON is a separate derived file.
`manifest.json` excludes itself and `SHA256SUMS.txt`; `SHA256SUMS.txt` covers the
manifest and every other file except itself. Each gate records schema version,
case, status, evidence level, source revision, profile/feature switches, artifact
hash/build ID, board/PCB/MCU/probe/UART/media identity, timestamps/timezone,
reset source, iteration count, expected/actual result, raw evidence paths, and
failure/rollback notes.

### Task S0: Freeze the candidate and evidence schema

- [ ] Add a small schema/validator for Phase 4 gate manifests. Freeze required
  software gates and hardware gates `H0` through `H5`. The software manifest has
  distinct rows for every automated matrix entry in the approved specification:
  V1 baseline, memory layout, EV03/EL01, event gate, AI, TERP, CrashRecord,
  Release profile, controlled enablement, resource regression, and Host/evidence.
- [ ] Require the immutable V1 tag peel
  `a69b6c6c71b91e1267780c169760eca284b7523b`, current source revision, default-off
  fallback artifact hashes, and exact FaultInjection artifact hashes.
- [ ] Generate a dry-run skeleton with every hardware gate
  `NOT_EXECUTED/BLOCKED_HARDWARE`; validation must reject it for RC use.
- [ ] Add focused tests for valid schema, missing gate/evidence, non-physical PASS,
  revision mismatch, duplicate case, path escape, and hash mismatch.

### Task S1: Add the isolated FaultInjection profile

- [ ] Extend `firmware/rtconfig.py`, `firmware/SConstruct`,
  `firmware/build_options.py`, and `scripts/build_firmware.ps1` with the exact
  three-profile contract above.
- [ ] Add one feature-only diagnostics module for the four processor-fault cases
  and a bounded status/readback command. Compile it only in FaultInjection.
- [ ] Use CMSIS registers/instructions to enter the real exception vectors. Do not
  call RT-Thread, logging, storage, TERP, AI, or allocation after the armed marker
  and before the fault instruction/barrier sequence.
- [ ] Add the minimum one-shot event seams needed for board orchestration. Label
  their evidence `hil_injection`, never `physical_sensor`.
- [ ] Add focused build-option/source-isolation/native tests. Do not run or flash
  the processor-fault cases during software verification.

### Task S2: Prove Release contains no injection path

- [ ] Add `scripts/check_fault_injection_absent.ps1` using the existing toolchain
  discovery and the Phase 2 checker pattern without refactoring that checker.
- [ ] Check `compile_commands.json`, linker map, `objdump -t/-r/-d`, FinSH command
  registration, and `strings` for the injection macro, source/object, symbol
  prefix, command name, confirmation token, and case strings.
- [ ] Call the checker from `scripts/release_gate.ps1` after the Release build.
- [ ] Keep Release reliability locked unless a manifest passes Task S0 validation
  for the same source revision. A failed/missing manifest must reject both in the
  PowerShell wrapper and inside canonical SCons/build policy; direct SCons must not
  bypass the lock. Pass the absolute external manifest path through a dedicated
  environment variable and treat it as read-only input.
- [ ] Keep `source_revision`, external evidence-package hash/revision, and any later
  evidence-only Git revision as separate fields. Build the RC from a clean worktree
  checked out at `source_revision`; never require generated hardware evidence to be
  uncommitted inside that worktree.
- [ ] Test the checker against synthetic positive/negative fixtures and a real
  normal Release ELF. Do not enable Reliability Release in the current no-board
  environment.

### Task S3: Add a dedicated Phase 4 collector/package tool

- [ ] Reuse the raw UART/frame tracing and SHA helpers from existing hardware
  tooling, but do not reuse its hard-coded V1 identity/capability assertions.
- [ ] Add additive Host CLI/collector operations for event evidence, exact 128B
  CrashRecord download, and sequence-exact ACK. Preserve raw bytes and PC/LR even
  when symbolization fails.
- [ ] Provide `--dry-run` which writes the full directory skeleton and commands but
  marks hardware gates non-PASS. Never auto-select a COM port, auto-flash, auto-ACK,
  or auto-trigger a fault.
- [ ] Record pre/post hashes for U2/QSPI/OTA/model/sidecar ownership checks and
  resource/timing fields without inventing missing measurements.
- [ ] Add manifest and `SHA256SUMS.txt` generation/verification tests, including
  immutable failure logs and path-boundary checks.

### Task S4: Seal the software-preparation gate

Run one software gate only after S0–S3:

```powershell
$env:TRANSPORT_VENV_ROOT='LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv'
pwsh scripts/run_tests.ps1
pwsh scripts/build_firmware.ps1 -BuildProfile Debug -RequireElf `
  -SaveReliabilityOffBaseline -ArtifactOutputDirectory <session-root>\firmware\default-off
pwsh scripts/build_firmware.ps1 -BuildProfile Debug -ReliabilityEvidence -RequireElf `
  -ArtifactOutputDirectory <session-root>\firmware\debug-reliability
pwsh scripts/build_firmware.ps1 -BuildProfile FaultInjection -RequireElf `
  -ArtifactOutputDirectory <session-root>\firmware\fault-injection
pwsh scripts/check_crash_fault_context.ps1
pwsh scripts/build_firmware.ps1 -BuildProfile Release `
  -GitRevision <current-40-char-sha> -DeviceSerial phase4-software-gate -RequireElf `
  -ArtifactOutputDirectory <session-root>\firmware\release-off
pwsh scripts/check_fault_injection_absent.ps1
python scripts/phase4_evidence.py create --dry-run --output <session-root>
python scripts/phase4_evidence.py verify --input <session-root> --for-release
git diff --check
```

The dry-run `--for-release` verification must fail specifically because H0–H5 are
not physical PASS. That expected refusal is the software-preparation success
condition; it is not a Phase 4 or RC PASS.

Every build copies ELF/BIN/MAP/compile database immediately after its checks to a
profile-specific session directory, writes size/SHA-256/build identity, then makes
those files read-only. Later builds may overwrite `firmware/build`, but H0 and all
manifest rows may reference only the sealed FaultInjection paths/hashes. Board
programming tools must require that explicit sealed BIN path.

### Task H0: Establish the exact-board baseline

- [ ] Require explicit operator confirmation, identify board/PCB/MCU UID,
  ST-Link, USB-TTL, supply, COM/115200 8N1, U2 JEDEC, and reset controls.
- [ ] Back up internal Flash twice with matching hashes before programming.
- [ ] Program and read back the exact sealed FaultInjection BIN; record hashes.
- [ ] Stop on identity/hash mismatch. Do not guess a COM port or erase QSPI/U2.

### Task H1: Validate four exceptions and reset retention

- [ ] Run each processor fault separately with the exact confirmation token.
- [ ] Prove actual vector/fault kind, stacked frame, SCB registers, non-returning
  reset, early recovery, raw 128B export, build ID, sequence, first/duplicate/stale
  ACK behavior, and record preservation before ACK.
- [ ] Validate software, watchdog, and supported system-reset retention separately;
  label full power removal outside the CrashRecord v1 claim.
- [ ] Measure fault-to-reset, reconnect, recovery, and watchdog margin. A secondary
  fault, wrong exception escalation, invalid frame, or timeout fails H1.
- [ ] Make IRQ state, I/D cache, MPU, and FPU/basic-versus-extended frame cases
  explicit H1 sub-gates. Exercise every supported combination and record the
  unsupported combinations with a reviewed evidence-backed N/A policy; silent
  omission is not allowed.
- [ ] Validate repeated faults before ACK and after ACK, capture flags for complete
  and incomplete frames, and prove startup initialization does not clear the
  retained slot. A repeated/secondary fault that prevents bounded recovery fails
  H1 unless the approved design explicitly marks and handles it.

### Task H2: Validate event quality on the real acquisition path

- [ ] Exercise ICM45686 FIFO/DMA loss, real sequence gaps, insufficient pretrigger,
  duration cap, pool pressure, and queue pressure.
- [ ] For every committed event, prove live facts, EV03 decode, EL01 readback,
  verdict/reasons, and TERP evidence agree. Non-PASS must not produce a normal AI
  prediction; PASS must preserve existing AI behavior.
- [ ] Keep profile-generated one-shot evidence separate from physical sensor/DMA
  evidence. Synthetic injection cannot close H2 by itself.

### Task H3: Validate storage and partition non-interference

- [ ] Validate W25Q64 JEDEC/page-write/4KiB erase/WIP, transaction interruption and
  approved physical power-loss windows using the existing guarded scratch policy;
  retain SPI timing evidence from a logic analyzer or equivalent raw capture.
- [ ] Execute guarded, read-back-verified physical QSPI read/write in its approved
  scratch range, then exercise the real candidate and recovery flows plus model and
  sidecar runtime reads. A hash-only check without those flows is NOT_EXECUTED.
- [ ] Prove QSPI candidate/recovery/model/sidecar before/after hashes and ownership
  are unchanged by CrashRecord/fault testing, and that no Backup SRAM operation
  reaches U2/QSPI/internal-Flash regions. Preserve all first-failure logs.

### Task H4: Validate physical UART3/TERP compatibility

- [ ] On PD8/PD9, verify reset reconnect, new capability/operations, chunk CRC and
  identity, Crash raw export/ACK, and legal DEGRADED/INVALID event display.
- [ ] Run a client exported from immutable `v1.0.0^{}` for HELLO/HEALTH/LIST/event
  download/48B AI behavior. The current compatible client is not proof of an old
  client.
- [ ] Perform the approved physical disconnect/resume matrix; a dry-run prompt or
  loopback result cannot count as physical PASS.

### Task H5: Close resources and existing V1 hardware gates

- [ ] Record ROM/RAM/heap/stack/DMA boundaries, startup recovery time, event-path
  timing, fault/reset/reconnect latency, watchdog margin, and build warnings.
- [ ] Re-run applicable V1 hardware/long-run/power-loss gates on the exact candidate
  artifact. Keep previously documented V1 limitations explicit.

### Task R0/R1: Audit evidence and create the Release candidate

- [ ] Validate every software and H0–H5 gate as PASS with physical evidence, exact
  revision, complete hashes, and no unresolved anomaly. Anything else rejects RC.
- [ ] Only then build `Release + ReliabilityEvidence` with the audited manifest.
- [ ] Re-run Release injection-absence, V1 compatibility, resource, boot/readiness,
  and physical old-client smoke checks on the exact RC artifact.
- [ ] On any failure, reject the RC and retain the frozen V1/default-off artifacts.
  Do not weaken a gate or rewrite evidence to obtain PASS.

## Current stop condition

After S0–S4, stop with `BLOCKED_HARDWARE` if no target board/probe/project UART is
available. Do not mark Phase 4 complete and do not remove the Release lock.
