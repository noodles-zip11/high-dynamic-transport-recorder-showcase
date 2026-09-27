from __future__ import annotations

import hashlib
import json
import math
import subprocess
from pathlib import Path

import pytest

import scripts.phase4_restore_v1 as restore_module
from scripts.phase4_cold_boot_matrix import main as cold_boot_main
from scripts.phase4_cold_boot_matrix import run_cold_boot_matrix
from scripts.phase4_manual_common import (
    APPLICATION_ADDRESS,
    EXPECTED_STLINK_SERIAL,
    UID_ADDRESS,
    collect_rc013_readback,
    collect_swd_preflight,
    require_fresh_file,
    sha256_file,
)
from scripts.phase4_restore_v1 import (
    RESTORE_TOKEN,
    V1_BYTES,
    V1_SHA256,
    restore_verified_v1,
)


RC013_SHA256 = (
    "096B36A11764BCEB77BC391DC1758736143EFAED43942366409E946D1F90263A"
)


def test_physical_matrix_runner_is_path_independent_and_fail_fast() -> None:
    script = (
        Path(__file__).resolve().parents[1] / "terp_physical_matrix.ps1"
    ).resolve()
    source = script.read_text(encoding="utf-8")

    assert "$PSNativeCommandUseErrorActionPreference = $false" in source
    assert "Set-Location -LiteralPath $ProjectRoot" in source
    assert "if ($row.status -eq 'FAIL')" in source
    assert (
        'throw "TERP physical matrix failed at $tag; '
        'stop physical reconnect actions."'
    ) in source

    parser = subprocess.run(
        [
            "pwsh",
            "-NoProfile",
            "-Command",
            (
                "$errors = $null; "
                "$source = [Console]::In.ReadToEnd(); "
                "[System.Management.Automation.Language.Parser]::ParseInput("
                "$source, [ref]$null, [ref]$errors) | Out-Null; "
                "if ($errors.Count -ne 0) { $errors | Out-String | Write-Error; exit 1 }"
            ),
        ],
        input=source,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    assert parser.returncode == 0, parser.stderr


def _passing_rc013_readback(label: str) -> dict[str, object]:
    return {
        "pass": True,
        "label": label,
        "port": "COM13",
        "device_info": {
            "serial_number": "recorder-001",
            "capability_flags": 491,
        },
        "health": {
            "storage_ready": True,
            "storage_error_count": 0,
            "event_export_error_count": 0,
        },
        "event_readback": {
            "event_id": 100,
            "listed": True,
            "event_info": {
                "total_length": 38560,
                "event_crc32": 1570525185,
            },
            "download": {
                "bytes": 38560,
                "crc32": 1570525185,
                "sha256": RC013_SHA256,
            },
        },
    }


def _formal_cold_boot_round(round_number: int) -> dict[str, object]:
    return {
        "schema": "phase4-rc013-cold-boot-round-v1",
        "round": round_number,
        "status": "PASS",
        "port": "COM13",
        "physical_confirmation": True,
        "formal_pass_eligible": True,
        "started_at_utc": "2026-08-28T00:00:00Z",
        "off_confirmation": "OFF",
        "off_confirmed_at_utc": "2026-08-28T00:00:01Z",
        "ready_confirmation": "READY",
        "ready_confirmed_at_utc": "2026-08-28T00:00:06Z",
        "off_seconds": 5.0,
        "ended_at_utc": "2026-08-28T00:00:07Z",
        "readback": _passing_rc013_readback(
            f"round-{round_number:02d}-readback"
        ),
    }


def _formal_cold_boot_aggregate() -> dict[str, object]:
    return {
        "schema": "phase4-rc013-cold-boot-matrix-v1",
        "pass": True,
        "status": "PASS",
        "port": "COM13",
        "physical_confirmation": True,
        "formal_pass_eligible": True,
        "rounds": 10,
        "completed_rounds": list(range(1, 11)),
        "preflight": _passing_rc013_readback("preflight-01"),
    }


def _write_formal_rounds(output_dir: Path) -> None:
    for round_number in range(1, 11):
        (output_dir / f"round-{round_number:02d}.json").write_text(
            json.dumps(_formal_cold_boot_round(round_number)),
            encoding="utf-8",
        )


class _FakeClock:
    def __init__(self) -> None:
        self.value = 10.0

    def __call__(self) -> float:
        return self.value

    def sleep(self, seconds: float) -> None:
        self.value += seconds


def test_rc013_readback_uses_bounded_startup_retry(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    from transport_recorder.protocol.client import TerpClient
    from transport_recorder.protocol.serial_transport import SerialTransport

    class FakeSerial:
        def write(self, data: bytes) -> None:
            pass

        def read(self, maximum_bytes: int) -> bytes:
            return b""

        def close(self) -> None:
            pass

    calls: list[dict[str, object]] = []

    def fake_connect_with_retry(
        cls: type[TerpClient],
        transport_factory: object,
        **kwargs: object,
    ) -> tuple[TerpClient, object]:
        calls.append(kwargs)
        raise RuntimeError("stop after startup retry seam")

    monkeypatch.setattr(
        SerialTransport,
        "open",
        classmethod(lambda cls, *args, **kwargs: FakeSerial()),
    )
    monkeypatch.setattr(
        TerpClient,
        "connect_with_retry",
        classmethod(fake_connect_with_retry),
    )
    monkeypatch.setattr(
        TerpClient,
        "hello",
        lambda self: (_ for _ in ()).throw(
            AssertionError("one-shot HELLO must not be used")
        ),
    )

    result = collect_rc013_readback("COM13", tmp_path, "readiness")

    assert len(calls) == 1
    assert calls[0]["startup_timeout_seconds"] == 15.0
    assert calls[0]["retry_interval_seconds"] == 0.5
    assert calls[0]["timeout_seconds"] == 2.0
    assert callable(calls[0]["trace"])
    assert result["error"] == "RuntimeError: stop after startup retry seam"


def test_evidence_paths_are_fresh_and_hashes_are_stable(tmp_path: Path) -> None:
    output_dir = tmp_path / "session" / "swd"
    result_path = output_dir / "swd-preflight.json"

    require_fresh_file(result_path)
    output_dir.mkdir(parents=True)
    result_path.write_bytes(b"first result")

    assert sha256_file(result_path) == hashlib.sha256(
        b"first result"
    ).hexdigest()
    with pytest.raises(FileExistsError):
        require_fresh_file(result_path)


def test_swd_preflight_collects_explicit_identity_without_mutation(
    tmp_path: Path,
) -> None:
    calls: list[list[str]] = []

    def fake_runner(
        command: list[str], stdout_path: Path, stderr_path: Path
    ) -> int:
        calls.append(command)
        stdout_path.write_text(
            {
                "-l": (
                    "ST-LINK SN : DEVICE_SERIAL_REDACTED__\n"
                    "ST-LINK FW : V2J46S7\n"
                ),
                "-r32-uid": (
                    "Reading 3 words from address 0x1FF1E800\n"
                    "Voltage     : 3.30V\n"
                    "Device ID   : 0x450\n"
                    "Device name : STM32H7xx\n"
                    "0x1FF1E800 : 003E0026 3433510C 34393738\n"
                ),
                "-r32-app": (
                    "Voltage     : 3.30V\n"
                    "Device ID   : 0x450\n"
                    "Device name : STM32H7xx\n"
                    "Reading 4 words from address 0x08020000\n"
                    "0x08020000 : 24001600 08024819 0802486D 0802041D\n"
                ),
            }[
                "-l"
                if "-l" in command
                else "-r32-uid"
                if f"0x{UID_ADDRESS:08X}" in command
                else "-r32-app"
            ],
            encoding="utf-8",
        )
        stderr_path.write_text("", encoding="utf-8")
        return 0

    result = collect_swd_preflight(
        programmer=tmp_path / "STM32_Programmer_CLI.exe",
        probe_serial=EXPECTED_STLINK_SERIAL,
        output_dir=tmp_path / "evidence",
        runner=fake_runner,
    )

    assert result["pass"] is True
    assert result["probe_serial"] == EXPECTED_STLINK_SERIAL
    assert result["uid_words"] == [
        0x003E0026,
        0x3433510C,
        0x34393738,
    ]
    assert result["application_words"] == [
        0x24001600,
        0x08024819,
        0x0802486D,
        0x0802041D,
    ]
    assert json.loads(
        (tmp_path / "evidence" / "swd-preflight.json").read_text(
            encoding="utf-8"
        )
    )["pass"] is True

    assert len(calls) == 3
    assert any("-l" in command and "stlink" in command for command in calls)
    read_commands = [command for command in calls if "-r32" in command]
    assert len(read_commands) == 2
    uid_command = next(
        command
        for command in read_commands
        if f"0x{UID_ADDRESS:08X}" in command
    )
    application_command = next(
        command
        for command in read_commands
        if f"0x{APPLICATION_ADDRESS:08X}" in command
    )
    assert uid_command[-1] == "12"
    assert application_command[-1] == "16"
    assert all(
        any(EXPECTED_STLINK_SERIAL in token for token in command)
        for command in read_commands
    )
    assert all("freq=400" in command for command in read_commands)
    assert all("mode=HOTPLUG" in command for command in read_commands)
    forbidden_connect_modes = {"mode=NORMAL", "mode=UR", "mode=HWRSTPULSE"}
    assert not any(
        token.upper() in forbidden_connect_modes
        for command in read_commands
        for token in command
    )
    assert not any(
        token.lower().startswith("reset=")
        for command in read_commands
        for token in command
    )
    assert not any(
        EXPECTED_STLINK_SERIAL in token
        for command in calls
        if "-l" in command
        for token in command
    )
    forbidden = {"-w", "-v", "-rst", "erase", "unlock"}
    assert not any(
        token.lower() in forbidden
        for command in calls
        for token in command
    )


def _swd_output_runner(
    outputs: dict[str, str], calls: list[list[str]]
):
    def fake_runner(
        command: list[str], stdout_path: Path, stderr_path: Path
    ) -> int:
        calls.append(command)
        if "-l" in command:
            output = outputs["identity"]
        elif f"0x{UID_ADDRESS:08X}" in command:
            output = outputs["uid"]
        else:
            output = outputs["application"]
        stdout_path.write_text(output, encoding="utf-8")
        stderr_path.write_text("", encoding="utf-8")
        return 0

    return fake_runner


def test_swd_preflight_rejects_real_hotplug_uid_output(
    tmp_path: Path,
) -> None:
    calls: list[list[str]] = []
    outputs = {
        "identity": (
            "ST-LINK SN : DEVICE_SERIAL_REDACTED__\n"
            "ST-LINK FW : V2J46S7\n"
        ),
        "uid": (
            "Voltage : 3.30V\n"
            "Device ID : 0x450\n"
            "Device name : STM32H7xx\n"
            "Reading 3 words from address 0x1FF1E800\n"
            "0x1FF1E800 : 00000800 00000800 00000800\n"
        ),
        "application": (
            "Voltage : 3.30V\n"
            "Device ID : 0x800\n"
            "Device name : STM32H7xx\n"
            "Error: database lookup failed\n"
            "Error: flash loader unavailable\n"
            "Error: failed to read memory\n"
            "Reading 4 words from address 0x08020000\n"
            "0x08020000 : 411FC271 411FC271 411FC271 411FC271\n"
        ),
    }

    result = collect_swd_preflight(
        programmer=tmp_path / "STM32_Programmer_CLI.exe",
        probe_serial=EXPECTED_STLINK_SERIAL,
        output_dir=tmp_path / "evidence",
        runner=_swd_output_runner(outputs, calls),
    )

    assert result["pass"] is False
    assert "uid" in str(result["error"]).lower()
    assert len(calls) == 2
    saved = json.loads(
        (tmp_path / "evidence" / "swd-preflight.json").read_text(
            encoding="utf-8"
        )
    )
    assert saved["pass"] is False
    assert saved["error"] == result["error"]


def test_swd_preflight_rejects_real_hotplug_application_output(
    tmp_path: Path,
) -> None:
    calls: list[list[str]] = []
    outputs = {
        "identity": (
            "ST-LINK SN : DEVICE_SERIAL_REDACTED__\n"
            "ST-LINK FW : V2J46S7\n"
        ),
        "uid": (
            "Voltage : 3.30V\n"
            "Device ID : 0x450\n"
            "Device name : STM32H7xx\n"
            "0x1FF1E800 : 003E0026 3433510C 34393738\n"
        ),
        "application": (
            "Voltage : 3.30V\n"
            "Device ID : 0x800\n"
            "Device name : STM32H7xx\n"
            "Error: database lookup failed\n"
            "Error: flash loader unavailable\n"
            "Error: failed to read memory\n"
            "0x08020000 : 411FC271 411FC271 411FC271 411FC271\n"
        ),
    }

    result = collect_swd_preflight(
        programmer=tmp_path / "STM32_Programmer_CLI.exe",
        probe_serial=EXPECTED_STLINK_SERIAL,
        output_dir=tmp_path / "evidence",
        runner=_swd_output_runner(outputs, calls),
    )

    assert result["pass"] is False
    assert "application" in str(result["error"]).lower()
    assert len(calls) == 3
    saved = json.loads(
        (tmp_path / "evidence" / "swd-preflight.json").read_text(
            encoding="utf-8"
        )
    )
    assert saved["pass"] is False
    assert saved["error"] == result["error"]


def test_cold_boot_preflight_is_validated_before_first_prompt(
    tmp_path: Path,
) -> None:
    prompts: list[str] = []
    bad = _passing_rc013_readback("preflight")
    bad["device_info"] = {
        "serial_number": "recorder-001",
        "capability_flags": 235,
    }

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path,
        rounds=10,
        input_fn=lambda prompt: prompts.append(prompt) or "OFF",
        readback_fn=lambda port, output_dir, label="readback": bad,
    )

    assert result["pass"] is False
    assert result["status"] == "PREFLIGHT_FAIL"
    assert "capability" in str(result["error"]).lower()
    assert prompts == []
    assert not (tmp_path / "round-01.json").exists()


def test_cold_boot_rejects_readback_from_any_other_port_before_prompt(
    tmp_path: Path,
) -> None:
    prompts: list[str] = []
    wrong_port = _passing_rc013_readback("preflight")
    wrong_port["port"] = "COM12"

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path,
        rounds=10,
        input_fn=lambda prompt: prompts.append(prompt) or "OFF",
        readback_fn=lambda port, output_dir, label: wrong_port,
    )

    assert result["pass"] is False
    assert result["status"] == "PREFLIGHT_FAIL"
    assert "port" in str(result["error"]).lower()
    assert prompts == []


def test_cold_boot_rejects_readback_that_did_not_report_pass(
    tmp_path: Path,
) -> None:
    prompts: list[str] = []
    failed = _passing_rc013_readback("preflight")
    failed["pass"] = False

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path,
        rounds=10,
        input_fn=lambda prompt: prompts.append(prompt) or "OFF",
        readback_fn=lambda port, output_dir, label: failed,
    )

    assert result["status"] == "PREFLIGHT_FAIL"
    assert "pass" in str(result["error"]).lower()
    assert prompts == []


@pytest.mark.parametrize("rounds", [1, 9, 11, 20])
def test_cold_boot_runner_rejects_any_round_count_other_than_ten(
    tmp_path: Path,
    rounds: int,
) -> None:
    calls: list[str] = []

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path / str(rounds),
        rounds=rounds,
        input_fn=lambda _: pytest.fail("invalid round count must not prompt"),
        readback_fn=lambda port, output_dir, label: calls.append(label),
    )

    assert result["pass"] is False
    assert result["status"] == "INVALID_ARGUMENT"
    assert calls == []


def test_cold_boot_cli_returns_nonzero_for_non_ten_round_count(
    tmp_path: Path,
) -> None:
    with pytest.raises(SystemExit) as exit_info:
        cold_boot_main(
            [
                "--port",
                "COM13",
                "--output-dir",
                str(tmp_path),
                "--rounds",
                "9",
            ]
        )
    assert exit_info.value.code != 0
    assert not (tmp_path / "results.json").exists()


def test_cold_boot_requires_off_then_ready_and_enforces_five_seconds(
    tmp_path: Path,
) -> None:
    for round_number in range(2, 11):
        (tmp_path / f"round-{round_number:02d}.json").write_text(
            json.dumps(_formal_cold_boot_round(round_number)),
            encoding="utf-8",
        )
    prompts: list[str] = []
    answers = iter(["OFF", "READY"])
    labels: list[str] = []
    clock = _FakeClock()

    def readback(port: str, output_dir: Path, label: str) -> dict[str, object]:
        assert port == "COM13"
        assert output_dir.is_absolute()
        labels.append(label)
        return _passing_rc013_readback(label)

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path,
        rounds=10,
        input_fn=lambda prompt: prompts.append(prompt) or next(answers),
        clock=clock,
        sleep_fn=clock.sleep,
        readback_fn=readback,
    )

    row = json.loads((tmp_path / "round-01.json").read_text("utf-8"))
    assert result["pass"] is True
    assert result["status"] == "PASS"
    assert labels == ["preflight-01", "round-01-readback"]
    assert "OFF" in prompts[0]
    assert "READY" in prompts[1]
    assert row["off_confirmation"] == "OFF"
    assert row["ready_confirmation"] == "READY"
    assert row["off_seconds"] >= 5.0
    assert row["physical_confirmation"] is True
    assert row["formal_pass_eligible"] is True
    assert row["status"] == "PASS"


def test_cold_boot_resume_skips_only_existing_formal_pass(
    tmp_path: Path,
) -> None:
    _write_formal_rounds(tmp_path)
    labels: list[str] = []

    def readback(port: str, output_dir: Path, label: str) -> dict[str, object]:
        labels.append(label)
        return _passing_rc013_readback(label)

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path,
        rounds=10,
        input_fn=lambda _: pytest.fail("completed rounds must be skipped"),
        readback_fn=readback,
    )

    assert result["pass"] is True
    assert labels == ["preflight-01"]
    assert json.loads((tmp_path / "round-01.json").read_text("utf-8")) == (
        _formal_cold_boot_round(1)
    )


@pytest.mark.parametrize("status", ["FAIL", "DIAGNOSTIC_NOT_PHYSICAL", None])
def test_cold_boot_existing_non_pass_round_is_an_immutable_blocker(
    tmp_path: Path,
    status: str | None,
) -> None:
    original = {"round": 1, "status": status}
    round_path = tmp_path / "round-01.json"
    round_path.write_text(json.dumps(original), encoding="utf-8")
    calls: list[str] = []

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path,
        rounds=10,
        input_fn=lambda _: pytest.fail("must not prompt"),
        readback_fn=lambda port, output_dir, label: calls.append(label),
    )

    assert result["pass"] is False
    assert result["status"] == "BLOCKED_EXISTING_ROUND"
    assert calls == []
    assert json.loads(round_path.read_text("utf-8")) == original


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("schema", "wrong-schema"),
        ("port", "COM12"),
        ("off_confirmation", "off"),
        ("ready_confirmation", "ready"),
        ("off_seconds", 4.999),
        ("off_seconds", math.nan),
        ("off_seconds", math.inf),
        ("started_at_utc", ""),
        ("off_confirmed_at_utc", None),
        ("ready_confirmed_at_utc", ""),
        ("ended_at_utc", None),
        ("readback", None),
    ],
)
def test_cold_boot_rejects_tampered_existing_pass_round(
    tmp_path: Path,
    field: str,
    value: object,
) -> None:
    row = _formal_cold_boot_round(1)
    row[field] = value
    round_path = tmp_path / "round-01.json"
    original_text = json.dumps(row)
    round_path.write_text(original_text, encoding="utf-8")
    calls: list[str] = []

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path,
        rounds=10,
        input_fn=lambda _: pytest.fail("tampered PASS must not prompt"),
        readback_fn=lambda port, output_dir, label: calls.append(label),
    )

    assert result["status"] == "BLOCKED_EXISTING_ROUND"
    assert calls == []
    assert round_path.read_text("utf-8") == original_text


@pytest.mark.parametrize(
    "aggregate",
    [
        {"schema": "wrong", "pass": True, "formal_pass_eligible": True,
         "rounds": 10, "completed_rounds": list(range(1, 11))},
        {"schema": "phase4-rc013-cold-boot-matrix-v1", "pass": True,
         "formal_pass_eligible": True, "rounds": 9,
         "completed_rounds": list(range(1, 11))},
        {"schema": "phase4-rc013-cold-boot-matrix-v1", "pass": True,
         "formal_pass_eligible": True, "rounds": 10,
         "completed_rounds": list(range(1, 10))},
    ],
)
def test_cold_boot_rejects_invalid_existing_aggregate(
    tmp_path: Path,
    aggregate: dict[str, object],
) -> None:
    _write_formal_rounds(tmp_path)
    results_path = tmp_path / "results.json"
    results_path.write_text(json.dumps(aggregate), encoding="utf-8")
    calls: list[str] = []

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path,
        rounds=10,
        input_fn=lambda _: pytest.fail("invalid aggregate must block"),
        readback_fn=lambda port, output_dir, label: calls.append(label),
    )

    assert result["pass"] is False
    assert result["status"] == "BLOCKED_EXISTING_AGGREGATE"
    assert calls == []
    assert json.loads(results_path.read_text("utf-8")) == aggregate


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("status", "FAIL"),
        ("port", "COM12"),
        ("physical_confirmation", False),
        ("preflight", None),
        ("preflight", {"pass": True, "port": "COM12"}),
    ],
)
def test_cold_boot_rejects_unbound_existing_aggregate(
    tmp_path: Path,
    field: str,
    value: object,
) -> None:
    _write_formal_rounds(tmp_path)
    aggregate = _formal_cold_boot_aggregate()
    aggregate[field] = value
    results_path = tmp_path / "results.json"
    original_text = json.dumps(aggregate)
    results_path.write_text(original_text, encoding="utf-8")
    calls: list[str] = []

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path,
        rounds=10,
        input_fn=lambda _: pytest.fail("invalid aggregate must block"),
        readback_fn=lambda port, output_dir, label: calls.append(label),
    )

    assert result["status"] == "BLOCKED_EXISTING_AGGREGATE"
    assert calls == []
    assert results_path.read_text("utf-8") == original_text


def test_cold_boot_first_readback_failure_is_preserved_and_stops(
    tmp_path: Path,
) -> None:
    answers = iter(["OFF", "READY", "OFF", "READY"])
    labels: list[str] = []
    clock = _FakeClock()

    def readback(port: str, output_dir: Path, label: str) -> dict[str, object]:
        labels.append(label)
        result = _passing_rc013_readback(label)
        if label == "round-01-readback":
            result["event_readback"] = {"event_id": 100, "listed": False}
        return result

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path,
        rounds=10,
        input_fn=lambda _: next(answers),
        clock=clock,
        sleep_fn=clock.sleep,
        readback_fn=readback,
    )

    row = json.loads((tmp_path / "round-01.json").read_text("utf-8"))
    assert result["pass"] is False
    assert result["status"] == "FAIL"
    assert row["status"] == "FAIL"
    assert labels == ["preflight-01", "round-01-readback"]
    assert not (tmp_path / "round-02.json").exists()
    assert not (tmp_path / "results.json").exists()


def test_cold_boot_diagnostic_never_returns_formal_pass(
    tmp_path: Path,
) -> None:
    labels: list[str] = []

    result = run_cold_boot_matrix(
        port="COM13",
        output_dir=tmp_path,
        diagnostic=True,
        input_fn=lambda _: pytest.fail("diagnostic must not prompt"),
        readback_fn=lambda port, output_dir, label: (
            labels.append(label) or _passing_rc013_readback(label)
        ),
    )

    assert result["pass"] is False
    assert result["status"] == "DIAGNOSTIC_NOT_PHYSICAL"
    assert result["physical_confirmation"] is False
    assert result["formal_pass_eligible"] is False
    assert labels == ["diagnostic-01"]
    assert not (tmp_path / "round-01.json").exists()
    assert not (tmp_path / "results.json").exists()


def _passing_v1_acceptance() -> dict[str, object]:
    return {
        "schema": "hardware-acceptance-readback-v1",
        "pass": True,
        "port": "COM13",
        "device_info": {
            "serial_number": "recorder-001",
            "capability_flags": 235,
        },
        "health": {
            "storage_ready": True,
            "storage_error_count": 0,
            "event_export_error_count": 0,
        },
        "event_count": 1,
        "event_ids": [100],
    }


def _v1_runner(
    calls: list[list[str]], image_bytes: bytes, acceptance: dict[str, object]
):
    def fake_runner(
        command: list[str], stdout_path: Path, stderr_path: Path
    ) -> int:
        calls.append(command)
        stdout_path.parent.mkdir(parents=True, exist_ok=True)
        stderr_path.parent.mkdir(parents=True, exist_ok=True)
        output = ""
        if "-l" in command:
            output = (
                "ST-LINK SN : DEVICE_SERIAL_REDACTED__\n"
                "ST-LINK FW : V2J46S7\n"
            )
        elif "-r32" in command and f"0x{UID_ADDRESS:08X}" in command:
            output = (
                "Voltage : 3.30V\nDevice ID : 0x450\n"
                "Device name : STM32H743\n"
                "0x1FF1E800 : 003E0026 3433510C 34393738\n"
            )
        elif "-r32" in command:
            output = (
                "Voltage : 3.30V\n"
                "Device ID : 0x450\n"
                "Device name : STM32H7xx\n"
                "0x08020000 : 24001600 08024819 0802486D 0802041D\n"
            )
        elif "-u" in command:
            readback_path = Path(command[command.index("-u") + 3])
            readback_path.write_bytes(image_bytes)
            output = "Upload verified\n"
        elif any(token.endswith("hardware_acceptance_readback.py")
                 for token in command):
            acceptance_dir = Path(
                command[command.index("--output-dir") + 1]
            )
            acceptance_dir.mkdir(parents=True, exist_ok=True)
            (acceptance_dir / "runtime-acceptance.json").write_text(
                json.dumps(acceptance), encoding="utf-8"
            )
            output = json.dumps(acceptance)
        else:
            output = "Programming and verification succeeded\n"
        stdout_path.write_text(output, encoding="utf-8")
        stderr_path.write_text("", encoding="utf-8")
        return 0

    return fake_runner


def test_restore_boundary_orders_probe_rc013_then_normal_swd(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    image_bytes = b"\xA5" * V1_BYTES
    image = (tmp_path / "restore-v1-readback-exact.bin").resolve()
    image.write_bytes(image_bytes)
    monkeypatch.setattr(
        restore_module,
        "V1_SHA256",
        hashlib.sha256(image_bytes).hexdigest(),
    )
    calls: list[list[str]] = []
    order: list[tuple[str, object]] = []
    programmer = (tmp_path / "STM32_Programmer_CLI.exe").resolve()
    output_dir = (tmp_path / "restore-evidence").resolve()
    runner = _v1_runner(calls, image_bytes, _passing_v1_acceptance())

    def ordered_runner(
        command: list[str], stdout_path: Path, stderr_path: Path
    ) -> int:
        order.append(("runner", list(command)))
        return runner(command, stdout_path, stderr_path)

    def rc013_readback(
        port: str, rc013_output_dir: Path, label: str
    ) -> dict[str, object]:
        order.append(("rc013", (port, rc013_output_dir, label)))
        return _passing_rc013_readback(label)

    try:
        result = restore_verified_v1(
            programmer=programmer,
            probe_serial=EXPECTED_STLINK_SERIAL,
            port="COM13",
            v1_image=image,
            output_dir=output_dir,
            confirmation=RESTORE_TOKEN,
            runner=ordered_runner,
            rc013_readback_fn=rc013_readback,
        )
    except TypeError as error:
        pytest.fail(f"restore must expose rc013_readback_fn seam: {error}")

    assert result["pass"] is True
    assert [entry[0] for entry in order] == [
        "runner",
        "rc013",
        "runner",
        "runner",
        "runner",
        "runner",
    ]
    assert calls[0] == [str(programmer), "-l", "stlink"]
    assert order[1][1] == (
        "COM13",
        output_dir / "rc013-readback",
        "restore-preflight",
    )
    assert not any(
        any(token in {"-r32", "-u", "-w", "-rst"}
            or token.lower().startswith("reset=")
            for token in command)
        for command in calls[:1]
    )

    assert calls[1] == [
        str(programmer),
        "-c",
        "port=SWD",
        f"sn={EXPECTED_STLINK_SERIAL}",
        "freq=400",
        "mode=NORMAL",
        "reset=SWrst",
        "-w",
        str(image),
        "0x08020000",
        "-v",
        "-rst",
    ]
    assert calls[2] == [
        str(programmer),
        "-c",
        "port=SWD",
        f"sn={EXPECTED_STLINK_SERIAL}",
        "freq=400",
        "mode=NORMAL",
        "reset=SWrst",
        "-u",
        "0x08020000",
        str(V1_BYTES),
        str(output_dir / "v1-readback.bin"),
        "-rst",
    ]
    assert calls[3][1:] == [
        "-m",
        "transport_recorder.cli",
        "info",
        "--port",
        "COM13",
        "--startup-timeout",
        "15",
    ]
    assert sum("-w" in command for command in calls) == 1
    assert result["program_attempts"] == 1
    assert result["readback_sha256"] == restore_module.V1_SHA256


def test_restore_boundary_stops_on_rc013_failure_before_mutation(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    image_bytes = b"\x5A" * V1_BYTES
    image = (tmp_path / "v1.bin").resolve()
    image.write_bytes(image_bytes)
    monkeypatch.setattr(
        restore_module,
        "V1_SHA256",
        hashlib.sha256(image_bytes).hexdigest(),
    )
    calls: list[list[str]] = []
    readback_calls: list[tuple[str, Path, str]] = []

    def runner(
        command: list[str], stdout_path: Path, stderr_path: Path
    ) -> int:
        calls.append(command)
        stdout_path.parent.mkdir(parents=True, exist_ok=True)
        stderr_path.parent.mkdir(parents=True, exist_ok=True)
        stdout_path.write_text(
            "ST-LINK SN : DEVICE_SERIAL_REDACTED__\n"
            "ST-LINK FW : V2J46S7\n"
            if "-l" in command
            else "",
            encoding="utf-8",
        )
        stderr_path.write_text("", encoding="utf-8")
        return 0

    def failed_rc013_readback(
        port: str, rc013_output_dir: Path, label: str
    ) -> dict[str, object]:
        readback_calls.append((port, rc013_output_dir, label))
        failed = _passing_rc013_readback(label)
        failed["event_readback"]["download"]["sha256"] = "0" * 64  # type: ignore[index]
        return failed

    result = restore_verified_v1(
        programmer=(tmp_path / "programmer.exe").resolve(),
        probe_serial=EXPECTED_STLINK_SERIAL,
        port="COM13",
        v1_image=image,
        output_dir=(tmp_path / "evidence").resolve(),
        confirmation=RESTORE_TOKEN,
        runner=runner,
        rc013_readback_fn=failed_rc013_readback,
    )

    assert result["pass"] is False
    assert result["status"] == "RC013_READBACK_FAIL"
    assert result["program_attempts"] == 0
    assert len(readback_calls) == 1
    assert all(
        not any(
            token in {"-r32", "-u", "-w", "-rst"}
            or token.lower().startswith("reset=")
            for token in command
        )
        for command in calls
    )


def test_restore_boundary_stops_on_probe_enumeration_failure(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    image_bytes = b"\x42" * V1_BYTES
    image = (tmp_path / "v1.bin").resolve()
    image.write_bytes(image_bytes)
    monkeypatch.setattr(
        restore_module,
        "V1_SHA256",
        hashlib.sha256(image_bytes).hexdigest(),
    )
    calls: list[list[str]] = []
    readback_calls: list[str] = []

    def runner(
        command: list[str], stdout_path: Path, stderr_path: Path
    ) -> int:
        calls.append(command)
        stdout_path.parent.mkdir(parents=True, exist_ok=True)
        stderr_path.parent.mkdir(parents=True, exist_ok=True)
        stdout_path.write_text(
            "ST-LINK SN : wrong-probe\nST-LINK FW : V2J46S7\n",
            encoding="utf-8",
        )
        stderr_path.write_text("", encoding="utf-8")
        return 0

    result = restore_verified_v1(
        programmer=(tmp_path / "programmer.exe").resolve(),
        probe_serial=EXPECTED_STLINK_SERIAL,
        port="COM13",
        v1_image=image,
        output_dir=(tmp_path / "evidence").resolve(),
        confirmation=RESTORE_TOKEN,
        runner=runner,
        rc013_readback_fn=lambda port, output_dir, label: readback_calls.append(label),
    )

    assert result["pass"] is False
    assert result["status"] == "STLINK_PREFLIGHT_FAIL"
    assert result["program_attempts"] == 0
    assert readback_calls == []
    assert calls == [[str((tmp_path / "programmer.exe").resolve()), "-l", "stlink"]]


def test_restore_programs_verified_v1_once_then_reads_back_and_accepts(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    image_bytes = b"\xA5" * V1_BYTES
    image = tmp_path / "restore-v1-readback-exact.bin"
    image.write_bytes(image_bytes)
    monkeypatch.setattr(
        restore_module,
        "V1_SHA256",
        hashlib.sha256(image_bytes).hexdigest(),
    )
    calls: list[list[str]] = []
    programmer = (tmp_path / "STM32_Programmer_CLI.exe").resolve()
    output_dir = (tmp_path / "restore-evidence").resolve()

    result = restore_verified_v1(
        programmer=programmer,
        probe_serial=EXPECTED_STLINK_SERIAL,
        port="COM13",
        v1_image=image.resolve(),
        output_dir=output_dir,
        confirmation=RESTORE_TOKEN,
        runner=_v1_runner(calls, image_bytes, _passing_v1_acceptance()),
        rc013_readback_fn=lambda port, rc013_output_dir, label: (
            _passing_rc013_readback(label)
        ),
    )

    assert result["pass"] is True
    assert calls[0] == [str(programmer), "-l", "stlink"]
    assert result["stlink_probe_preflight"]["pass"] is True  # type: ignore[index]
    assert result["rc013_readback"]["pass"] is True  # type: ignore[index]
    write_commands = [command for command in calls if "-w" in command]
    assert write_commands == [[
        str(programmer),
        "-c",
        "port=SWD",
        f"sn={EXPECTED_STLINK_SERIAL}",
        "freq=400",
        "mode=NORMAL",
        "reset=SWrst",
        "-w",
        str(image.resolve()),
        "0x08020000",
        "-v",
        "-rst",
    ]]
    program_command = write_commands[0]
    assert sum(token == "-rst" for token in program_command) == 1
    assert "mode=NORMAL" in program_command
    assert "reset=SWrst" in program_command
    assert calls[2] == [
        str(programmer),
        "-c",
        "port=SWD",
        f"sn={EXPECTED_STLINK_SERIAL}",
        "freq=400",
        "mode=NORMAL",
        "reset=SWrst",
        "-u",
        "0x08020000",
        str(V1_BYTES),
        str(output_dir / "v1-readback.bin"),
        "-rst",
    ]
    assert calls[3][1:] == [
        "-m",
        "transport_recorder.cli",
        "info",
        "--port",
        "COM13",
        "--startup-timeout",
        "15",
    ]
    acceptance_command = calls[4]
    assert str(Path(acceptance_command[0])).endswith("python.exe")
    assert any(
        token.endswith("hardware_acceptance_readback.py")
        for token in acceptance_command
    )
    assert acceptance_command[acceptance_command.index("--port") + 1] == "COM13"
    assert result["readback_sha256"] == restore_module.V1_SHA256
    assert result["runtime_acceptance"]["pass"] is True


@pytest.mark.parametrize(
    "image_bytes",
    [b"too short", b"\x33" * V1_BYTES],
    ids=["wrong-length", "wrong-sha256"],
)
def test_restore_rejects_wrong_image_before_any_hardware_command(
    tmp_path: Path, image_bytes: bytes
) -> None:
    image = (tmp_path / "candidate.bin").resolve()
    image.write_bytes(image_bytes)
    calls: list[list[str]] = []

    result = restore_verified_v1(
        programmer=(tmp_path / "programmer.exe").resolve(),
        probe_serial=EXPECTED_STLINK_SERIAL,
        port="COM13",
        v1_image=image,
        output_dir=(tmp_path / "evidence").resolve(),
        confirmation=RESTORE_TOKEN,
        runner=lambda command, stdout, stderr: calls.append(command) or 0,
    )

    assert result["pass"] is False
    assert result["status"] == "VALIDATION_FAIL"
    assert calls == []
    assert "image" in str(result["error"])


def test_restore_requires_the_frozen_explicit_confirmation(
    tmp_path: Path,
) -> None:
    calls: list[list[str]] = []
    result = restore_verified_v1(
        programmer=(tmp_path / "programmer.exe").resolve(),
        probe_serial=EXPECTED_STLINK_SERIAL,
        port="COM13",
        v1_image=(tmp_path / "missing.bin").resolve(),
        output_dir=(tmp_path / "evidence").resolve(),
        confirmation="YES",
        runner=lambda command, stdout, stderr: calls.append(command) or 0,
    )

    assert RESTORE_TOKEN == "RESTORE_VERIFIED_V1"
    assert V1_BYTES == 219120
    assert V1_SHA256 == (
        "83584fd8b3bba44619d315d75e3140aebc706ba8b406f3e5e04cdfd1a17f9112"
    )
    assert result["status"] == "VALIDATION_FAIL"
    assert "confirmation" in str(result["error"])
    assert calls == []


def test_restore_stops_after_preflight_or_programmer_failure(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    image_bytes = b"\x42" * V1_BYTES
    image = (tmp_path / "v1.bin").resolve()
    image.write_bytes(image_bytes)
    monkeypatch.setattr(
        restore_module,
        "V1_SHA256",
        hashlib.sha256(image_bytes).hexdigest(),
    )

    preflight_calls: list[list[str]] = []
    preflight_base = _v1_runner(
        preflight_calls, image_bytes, _passing_v1_acceptance()
    )

    def failed_preflight(
        command: list[str], stdout_path: Path, stderr_path: Path
    ) -> int:
        exit_code = preflight_base(command, stdout_path, stderr_path)
        return 2 if "-l" in command else exit_code

    preflight_result = restore_verified_v1(
        programmer=(tmp_path / "programmer.exe").resolve(),
        probe_serial=EXPECTED_STLINK_SERIAL,
        port="COM13",
        v1_image=image,
        output_dir=(tmp_path / "preflight-failure").resolve(),
        confirmation=RESTORE_TOKEN,
        runner=failed_preflight,
    )
    assert preflight_result["status"] == "STLINK_PREFLIGHT_FAIL"
    assert len(preflight_calls) == 1
    assert not any("-w" in command for command in preflight_calls)

    program_calls: list[list[str]] = []
    program_base = _v1_runner(
        program_calls, image_bytes, _passing_v1_acceptance()
    )

    def failed_program(
        command: list[str], stdout_path: Path, stderr_path: Path
    ) -> int:
        exit_code = program_base(command, stdout_path, stderr_path)
        return 7 if "-w" in command else exit_code

    program_result = restore_verified_v1(
        programmer=(tmp_path / "programmer.exe").resolve(),
        probe_serial=EXPECTED_STLINK_SERIAL,
        port="COM13",
        v1_image=image,
        output_dir=(tmp_path / "program-failure").resolve(),
        confirmation=RESTORE_TOKEN,
        runner=failed_program,
        rc013_readback_fn=lambda port, rc013_output_dir, label: (
            _passing_rc013_readback(label)
        ),
    )
    assert program_result["status"] == "PROGRAM_FAIL"
    assert program_result["program_attempts"] == 1
    assert sum("-w" in command for command in program_calls) == 1
    assert not any("-u" in command for command in program_calls)


def test_restore_readback_or_acceptance_mismatch_stops_with_evidence(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    image_bytes = b"\x5A" * V1_BYTES
    image = (tmp_path / "v1.bin").resolve()
    image.write_bytes(image_bytes)
    monkeypatch.setattr(
        restore_module,
        "V1_SHA256",
        hashlib.sha256(image_bytes).hexdigest(),
    )

    mismatch_calls: list[list[str]] = []
    mismatch_result = restore_verified_v1(
        programmer=(tmp_path / "programmer.exe").resolve(),
        probe_serial=EXPECTED_STLINK_SERIAL,
        port="COM13",
        v1_image=image,
        output_dir=(tmp_path / "readback-mismatch").resolve(),
        confirmation=RESTORE_TOKEN,
        runner=_v1_runner(
            mismatch_calls,
            b"\xA5" * V1_BYTES,
            _passing_v1_acceptance(),
        ),
        rc013_readback_fn=lambda port, rc013_output_dir, label: (
            _passing_rc013_readback(label)
        ),
    )
    assert mismatch_result["status"] == "READBACK_FAIL"
    assert sum("-w" in command for command in mismatch_calls) == 1
    assert not any(
        token.endswith("hardware_acceptance_readback.py")
        for command in mismatch_calls
        for token in command
    )

    bad_acceptance = _passing_v1_acceptance()
    bad_acceptance["device_info"] = {
        "serial_number": "recorder-001",
        "capability_flags": 491,
    }
    acceptance_calls: list[list[str]] = []
    acceptance_result = restore_verified_v1(
        programmer=(tmp_path / "programmer.exe").resolve(),
        probe_serial=EXPECTED_STLINK_SERIAL,
        port="COM13",
        v1_image=image,
        output_dir=(tmp_path / "acceptance-mismatch").resolve(),
        confirmation=RESTORE_TOKEN,
        runner=_v1_runner(acceptance_calls, image_bytes, bad_acceptance),
        rc013_readback_fn=lambda port, rc013_output_dir, label: (
            _passing_rc013_readback(label)
        ),
    )
    assert acceptance_result["status"] == "RUNTIME_ACCEPTANCE_FAIL"
    assert acceptance_result["runtime_acceptance"] == bad_acceptance
    assert sum("-w" in command for command in acceptance_calls) == 1


def test_restore_never_overwrites_existing_evidence(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    image_bytes = b"\x7E" * V1_BYTES
    image = (tmp_path / "v1.bin").resolve()
    image.write_bytes(image_bytes)
    monkeypatch.setattr(
        restore_module,
        "V1_SHA256",
        hashlib.sha256(image_bytes).hexdigest(),
    )
    output_dir = (tmp_path / "evidence").resolve()
    output_dir.mkdir()
    existing = output_dir / "program.stdout.log"
    existing.write_text("first evidence", encoding="utf-8")
    calls: list[list[str]] = []

    result = restore_verified_v1(
        programmer=(tmp_path / "programmer.exe").resolve(),
        probe_serial=EXPECTED_STLINK_SERIAL,
        port="COM13",
        v1_image=image,
        output_dir=output_dir,
        confirmation=RESTORE_TOKEN,
        runner=lambda command, stdout, stderr: calls.append(command) or 0,
    )

    assert result["status"] == "BLOCKED_EXISTING_EVIDENCE"
    assert calls == []
    assert existing.read_text(encoding="utf-8") == "first evidence"
