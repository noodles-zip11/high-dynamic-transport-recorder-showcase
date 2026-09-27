# Phase 4 Manual Command Pack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver three independent, absolute-path-safe commands for read-only SWD preflight, ten RC-013 cold boots, and exact verified-v1 restore/acceptance without weakening Phase 4 gates.

**Architecture:** Keep user-facing entry points small and separate. A shared Python module owns immutable evidence paths, subprocess capture, fixed hardware identity, hashes, and TERP readback; three thin CLI scripts own their individual workflows. All hardware mutations remain confined to the explicit v1 restore command.

**Tech Stack:** Python 3.12, pyserial/TERP host client, STM32CubeProgrammer CLI, pytest, PowerShell only as the operator shell.

---

## File map

- Create `scripts/phase4_manual_common.py`: shared identities, hashes, immutable evidence creation, subprocess logging, SWD read-only probe, and RC-013 TERP readback.
- Create `scripts/phase4_swd_preflight.py`: read-only SWD command.
- Create `scripts/phase4_cold_boot_matrix.py`: resumable ten-round physical cold-boot command.
- Create `scripts/phase4_restore_v1.py`: verified-v1 program/readback/runtime-acceptance command.
- Create `scripts/tests/test_phase4_manual_commands.py`: unit and CLI contract tests with fake subprocess/readback seams.
- Modify `scripts/terp_physical_matrix.ps1`: retain the already-debugged native-exit capture, repository-root independence, and fail-fast behavior under regression tests.

### Task 1: Shared evidence helpers and SWD preflight

**Files:**
- Create: `scripts/phase4_manual_common.py`
- Create: `scripts/phase4_swd_preflight.py`
- Create: `scripts/tests/test_phase4_manual_commands.py`

- [ ] **Step 1: Write failing tests for immutable output and explicit SWD identity**

Add tests that create a temporary session, verify a new output directory is accepted, verify an existing result file raises `FileExistsError`, and inject a fake command runner whose `-l stlink`, UID read, and application-word read contain serial `DEVICE_SERIAL_REDACTED__`. Assert the parsed result contains `pass=True`, the explicit serial, UID words, application words, and no mutating programmer flags (`-w`, `-v`, `-rst`, erase or unlock).

- [ ] **Step 2: Run the focused tests and confirm RED**

Run:

```powershell
& 'LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv\Scripts\python.exe' -m pytest scripts/tests/test_phase4_manual_commands.py -q
```

Expected: import failure for `phase4_manual_common` or missing SWD functions.

- [ ] **Step 3: Implement the minimum shared API and preflight CLI**

Implement constants `EXPECTED_STLINK_SERIAL =
"DEVICE_SERIAL_REDACTED__"`, `APPLICATION_ADDRESS = 0x08020000`, and
`UID_ADDRESS = 0x1FF1E800`. Define the stable functions
`sha256_file(path: Path) -> str`, `require_fresh_file(path: Path) -> None`,
`run_logged(command: list[str], stdout_path: Path, stderr_path: Path) -> int`,
and `collect_swd_preflight(programmer: Path, probe_serial: str, output_dir:
Path, runner: CommandRunner = run_logged) -> dict[str, object]`.

`collect_swd_preflight` must run exactly `-l stlink`, a 3-word read at `0x1FF1E800`, and a 4-word read at `0x08020000`; preserve stdout/stderr/exit codes; reject missing serial, blank firmware, wrong device, non-3.0-to-3.6 V target voltage, missing UID words, or missing application words; write `swd-preflight.json` only after collecting the first failure.

The CLI requires absolute `--programmer`, explicit `--probe-serial`, and absolute `--output-dir`. It exits 0 only when `pass=true`.

- [ ] **Step 4: Run tests and syntax checks**

Run the focused pytest command plus:

```powershell
& 'LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv\Scripts\python.exe' -m py_compile scripts/phase4_manual_common.py scripts/phase4_swd_preflight.py
```

Expected: PASS.

- [ ] **Step 5: Commit Task 1**

```powershell
git add scripts/phase4_manual_common.py scripts/phase4_swd_preflight.py scripts/tests/test_phase4_manual_commands.py
git commit -m "feat(reliability): add read-only SWD preflight"
```

### Task 2: Resumable RC-013 cold-boot matrix

**Files:**
- Modify: `scripts/phase4_manual_common.py`
- Create: `scripts/phase4_cold_boot_matrix.py`
- Modify: `scripts/tests/test_phase4_manual_commands.py`

- [ ] **Step 1: Write failing tests for preflight, prompts, resume, and first-failure preservation**

Use fake input and a fake TERP readback callback. Assert:

- the initial readback must match capability 491, serial `recorder-001`, event 100, 38,560 bytes, device/download CRC equality, and one baseline SHA-256 before the first prompt;
- each physical round requires exact `OFF` then `READY` tokens and records at least five seconds off time;
- an existing PASS round is skipped, an existing FAIL round aborts, and no existing round JSON is overwritten;
- the first readback mismatch writes that round as FAIL and does not request another physical cycle;
- diagnostic mode performs readback but records `physical_confirmation=false` and cannot return formal PASS.

- [ ] **Step 2: Run focused tests and confirm RED**

Run the focused pytest command. Expected: missing cold-boot runner interfaces.

- [ ] **Step 3: Implement the cold-boot runner**

Implement constants `RC013_CAPABILITY_FLAGS = 491`, `RC013_EVENT_ID = 100`,
and `RC013_EVENT_BYTES = 38560`. Define
`collect_rc013_readback(port: str, output_dir: Path, label: str = "readback")
-> dict[str, object]` and `run_cold_boot_matrix(port: str, output_dir: Path,
rounds: int = 10, diagnostic: bool = False, input_fn: Callable[[str], str] =
input, clock: Callable[[], float] = time.monotonic, sleep_fn:
Callable[[float], None] = time.sleep, readback_fn: ReadbackFn =
collect_rc013_readback) -> dict[str, object]`.

Use one TERP session per readback and preserve raw TX/RX plus frame summaries. The preflight establishes event length, CRC, and SHA. Every round must match that baseline and the frozen identity. Store `round-XX.json` with physical confirmations and timestamps before continuing. Resume only over existing PASS rows; an existing FAIL is an explicit blocker. Write aggregate `results.json` only when all requested rounds PASS.

The CLI requires absolute `--output-dir`, explicit `--port COM13`, `--rounds 10`, and supports `--diagnostic` only for agent preflight.

- [ ] **Step 4: Verify from a non-project working directory**

Run tests, `py_compile`, and from `LOCAL_USER_HOME\Desktop` invoke `--help`. Then run `--diagnostic` against the live RC-013 into a fresh external directory. Expected: path/import independence and `DIAGNOSTIC_NOT_PHYSICAL`, with event 100 readback success.

- [ ] **Step 5: Commit Task 2**

```powershell
git add scripts/phase4_manual_common.py scripts/phase4_cold_boot_matrix.py scripts/tests/test_phase4_manual_commands.py
git commit -m "feat(reliability): add resumable cold boot matrix"
```

### Task 3: Exact verified-v1 restore and acceptance

**Files:**
- Modify: `scripts/phase4_manual_common.py`
- Create: `scripts/phase4_restore_v1.py`
- Modify: `scripts/tests/test_phase4_manual_commands.py`

- [ ] **Step 1: Write failing tests for hash binding, programmer order, readback, and fail-stop**

Inject fake programmer and acceptance runners. Assert the command:

- rejects any v1 file not exactly 219,120 bytes with SHA-256 `83584fd8b3bba44619d315d75e3140aebc706ba8b406f3e5e04cdfd1a17f9112` before SWD mutation;
- runs SWD preflight before `-w`;
- programs only `0x08020000` using
  `-w C:\transport-recorder-evidence\2026-08-28\reliability-evidence-rc-013\baseline\h0-rc013\restore-v1-readback-exact.bin 0x08020000 -v -rst`;
- runs exact `-u 0x08020000 219120
  C:\transport-recorder-evidence\2026-08-28\reliability-evidence-rc-013\restore-v1-command\v1-readback.bin`
  and requires identical SHA-256;
- invokes `hardware_acceptance_readback.py` on explicit COM13 and requires `pass=true`, capability 235, serial `recorder-001`, healthy storage, and non-empty events;
- stops after the first nonzero exit or mismatch and never performs a second programming attempt.

- [ ] **Step 2: Run focused tests and confirm RED**

Run the focused pytest command. Expected: missing restore interfaces.

- [ ] **Step 3: Implement restore with an explicit acknowledgement token**

Implement constants `V1_BYTES = 219120`, `V1_SHA256 =
"83584fd8b3bba44619d315d75e3140aebc706ba8b406f3e5e04cdfd1a17f9112"`, and
`RESTORE_TOKEN = "RESTORE_VERIFIED_V1"`. Define
`restore_verified_v1(programmer: Path, probe_serial: str, port: str,
v1_image: Path, output_dir: Path, confirmation: str, runner: CommandRunner =
run_logged) -> dict[str, object]`.

Reject any other confirmation token. Preserve SWD preflight, program log, exact readback binary/log, hash comparison, acceptance stdout/stderr, and final parsed JSON. Do not update package manifests or delete prior evidence.

- [ ] **Step 4: Run synthetic verification only**

Run pytest and `py_compile`. Invoke `--help` from Desktop. Do not run the live restore during implementation; the operator command is the explicit mutation boundary.

- [ ] **Step 5: Commit Task 3**

```powershell
git add scripts/phase4_manual_common.py scripts/phase4_restore_v1.py scripts/tests/test_phase4_manual_commands.py
git commit -m "feat(reliability): add verified v1 restore command"
```

### Task 4: Lock down the prior UART3 harness fixes and final delivery

**Files:**
- Modify: `scripts/tests/test_phase4_manual_commands.py`
- Modify: `scripts/terp_physical_matrix.ps1`

- [ ] **Step 1: Add regression assertions for the existing fixes**

Assert the PowerShell source disables native-command terminating conversion, switches to `$ProjectRoot`, and throws immediately when a round status is FAIL. Parse the script with the PowerShell parser.

- [ ] **Step 2: Run focused and adjacent tests**

Run:

```powershell
& 'LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv\Scripts\python.exe' -m pytest scripts/tests/test_phase4_manual_commands.py scripts/tests/test_phase4_collector.py scripts/tests/test_phase4_evidence.py -q
git diff --check
```

Expected: PASS.

- [ ] **Step 3: Commit the UART3 harness regression**

```powershell
git add scripts/terp_physical_matrix.ps1 scripts/tests/test_phase4_manual_commands.py
git commit -m "fix(reliability): harden physical matrix runner"
```

- [ ] **Step 4: Review and operator delivery**

Run specification review first, then code-quality review, both with Luna Max subagents. The main agent checks the worktree, reruns the focused verification from Desktop, and delivers exactly three absolute one-line commands in their required order. Do not claim H0-H5 or Release PASS.
