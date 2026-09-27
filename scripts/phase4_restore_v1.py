#!/usr/bin/env python3
"""Restore the exact released-v1 image and collect acceptance evidence."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
from typing import Callable, Sequence, TypeAlias

try:
    from scripts.phase4_manual_common import (
        APPLICATION_ADDRESS,
        CommandRunner,
        EXPECTED_STLINK_SERIAL,
        _rc013_readback_failure,
        collect_rc013_readback,
        collect_stlink_probe_preflight,
        require_fresh_file,
        run_logged,
        sha256_file,
    )
except ModuleNotFoundError:  # direct absolute-path invocation from any cwd
    from phase4_manual_common import (  # type: ignore[no-redef]
        APPLICATION_ADDRESS,
        CommandRunner,
        EXPECTED_STLINK_SERIAL,
        _rc013_readback_failure,
        collect_rc013_readback,
        collect_stlink_probe_preflight,
        require_fresh_file,
        run_logged,
        sha256_file,
    )


V1_BYTES = 219120
V1_SHA256 = (
    "83584fd8b3bba44619d315d75e3140aebc706ba8b406f3e5e04cdfd1a17f9112"
)
RESTORE_TOKEN = "RESTORE_VERIFIED_V1"
V1_CAPABILITY_FLAGS = 235
V1_SERIAL_NUMBER = "recorder-001"

ReadbackFn: TypeAlias = Callable[[str, Path, str], dict[str, object]]


def _timestamp() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _write_json_once(path: Path, payload: dict[str, object]) -> None:
    require_fresh_file(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(payload, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")


def _command_record(
    name: str,
    command: list[str],
    stdout_path: Path,
    stderr_path: Path,
    exit_code: int | None,
    error: str | None = None,
) -> dict[str, object]:
    record: dict[str, object] = {
        "name": name,
        "command": list(command),
        "stdout_path": str(stdout_path),
        "stderr_path": str(stderr_path),
        "exit_code": exit_code,
    }
    if stdout_path.is_file():
        record["stdout_sha256"] = sha256_file(stdout_path)
    if stderr_path.is_file():
        record["stderr_sha256"] = sha256_file(stderr_path)
    if error is not None:
        record["error"] = error
    return record


def _acceptance_failure(result: object, port: str) -> str | None:
    if not isinstance(result, dict):
        return "runtime acceptance result is not a JSON object"
    if result.get("pass") is not True:
        return "runtime acceptance did not report pass=true"
    if str(result.get("port", "")).upper() != port.upper():
        return "runtime acceptance port does not match explicit COM13"

    device_info = result.get("device_info")
    if not isinstance(device_info, dict):
        return "runtime device identity is missing"
    if device_info.get("serial_number") != V1_SERIAL_NUMBER:
        return "runtime device serial is not recorder-001"
    if device_info.get("capability_flags") != V1_CAPABILITY_FLAGS:
        return "runtime capability flags are not released-v1 value 235"

    health = result.get("health")
    if not isinstance(health, dict):
        return "runtime health snapshot is missing"
    if health.get("storage_ready") is not True:
        return "runtime storage is not ready"
    if health.get("storage_error_count") != 0:
        return "runtime storage error count is nonzero"
    if health.get("event_export_error_count") != 0:
        return "runtime event export error count is nonzero"

    events = result.get("event_ids")
    if not isinstance(events, list) or not events:
        return "runtime event list is empty"
    event_count = result.get("event_count")
    if not isinstance(event_count, int) or event_count <= 0:
        return "runtime event count is empty"
    return None


def _require_absolute(path: Path, label: str) -> Path:
    candidate = Path(path)
    if not candidate.is_absolute():
        raise ValueError(f"{label} must be an absolute path")
    return candidate.resolve()


def restore_verified_v1(
    programmer: Path,
    probe_serial: str,
    port: str,
    v1_image: Path,
    output_dir: Path,
    confirmation: str,
    runner: CommandRunner = run_logged,
    rc013_readback_fn: ReadbackFn = collect_rc013_readback,
) -> dict[str, object]:
    """Program exactly one verified-v1 image and fail at the first mismatch."""

    programmer = _require_absolute(programmer, "programmer")
    v1_image = _require_absolute(v1_image, "v1 image")
    output_dir = _require_absolute(output_dir, "output directory")
    output_dir.mkdir(parents=True, exist_ok=True)

    result_path = output_dir / "restore-v1.json"
    require_fresh_file(result_path)
    result: dict[str, object] = {
        "schema": "phase4-verified-v1-restore-v1",
        "pass": False,
        "status": "NOT_STARTED",
        "started_at_utc": _timestamp(),
        "programmer": str(programmer),
        "probe_serial": probe_serial,
        "port": port,
        "v1_image": str(v1_image),
        "output_dir": str(output_dir),
        "program_attempts": 0,
        "commands": [],
    }

    first_failure: str | None = None
    if confirmation != RESTORE_TOKEN:
        first_failure = f"confirmation must be exactly {RESTORE_TOKEN}"
    elif probe_serial != EXPECTED_STLINK_SERIAL:
        first_failure = "probe serial does not match the frozen ST-Link identity"
    elif port.upper() != "COM13":
        first_failure = "runtime acceptance port must be explicit COM13"
    elif not v1_image.is_file():
        first_failure = "verified-v1 image does not exist"
    else:
        image_bytes = v1_image.stat().st_size
        image_sha256 = sha256_file(v1_image)
        result["v1_bytes"] = image_bytes
        result["v1_sha256"] = image_sha256
        if image_bytes != V1_BYTES:
            first_failure = f"verified-v1 image length is not {V1_BYTES} bytes"
        elif image_sha256.lower() != V1_SHA256.lower():
            first_failure = "verified-v1 image SHA-256 does not match"

    stlink_dir = output_dir / "stlink-probe-preflight"
    rc013_dir = output_dir / "rc013-readback"
    rc013_label = "restore-preflight"
    program_stdout = output_dir / "program.stdout.log"
    program_stderr = output_dir / "program.stderr.log"
    readback_path = output_dir / "v1-readback.bin"
    readback_stdout = output_dir / "readback.stdout.log"
    readback_stderr = output_dir / "readback.stderr.log"
    readiness_stdout = output_dir / "readiness.stdout.log"
    readiness_stderr = output_dir / "readiness.stderr.log"
    acceptance_dir = output_dir / "runtime-acceptance"
    acceptance_stdout = output_dir / "acceptance.stdout.log"
    acceptance_stderr = output_dir / "acceptance.stderr.log"
    acceptance_result_path = acceptance_dir / "runtime-acceptance.json"

    if first_failure is None:
        evidence_paths = (
            stlink_dir / "stlink-probe-preflight.json",
            stlink_dir / "identity.stdout.log",
            stlink_dir / "identity.stderr.log",
            rc013_dir / f"{rc013_label}.raw.jsonl",
            rc013_dir / f"{rc013_label}.frames.jsonl",
            rc013_dir / f"{rc013_label}.event-100.bin",
            rc013_dir / f"{rc013_label}.event-100.bin.part",
            rc013_dir / f"{rc013_label}.event-100.bin.part.json",
            rc013_dir / f"{rc013_label}.json",
            program_stdout,
            program_stderr,
            readback_path,
            readback_stdout,
            readback_stderr,
            readiness_stdout,
            readiness_stderr,
            acceptance_stdout,
            acceptance_stderr,
            acceptance_dir / "runtime-acceptance.raw.jsonl",
            acceptance_dir / "runtime-acceptance.frames.jsonl",
            acceptance_result_path,
        )
        try:
            for evidence_path in evidence_paths:
                require_fresh_file(evidence_path)
        except FileExistsError as error:
            first_failure = str(error)
            result["status"] = "BLOCKED_EXISTING_EVIDENCE"

    if first_failure is None:
        try:
            preflight = collect_stlink_probe_preflight(
                programmer=programmer,
                probe_serial=probe_serial,
                output_dir=stlink_dir,
                runner=runner,
            )
            result["stlink_probe_preflight"] = preflight
            if preflight.get("pass") is not True:
                first_failure = "ST-Link probe enumeration failed"
                result["status"] = "STLINK_PREFLIGHT_FAIL"
        except Exception as error:
            first_failure = (
                "ST-Link probe enumeration failed: "
                f"{type(error).__name__}: {error}"
            )
            result["status"] = "STLINK_PREFLIGHT_FAIL"

    if first_failure is None:
        try:
            rc013_readback = rc013_readback_fn(
                port,
                rc013_dir,
                rc013_label,
            )
            result["rc013_readback"] = rc013_readback
            rc013_failure = _rc013_readback_failure(rc013_readback)
        except Exception as error:
            rc013_readback = None
            result["rc013_readback"] = rc013_readback
            rc013_failure = f"{type(error).__name__}: {error}"
        if rc013_failure is not None:
            first_failure = f"RC-013 readback failed: {rc013_failure}"
            result["status"] = "RC013_READBACK_FAIL"

    def run_step(
        name: str,
        command: list[str],
        stdout_path: Path,
        stderr_path: Path,
    ) -> int | None:
        nonlocal first_failure
        exit_code: int | None = None
        error_text: str | None = None
        try:
            exit_code = runner(command, stdout_path, stderr_path)
        except Exception as error:
            error_text = f"{type(error).__name__}: {error}"
            first_failure = f"{name}: {error_text}"
        commands = result["commands"]
        assert isinstance(commands, list)
        commands.append(
            _command_record(
                name,
                command,
                stdout_path,
                stderr_path,
                exit_code,
                error_text,
            )
        )
        if first_failure is None and exit_code != 0:
            first_failure = f"{name}: command exited with code {exit_code}"
        return exit_code

    if first_failure is None:
        program_command = [
            str(programmer),
            "-c",
            "port=SWD",
            f"sn={probe_serial}",
            "freq=400",
            "mode=NORMAL",
            "reset=SWrst",
            "-w",
            str(v1_image),
            f"0x{APPLICATION_ADDRESS:08X}",
            "-v",
            "-rst",
        ]
        result["program_attempts"] = 1
        run_step("program", program_command, program_stdout, program_stderr)
        if first_failure is not None:
            result["status"] = "PROGRAM_FAIL"

    if first_failure is None:
        readback_command = [
            str(programmer),
            "-c",
            "port=SWD",
            f"sn={probe_serial}",
            "freq=400",
            "mode=NORMAL",
            "reset=SWrst",
            "-u",
            f"0x{APPLICATION_ADDRESS:08X}",
            str(V1_BYTES),
            str(readback_path),
            "-rst",
        ]
        run_step("readback", readback_command, readback_stdout, readback_stderr)
        if first_failure is None and not readback_path.is_file():
            first_failure = "readback: exact output binary was not created"
        if first_failure is None:
            readback_bytes = readback_path.stat().st_size
            readback_sha256 = sha256_file(readback_path)
            result["readback_bytes"] = readback_bytes
            result["readback_sha256"] = readback_sha256
            if readback_bytes != V1_BYTES:
                first_failure = f"readback length is not {V1_BYTES} bytes"
            elif readback_sha256.lower() != V1_SHA256.lower():
                first_failure = "readback SHA-256 does not match verified v1"
        if first_failure is not None:
            result["status"] = "READBACK_FAIL"

    if first_failure is None:
        readiness_command = [
            str(Path(sys.executable).resolve()),
            "-m",
            "transport_recorder.cli",
            "info",
            "--port",
            port,
            "--startup-timeout",
            "15",
        ]
        run_step(
            "runtime-readiness",
            readiness_command,
            readiness_stdout,
            readiness_stderr,
        )
        if first_failure is not None:
            result["status"] = "RUNTIME_READINESS_FAIL"

    if first_failure is None:
        acceptance_script = (
            Path(__file__).resolve().parent / "hardware_acceptance_readback.py"
        )
        acceptance_command = [
            str(Path(sys.executable).resolve()),
            str(acceptance_script),
            "--port",
            port,
            "--output-dir",
            str(acceptance_dir),
            "--label",
            "runtime-acceptance",
        ]
        run_step(
            "runtime-acceptance",
            acceptance_command,
            acceptance_stdout,
            acceptance_stderr,
        )
        acceptance: object = None
        if first_failure is None:
            try:
                acceptance = json.loads(
                    acceptance_result_path.read_text(encoding="utf-8")
                )
            except Exception as error:
                first_failure = (
                    "runtime acceptance result could not be read: "
                    f"{type(error).__name__}: {error}"
                )
        if isinstance(acceptance, dict):
            result["runtime_acceptance"] = acceptance
        if first_failure is None:
            first_failure = _acceptance_failure(acceptance, port)
        if first_failure is not None:
            result["status"] = "RUNTIME_ACCEPTANCE_FAIL"

    if first_failure is None:
        result["pass"] = True
        result["status"] = "PASS"
    else:
        result["error"] = first_failure
        if result["status"] == "NOT_STARTED":
            result["status"] = "VALIDATION_FAIL"
    result["ended_at_utc"] = _timestamp()
    _write_json_once(result_path, result)
    return result


def _absolute_path(value: str) -> Path:
    path = Path(value)
    if not path.is_absolute():
        raise argparse.ArgumentTypeError("path must be absolute")
    return path


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--programmer", required=True, type=_absolute_path)
    parser.add_argument("--probe-serial", required=True)
    parser.add_argument("--port", required=True)
    parser.add_argument("--v1-image", required=True, type=_absolute_path)
    parser.add_argument("--output-dir", required=True, type=_absolute_path)
    parser.add_argument("--confirm", required=True)
    args = parser.parse_args(argv)

    try:
        result = restore_verified_v1(
            programmer=args.programmer,
            probe_serial=args.probe_serial,
            port=args.port,
            v1_image=args.v1_image,
            output_dir=args.output_dir,
            confirmation=args.confirm,
        )
    except Exception as error:
        print(f"ERROR: {type(error).__name__}: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("pass") is True else 1


if __name__ == "__main__":
    raise SystemExit(main())
