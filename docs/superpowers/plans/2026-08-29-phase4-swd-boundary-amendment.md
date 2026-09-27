# Phase 4 SWD Boundary Amendment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the false HOTPLUG PASS path, keep the cold-boot matrix independent, and move reliable NORMAL SWD access inside the acknowledged verified-v1 restore boundary.

**Architecture:** Keep the three existing CLI entry points. Harden the shared SWD parser for future diagnostics, but do not ask the operator to repeat command 1. Replace restore-time HOTPLUG memory preflight with probe enumeration plus a read-only frozen RC-013 TERP check, then use explicit NORMAL connections only after the restore token has been validated.

**Tech Stack:** Python 3.12, pytest, STM32CubeProgrammer CLI 2.19, TERP/pyserial, PowerShell operator shell.

---

### Task 1: Reject false HOTPLUG target evidence

**Files:**
- Modify: `scripts/phase4_manual_common.py`
- Modify: `scripts/tests/test_phase4_manual_commands.py`

- [ ] **Step 1: Add failing real-output regression tests**

Add fixtures containing the captured 2026-08-29 outputs. Assert that output
with `Device ID : 0x800`, any line beginning `Error:`, repeated UID words, or
application words other than `24001600 08024819 0802486D 0802041D` cannot
produce `pass=true` even when the injected runner exits zero.

- [ ] **Step 2: Run the focused tests and confirm RED**

```powershell
& 'LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv\Scripts\python.exe' -m pytest scripts/tests/test_phase4_manual_commands.py -q
```

Expected: the captured `0x800`/`Error:` case is incorrectly accepted before
the implementation change.

- [ ] **Step 3: Implement strict per-connection validation**

Define frozen tuples:

```python
EXPECTED_UID_WORDS = (0x003E0026, 0x3433510C, 0x34393738)
RC013_APPLICATION_WORDS = (0x24001600, 0x08024819, 0x0802486D, 0x0802041D)
```

For both UID and application command output, reject CubeProgrammer `Error:`
lines, require device ID `0x450`, STM32H7xx identity, and 3.0--3.6 V. Require
the exact frozen word tuple for the command being evaluated. Preserve the first
failure and immutable evidence files.

- [ ] **Step 4: Verify and commit**

Run focused pytest, `py_compile`, and `git diff --check`, then commit only the
two Task 1 files as `fix(reliability): reject invalid SWD read evidence`.

### Task 2: Move reliable SWD access into the restore boundary

**Files:**
- Modify: `scripts/phase4_manual_common.py`
- Modify: `scripts/phase4_restore_v1.py`
- Modify: `scripts/tests/test_phase4_manual_commands.py`

- [ ] **Step 1: Add failing restore-boundary tests**

Assert the restore flow, in order:

1. validates `RESTORE_VERIFIED_V1`, exact image length/SHA, explicit probe
   serial, and COM13;
2. runs only `-l stlink` for pre-boundary SWD evidence;
3. runs an injected read-only RC-013 TERP check and requires the frozen 491,
   `recorder-001`, event 100 length/CRC/SHA result;
4. then runs exactly one program command using explicit `mode=NORMAL` and
   `reset=SWrst`, `-w <image> 0x08020000 -v -rst`;
5. runs the exact NORMAL readback and hash comparison;
6. runs released-v1 COM13 acceptance.

Assert no target-memory SWD command, reset, or write occurs before the frozen
image, probe enumeration, RC-013 TERP result, and confirmation token pass.

- [ ] **Step 2: Run the focused tests and confirm RED**

Run the focused pytest command. Expected: current restore still calls the
HOTPLUG memory preflight and uses HOTPLUG for program/readback.

- [ ] **Step 3: Implement the minimum boundary change**

Add an enumeration-only helper that records `-l stlink`, requires exactly the
frozen probe serial and nonblank firmware, and writes immutable evidence. Inject
the existing `collect_rc013_readback` seam into `restore_verified_v1`. Replace
the restore-time `collect_swd_preflight` call with enumeration plus RC-013 TERP
validation. Change program and upload connect tokens to explicit
`mode=NORMAL`, `reset=SWrst`; keep one program attempt and all existing exact
readback/runtime acceptance gates.

- [ ] **Step 4: Verify, review, and commit**

Run focused and adjacent Phase 4 tests, `py_compile`, all three absolute-path
`--help` calls from Desktop, PowerShell parsing, and `git diff --check`. Commit
Task 2 as `fix(reliability): isolate SWD restore boundary`. A Luna Max spec
reviewer checks the amendment, then the main agent performs final review.

### Task 3: Operator handoff

**Files:**
- No source modification.

- [ ] **Step 1: Preserve the invalid sessions**

Confirm both existing SWD preflight directories remain unchanged. Record command
1 as probe enumeration success and target-memory evidence invalid; do not ask
the operator to repeat it.

- [ ] **Step 2: Deliver command 2 only**

Give one absolute PowerShell command for the ten-round cold-boot matrix using a
fresh external output directory. Stop and wait for the operator at its OFF and
READY prompts. Do not provide the v1 restore command until the matrix result is
reviewed.
