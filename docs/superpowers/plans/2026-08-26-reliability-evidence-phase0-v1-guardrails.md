# Reliability Evidence Phase 0 V1 Guardrails Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Repository rules override the skill's normal commit cadence: do not commit, create a branch/worktree, push, merge, or open a PR without explicit authorization.

**Goal:** Add executable guardrails that keep Reliability Evidence disabled by default and detect changes to V1 event, protocol, sidecar, and memory-layout contracts before later phases touch runtime behavior.

**Architecture:** Introduce a pure Python build-option parser consumed by SCons and unit-tested without invoking the ARM toolchain. Add a Host-side compatibility-contract test that treats existing V1 constants and schemas as an additive-only baseline. Forward the opt-in switch through the PowerShell build/test wrappers, while rejecting Release enablement until Phase 4 explicitly changes that policy.

**Tech Stack:** Python 3.12, pytest, SCons, PowerShell 7, STM32 C11 source-contract inspection.

---

## File map

- Create `firmware/build_options.py`: pure parsing and Release-safety policy for the reliability build switch.
- Modify `firmware/SConstruct`: consume the parser and emit `TRANSPORT_RELIABILITY_EVIDENCE_ENABLED` only for an explicit Debug opt-in.
- Modify `scripts/build_firmware.ps1`: expose `-ReliabilityEvidence` and forward the SCons argument.
- Modify `scripts/run_tests.ps1`: forward the same switch when a firmware build is requested.
- Create `scripts/tests/test_reliability_build_option.py`: unit tests for default-off, Debug opt-in, invalid input, and Release rejection.
- Create `host/tests/test_v1_compatibility_contract.py`: additive-only V1 schema and constant guardrails.
- Modify `firmware/tests/native/test_event_export_debug.c`: freeze the real EV03 exporter payload CRC behavior.
- Modify `firmware/tests/native/test_event_log.c`: freeze EL01 byte layout, CRC, commit, and EV03 readback behavior with independent V1 oracles.
- Modify `firmware/tests/native/test_ai_result_sidecar.c`: freeze sidecar v2 byte layout, full-field remount readback, corruption rejection, and commit-last ordering.

### Task 1: Add a pure reliability build-option policy

**Files:**
- Create: `firmware/build_options.py`
- Test: `scripts/tests/test_reliability_build_option.py`

- [ ] **Step 1: Write the failing option-policy tests**

Create `scripts/tests/test_reliability_build_option.py`:

```python
from __future__ import annotations

from pathlib import Path
import sys

import pytest


PROJECT_ROOT = Path(__file__).resolve().parents[2]
FIRMWARE_ROOT = PROJECT_ROOT / "firmware"
sys.path.insert(0, str(FIRMWARE_ROOT))

from build_options import resolve_reliability_evidence_enabled  # noqa: E402


def test_reliability_evidence_is_disabled_by_default() -> None:
    assert not resolve_reliability_evidence_enabled({}, "debug")


def test_debug_build_can_explicitly_enable_reliability_evidence() -> None:
    assert resolve_reliability_evidence_enabled(
        {"reliability_evidence": "1"}, "debug"
    )


@pytest.mark.parametrize("value", ["", "true", "yes", "2", "-1"])
def test_reliability_evidence_rejects_non_binary_values(value: str) -> None:
    with pytest.raises(RuntimeError, match="reliability_evidence must be '0' or '1'"):
        resolve_reliability_evidence_enabled(
            {"reliability_evidence": value}, "debug"
        )


def test_release_build_rejects_reliability_evidence_before_phase4() -> None:
    with pytest.raises(
        RuntimeError,
        match="Release reliability evidence remains locked until Phase 4",
    ):
        resolve_reliability_evidence_enabled(
            {"reliability_evidence": "1"}, "release"
        )
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```powershell
python -m pytest scripts/tests/test_reliability_build_option.py -q
```

Expected: collection fails with `ModuleNotFoundError: No module named 'build_options'`.

- [ ] **Step 3: Implement the minimal pure parser**

Create `firmware/build_options.py`:

```python
"""Pure build-option policy shared by SCons and host-side tests."""

from __future__ import annotations

from collections.abc import Mapping


def resolve_reliability_evidence_enabled(
    arguments: Mapping[str, str], build_profile: str
) -> bool:
    value = arguments.get("reliability_evidence", "0")
    if value not in {"0", "1"}:
        raise RuntimeError("reliability_evidence must be '0' or '1'")
    enabled = value == "1"
    if enabled and build_profile.lower() == "release":
        raise RuntimeError(
            "Release reliability evidence remains locked until Phase 4"
        )
    return enabled
```

- [ ] **Step 4: Run the focused test and verify it passes**

Run:

```powershell
python -m pytest scripts/tests/test_reliability_build_option.py -q
```

Expected: `8 passed`.

- [ ] **Step 5: Stop at the review checkpoint**

Do not commit. Report the two changed files and focused-test output to the reviewing task.

### Task 2: Wire the default-off option into SCons

**Files:**
- Modify: `firmware/SConstruct`
- Test: `scripts/tests/test_reliability_build_option.py`

- [ ] **Step 1: Add failing source-integration assertions**

Append to `scripts/tests/test_reliability_build_option.py`:

```python
def test_sconstruct_uses_the_shared_reliability_policy() -> None:
    sconstruct = (FIRMWARE_ROOT / "SConstruct").read_text(encoding="utf-8")

    assert "from build_options import resolve_reliability_evidence_enabled" in sconstruct
    assert "RELIABILITY_EVIDENCE_ENABLED =" in sconstruct
    assert "resolve_reliability_evidence_enabled(ARGUMENTS, BUILD_PROFILE)" in sconstruct
    assert 'env.Append(CPPDEFINES=["TRANSPORT_RELIABILITY_EVIDENCE_ENABLED"])' in sconstruct
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```powershell
python -m pytest scripts/tests/test_reliability_build_option.py -q
```

Expected: the new `test_sconstruct_uses_the_shared_reliability_policy` fails.

- [ ] **Step 3: Wire the parser into `firmware/SConstruct`**

After the SCons imports, add:

```python
from build_options import resolve_reliability_evidence_enabled
```

Immediately after `BUILD_PROFILE` validation, add:

```python
RELIABILITY_EVIDENCE_ENABLED = resolve_reliability_evidence_enabled(
    ARGUMENTS, BUILD_PROFILE
)
```

Immediately after the build-identity `CPPDEFINES` block, add:

```python
if RELIABILITY_EVIDENCE_ENABLED:
    env.Append(CPPDEFINES=["TRANSPORT_RELIABILITY_EVIDENCE_ENABLED"])
```

Do not add the macro to `firmware/app/SConscript`; a single global SCons definition avoids conflicting ownership and reaches BSP/linker-facing Phase 2 code later.

- [ ] **Step 4: Run the focused test and parser checks**

Run:

```powershell
python -m pytest scripts/tests/test_reliability_build_option.py -q
python -c "import sys; sys.path.insert(0, 'firmware'); from build_options import resolve_reliability_evidence_enabled as r; assert not r({}, 'debug'); assert r({'reliability_evidence':'1'}, 'debug')"
```

Expected: pytest passes and the Python assertion command exits `0` with no output.

- [ ] **Step 5: Stop at the review checkpoint**

Do not commit. Report the exact inserted SConstruct blocks and test output.

### Task 3: Forward the option through PowerShell wrappers

**Files:**
- Modify: `scripts/build_firmware.ps1`
- Modify: `scripts/run_tests.ps1`
- Test: `scripts/tests/test_reliability_build_option.py`

- [ ] **Step 1: Add failing wrapper-contract assertions**

Append to `scripts/tests/test_reliability_build_option.py`:

```python
def test_powershell_wrappers_forward_the_opt_in_without_changing_defaults() -> None:
    build_script = (PROJECT_ROOT / "scripts" / "build_firmware.ps1").read_text(
        encoding="utf-8-sig"
    )
    test_script = (PROJECT_ROOT / "scripts" / "run_tests.ps1").read_text(
        encoding="utf-8-sig"
    )

    assert "[switch]$ReliabilityEvidence" in build_script
    assert "$sconsArguments = @('-C', $firmwareDir, '-j4', '-Q')" in build_script
    assert "$sconsArguments += 'reliability_evidence=1'" in build_script
    assert "Release reliability evidence remains locked until Phase 4" in build_script
    assert "[switch]$ReliabilityEvidence" in test_script
    assert "-ReliabilityEvidence:$ReliabilityEvidence" in test_script
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```powershell
python -m pytest scripts/tests/test_reliability_build_option.py -q
```

Expected: the wrapper-contract test fails.

- [ ] **Step 3: Add the switch to `scripts/build_firmware.ps1`**

Change the parameter list to:

```powershell
param(
    [ValidateSet('Debug', 'Release')]
    [string]$BuildProfile = 'Debug',
    [string]$GitRevision = 'local',
    [string]$DeviceSerial = 'pending',
    [switch]$RequireElf,
    [switch]$ReliabilityEvidence
)
```

After the existing Release identity checks, add:

```powershell
if ($BuildProfile -eq 'Release' -and $ReliabilityEvidence) {
    throw 'Release reliability evidence remains locked until Phase 4'
}
```

Replace the direct SCons call with:

```powershell
$sconsArguments = @('-C', $firmwareDir, '-j4', '-Q')
if ($ReliabilityEvidence) {
    $sconsArguments += 'reliability_evidence=1'
}
& $SConsExe @sconsArguments
```

- [ ] **Step 4: Forward the switch from `scripts/run_tests.ps1`**

Change its parameter list to:

```powershell
param(
    [switch]$WithFirmware,
    [ValidateSet('Debug', 'Release')]
    [string]$FirmwareProfile = 'Debug',
    [string]$FirmwareRevision = 'local',
    [string]$FirmwareSerial = 'pending',
    [switch]$ReliabilityEvidence
)
```

Add this named argument to the existing `build_firmware.ps1` invocation:

```powershell
        -ReliabilityEvidence:$ReliabilityEvidence `
```

Place it before `-RequireElf` so the final command remains syntactically valid.

- [ ] **Step 5: Run the focused tests and PowerShell parser checks**

Run:

```powershell
python -m pytest scripts/tests/test_reliability_build_option.py -q
pwsh -NoProfile -Command "[void][scriptblock]::Create((Get-Content -Raw scripts/build_firmware.ps1)); [void][scriptblock]::Create((Get-Content -Raw scripts/run_tests.ps1))"
```

Expected: pytest passes; PowerShell exits `0` with no parser error.

- [ ] **Step 6: Stop at the review checkpoint**

Do not build firmware yet. Do not commit. Report wrapper changes and focused verification.

### Task 4: Freeze additive-only V1 contracts

**Files:**
- Create: `host/tests/test_v1_compatibility_contract.py`

- [ ] **Step 1: Create the compatibility-contract test**

Create `host/tests/test_v1_compatibility_contract.py`:

```python
from __future__ import annotations

from pathlib import Path
import re
import subprocess


PROJECT_ROOT = Path(__file__).resolve().parents[2]

V1_MESSAGES = {
    "HELLO": (0x0001, "[protocol_version_u8]", "[protocol_version_u8, maximum_payload_u32, maximum_chunk_u32, capabilities_u32]"),
    "GET_DEVICE_INFO": (0x0002, "[]", "[model_lp_utf8, firmware_lp_utf8, hardware_lp_utf8, serial_lp_utf8, capabilities_u32]"),
    "GET_HEALTH": (0x0003, "[]", "[state_u8, storage_ready_u8, free_log_bytes_u32, storage_errors_u32, export_errors_u32]"),
    "GET_TIME": (0x0004, "[]", "[utc_unix_seconds_i64, epoch_id_u32]"),
    "SET_TIME": (0x0005, "[utc_unix_seconds_i64]", "[utc_unix_seconds_i64, epoch_id_u32]"),
    "LIST_EVENTS": (0x0006, "[after_event_id_u32, maximum_count_u32]", "[next_event_id_u32, event_count_u16, event_info_repeated]"),
    "GET_EVENT_INFO": (0x0007, "[event_id_u32]", "[event_id_u32, total_length_u32, event_crc32_u32]"),
    "READ_EVENT_CHUNK": (0x0008, "[event_id_u32, offset_u32, requested_length_u32]", "[event_id_u32, actual_offset_u32, total_length_u32, actual_length_u32, chunk_crc32_u32, data_bytes]"),
    "DELETE_EVENT": (0x0009, "[event_id_u32]", "[empty]"),
    "START_LIVE": (0x000A, "[]", "[empty]"),
    "STOP_LIVE": (0x000B, "[]", "[empty]"),
    "GET_AI_RESULT": (0x0200, "[event_id_u32]", "[event_id_u32, model_version_u16, status_u8, class_index_u8, class_count_u8, quality_flags_u8, event_flags_u16, sample_count_u32, model_crc32_u32, confidence_f32, logits_f32x4, failure_reason_u16, reserved_u16, result_sequence_u32]"),
}


def _message_blocks() -> dict[str, tuple[int, str, str]]:
    text = (PROJECT_ROOT / "protocol" / "terp_messages.yaml").read_text(
        encoding="utf-8"
    )
    blocks = re.findall(
        r"  - id: (0x[0-9A-Fa-f]+)\n"
        r"    name: ([A-Z0-9_]+)\n"
        r"(?:    request: (\[[^\n]*\])\n)?"
        r"    response: (\[[^\n]*\])",
        text,
    )
    return {
        name: (int(message_id, 16), request or "[]", response)
        for message_id, name, request, response in blocks
    }


def test_v1_release_tag_remains_the_immutable_rollback_baseline() -> None:
    completed = subprocess.run(
        ["git", "rev-parse", "v1.0.0^{}"],
        cwd=PROJECT_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )

    assert completed.stdout.strip() == "a69b6c6c71b91e1267780c169760eca284b7523b"


def test_existing_terp_v1_messages_are_unchanged_and_additions_are_allowed() -> None:
    actual = _message_blocks()
    for name, contract in V1_MESSAGES.items():
        assert actual[name] == contract


def test_ev03_and_ai_sidecar_v2_constants_are_frozen() -> None:
    event_header = (
        PROJECT_ROOT / "firmware" / "app" / "event" / "event_export_debug.h"
    ).read_text(encoding="utf-8")
    event_service = (
        PROJECT_ROOT / "firmware" / "app" / "event" / "event_service.c"
    ).read_text(encoding="utf-8")
    sidecar_header = (
        PROJECT_ROOT
        / "firmware"
        / "app"
        / "ai_inference"
        / "ai_result_sidecar.h"
    ).read_text(encoding="utf-8")
    host_decoder = (
        PROJECT_ROOT
        / "host"
        / "transport_recorder"
        / "analysis"
        / "event_record.py"
    ).read_text(encoding="utf-8")

    for definition in (
        "#define EVENT_EXPORT_VERSION 3U",
        "#define EVENT_EXPORT_HEADER_SIZE 160U",
        "#define EVENT_EXPORT_SAMPLE_SIZE 16U",
        "#define EVENT_EXPORT_SAMPLE_RATE_HZ 1600U",
    ):
        assert definition in event_header
    assert "#define EVENT_FIXED_SAMPLE_COUNT 2400U" in event_service
    assert 'EV03_MAGIC: Final = b"EV03"' in host_decoder
    assert "EV03_VERSION: Final = 3" in host_decoder
    assert "EV03_HEADER_BYTES: Final = 160" in host_decoder
    assert "#define AI_RESULT_SIDECAR_RECORD_BYTES UINT32_C(128)" in sidecar_header
    assert "#define AI_RESULT_SIDECAR_FORMAT_VERSION UINT16_C(2)" in sidecar_header
    assert "#define AI_RESULT_SIDECAR_HEADER_BYTES UINT16_C(60)" in sidecar_header


def test_internal_flash_and_qspi_layout_are_frozen() -> None:
    layout = (
        PROJECT_ROOT / "config" / "memory_layout.yaml"
    ).read_text(encoding="utf-8")
    expected = {
        "bootloader_base": "0x08000000",
        "bootloader_size_bytes": "0x00020000",
        "application_base": "0x08020000",
        "application_size_bytes": "0x001A0000",
        "state_primary_base": "0x081C0000",
        "state_secondary_base": "0x081E0000",
        "qspi_metadata_offset": "0x00000000",
        "qspi_metadata_size_bytes": "0x00010000",
        "qspi_candidate_offset": "0x00010000",
        "qspi_candidate_size_bytes": "0x00100000",
        "qspi_recovery_offset": "0x00110000",
        "qspi_recovery_size_bytes": "0x00100000",
        "qspi_model_a_offset": "0x00210000",
        "qspi_model_a_size_bytes": "0x002F8000",
        "qspi_model_b_offset": "0x00508000",
        "qspi_model_b_size_bytes": "0x002F8000",
    }
    for key, value in expected.items():
        assert re.search(rf"^{key}: {value}$", layout, re.MULTILINE)
```

This deliberately checks existing contracts as a subset so Phase 3 can add new TERP messages without editing the V1 baseline map.

- [ ] **Step 2: Run the new compatibility test**

Run:

```powershell
python -m pytest host/tests/test_v1_compatibility_contract.py -q
```

Expected: `4 passed`.

- [ ] **Step 3: Run existing adjacent golden and layout checks**

Run:

```powershell
python protocol/generate_messages.py --check
python protocol/golden/generate.py --check
python -m pytest host/tests/test_terp_golden.py host/tests/test_ota_package_tools.py -q
pwsh scripts/test_native.ps1
```

Expected: generators exit `0`; focused Host tests pass; native suite ends with `Native C tests: PASS`.

- [ ] **Step 4: Stop at the review checkpoint**

Do not commit. Report the new contract test and all verification counts/output.

### Task 5: Phase 0 integrated verification

**Files:**
- Verify only; no new files expected.

- [ ] **Step 1: Run script and Host tests**

Run:

```powershell
python -m pytest scripts/tests -q
python -m pytest host/tests/test_v1_compatibility_contract.py host/tests/test_terp_golden.py host/tests/test_ota_package_tools.py -q
```

Expected: all selected tests pass.

- [ ] **Step 2: Run the complete local software suite**

Run:

```powershell
pwsh scripts/run_tests.ps1
```

Expected: final line `All host-side tests: PASS`.

- [ ] **Step 3: Build the ordinary Debug firmware with the switch off**

Run:

```powershell
pwsh scripts/build_firmware.ps1 -BuildProfile Debug -RequireElf
```

Expected: firmware, vector, and memory-map checks pass. The build command must not contain `reliability_evidence=1`, and the compilation database must not contain `TRANSPORT_RELIABILITY_EVIDENCE_ENABLED`.

- [ ] **Step 4: Build the isolated Debug opt-in**

Run:

```powershell
pwsh scripts/build_firmware.ps1 -BuildProfile Debug -ReliabilityEvidence -RequireElf
```

Expected: build passes; `firmware/compile_commands.json` contains `TRANSPORT_RELIABILITY_EVIDENCE_ENABLED`. Runtime behavior remains unchanged because Phase 0 adds no guarded runtime code.

- [ ] **Step 5: Verify Release enablement is rejected before invoking SCons**

Run:

```powershell
pwsh scripts/build_firmware.ps1 -BuildProfile Release -GitRevision 0000000000000000000000000000000000000000 -DeviceSerial guardrail-test -ReliabilityEvidence -RequireElf
```

Expected: command fails with `Release reliability evidence remains locked until Phase 4` before compilation.

- [ ] **Step 6: Check formatting and scope**

Run:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors. Only the nine Phase 0 files listed in this plan plus the already-approved spec/plan documents may be attributable to this task; all pre-existing user changes remain untouched. The three native test files were added during independent review to close gaps that source-level constant checks could not detect; they do not change V1 runtime code.

- [ ] **Step 7: Hand back for architecture review**

Do not commit. Provide:

- changed-file list;
- focused and full test counts;
- Debug-off and Debug-on build results;
- proof that Release-on was rejected;
- explicit statement that no runtime behavior, protocol registry, memory layout, EV03/EL01 format, AI sidecar, Bootloader, or OTA file changed.
