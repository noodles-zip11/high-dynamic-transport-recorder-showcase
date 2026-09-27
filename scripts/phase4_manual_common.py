"""Shared helpers for the Phase 4 manual command pack.

The SWD helper in this module is deliberately read-only.  The other manual
commands can reuse the evidence and hashing helpers without gaining a way to
program, erase, reset, or unlock a target implicitly.
"""

from __future__ import annotations

from datetime import datetime, timezone
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Callable, TypeAlias
import zlib


EXPECTED_STLINK_SERIAL = "DEVICE_SERIAL_REDACTED__"
APPLICATION_ADDRESS = 0x08020000
UID_ADDRESS = 0x1FF1E800
EXPECTED_DEVICE_ID = 0x450
EXPECTED_DEVICE_NAME = "STM32H743"
EXPECTED_UID_WORDS = (
    0x003E0026,
    0x3433510C,
    0x34393738,
)
RC013_APPLICATION_WORDS = (
    0x24001600,
    0x08024819,
    0x0802486D,
    0x0802041D,
)
MIN_TARGET_VOLTAGE = 3.0
MAX_TARGET_VOLTAGE = 3.6
RC013_CAPABILITY_FLAGS = 491
RC013_EVENT_ID = 100
RC013_EVENT_BYTES = 38560
RC013_EVENT_CRC32 = 1570525185
RC013_EVENT_SHA256 = (
    "096B36A11764BCEB77BC391DC1758736143EFAED43942366409E946D1F90263A"
)

CommandRunner: TypeAlias = Callable[[list[str], Path, Path], int]

_IDENTITY_SERIAL_RE = re.compile(
    r"ST[- ]LINK\s+SN\s*[:=]\s*([^\s]+)", re.IGNORECASE
)
_FIRMWARE_RE = re.compile(
    r"ST[- ]LINK\s+FW\s*[:=]\s*([^\r\n]*)", re.IGNORECASE
)
_VOLTAGE_RE = re.compile(
    r"(?:target\s+)?voltage\s*[:=]\s*"
    r"([0-9]+(?:\.[0-9]+)?)\s*V\b",
    re.IGNORECASE,
)
_DEVICE_ID_RE = re.compile(
    r"device\s+id\s*[:=]\s*(0x[0-9a-f]+)", re.IGNORECASE
)
_DEVICE_NAME_RE = re.compile(
    r"device\s+name\s*[:=]\s*([^\r\n]*)", re.IGNORECASE
)
_WORD_LINE_RE = re.compile(
    r"^\s*(0x[0-9a-f]+)\s*:\s*(.*?)\s*$",
    re.IGNORECASE | re.MULTILINE,
)
_WORD_TOKEN_RE = re.compile(r"(?:0x)?([0-9a-f]{1,8})\b", re.IGNORECASE)
_ERROR_LINE_RE = re.compile(r"^\s*Error:", re.MULTILINE)


def _timestamp() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def sha256_file(path: Path) -> str:
    """Return the lowercase SHA-256 digest of *path*."""

    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_fresh_file(path: Path) -> None:
    """Reject an evidence path that would otherwise be overwritten."""

    candidate = Path(path)
    if candidate.exists():
        raise FileExistsError(
            f"immutable evidence path already exists: {candidate}"
        )


def run_logged(
    command: list[str],
    stdout_path: Path,
    stderr_path: Path,
) -> int:
    """Run *command* while capturing stdout and stderr in fresh files."""

    stdout_path = Path(stdout_path)
    stderr_path = Path(stderr_path)
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    stderr_path.parent.mkdir(parents=True, exist_ok=True)
    require_fresh_file(stdout_path)
    require_fresh_file(stderr_path)
    with stdout_path.open("x", encoding="utf-8", newline="\n") as stdout:
        with stderr_path.open("x", encoding="utf-8", newline="\n") as stderr:
            completed = subprocess.run(
                command,
                stdout=stdout,
                stderr=stderr,
                check=False,
            )
    return completed.returncode


def _read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def _word_values(output: str, address: int, count: int) -> list[int] | None:
    """Extract exactly the requested contiguous words from a read log."""

    values: dict[int, int] = {}
    for address_text, words_text in _WORD_LINE_RE.findall(output):
        word_address = int(address_text, 16)
        for value_match in _WORD_TOKEN_RE.finditer(words_text):
            if address <= word_address < address + count * 4:
                values[word_address] = int(value_match.group(1), 16)
            word_address += 4

    expected_addresses = [address + index * 4 for index in range(count)]
    if any(word_address not in values for word_address in expected_addresses):
        return None
    return [values[word_address] for word_address in expected_addresses]


def _probe_identity_failure(output: str, probe_serial: str) -> str | None:
    serials = [value.strip() for value in _IDENTITY_SERIAL_RE.findall(output)]
    if serials != [probe_serial]:
        if not serials:
            return "ST-Link serial is missing from -l output"
        return "-l output does not identify exactly the requested ST-Link"

    firmware_match = _FIRMWARE_RE.search(output)
    firmware = firmware_match.group(1).strip() if firmware_match else ""
    if not firmware:
        return "ST-Link firmware is missing or blank"
    return None


def _target_identity_failure(output: str) -> str | None:
    device_id_match = _DEVICE_ID_RE.search(output)
    if device_id_match is None:
        return "STM32 device ID is missing"
    if int(device_id_match.group(1), 16) != EXPECTED_DEVICE_ID:
        return "STM32 device ID is not 0x450 (STM32H743)"

    device_name_match = _DEVICE_NAME_RE.search(output)
    device_name = (
        device_name_match.group(1).strip()
        if device_name_match is not None
        else ""
    )
    normalized_name = device_name.upper()
    if not (
        normalized_name.startswith(EXPECTED_DEVICE_NAME)
        or normalized_name == "STM32H7XX"
    ):
        return "STM32 device name is not STM32H743"

    voltage_match = _VOLTAGE_RE.search(output)
    if voltage_match is None:
        return "target voltage is missing"
    voltage = float(voltage_match.group(1))
    if not MIN_TARGET_VOLTAGE <= voltage <= MAX_TARGET_VOLTAGE:
        return "target voltage is outside the 3.0V to 3.6V range"
    return None


def _target_output_failure(output: str, target: str) -> str | None:
    if _ERROR_LINE_RE.search(output) is not None:
        return f"{target} output contains a line starting with Error:"
    return _target_identity_failure(output)


def _record_command(
    name: str,
    command: list[str],
    stdout_path: Path,
    stderr_path: Path,
    exit_code: int | None,
    error: str | None = None,
) -> dict[str, object]:
    stdout = _read_text(stdout_path)
    stderr = _read_text(stderr_path)
    record: dict[str, object] = {
        "name": name,
        "command": list(command),
        "stdout_path": str(stdout_path),
        "stderr_path": str(stderr_path),
        "stdout": stdout,
        "stderr": stderr,
        "exit_code": exit_code,
    }
    if stdout_path.is_file():
        record["stdout_sha256"] = sha256_file(stdout_path)
    if stderr_path.is_file():
        record["stderr_sha256"] = sha256_file(stderr_path)
    if error is not None:
        record["error"] = error
    return record


def _write_json_once(path: Path, payload: dict[str, object]) -> None:
    require_fresh_file(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(payload, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")


def _crc32_file(path: Path) -> int:
    checksum = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(64 * 1024), b""):
            checksum = zlib.crc32(block, checksum)
    return checksum & 0xFFFFFFFF


def collect_rc013_readback(
    port: str,
    output_dir: Path,
    label: str = "readback",
) -> dict[str, object]:
    """Collect one read-only RC-013 TERP identity and event readback.

    All protocol requests share one successful serial session.  Raw bytes,
    decoded frame summaries, the downloaded event, and the parsed result are
    stored under distinct immutable paths selected by *label*.
    """

    repo_root = Path(__file__).resolve().parents[1]
    host_root = str(repo_root / "host")
    if host_root not in sys.path:
        sys.path.insert(0, host_root)

    from transport_recorder.protocol.client import TerpClient
    from transport_recorder.protocol.serial_transport import SerialTransport

    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    raw_path = output_dir / f"{label}.raw.jsonl"
    frames_path = output_dir / f"{label}.frames.jsonl"
    event_path = output_dir / f"{label}.event-{RC013_EVENT_ID}.bin"
    result_path = output_dir / f"{label}.json"
    for path in (
        raw_path,
        frames_path,
        event_path,
        event_path.with_name(event_path.name + ".part"),
        event_path.with_name(event_path.name + ".part.json"),
        result_path,
    ):
        require_fresh_file(path)

    result: dict[str, object] = {
        "schema": "phase4-rc013-readback-v1",
        "label": label,
        "port": port,
        "read_only": True,
        "pass": False,
        "started_at_utc": _timestamp(),
    }
    client = None
    serial_transport = None
    raw_file = raw_path.open("x", encoding="utf-8", newline="\n")
    frames_file = frames_path.open("x", encoding="utf-8", newline="\n")

    class RecordingTransport:
        def write(self, data: bytes) -> None:
            record_raw("TX", data)
            assert serial_transport is not None
            serial_transport.write(data)

        def read(self, maximum_bytes: int) -> bytes:
            assert serial_transport is not None
            data = serial_transport.read(maximum_bytes)
            if data:
                record_raw("RX", data)
            return data

        def close(self) -> None:
            if serial_transport is not None:
                serial_transport.close()

    def record_raw(direction: str, data: bytes) -> None:
        raw_file.write(
            json.dumps(
                {
                    "timestamp_utc": _timestamp(),
                    "direction": direction,
                    "bytes": len(data),
                    "hex": data.hex().upper(),
                },
                ensure_ascii=False,
                separators=(",", ":"),
            )
            + "\n"
        )
        raw_file.flush()

    def record_frame(frame: object) -> None:
        frames_file.write(
            json.dumps(
                {"timestamp_utc": _timestamp(), **asdict(frame)},
                ensure_ascii=False,
                separators=(",", ":"),
            )
            + "\n"
        )
        frames_file.flush()

    def open_recording_transport() -> RecordingTransport:
        nonlocal serial_transport
        serial_transport = SerialTransport.open(
            port, baudrate=115200, timeout_seconds=0.2
        )
        return RecordingTransport()

    try:
        client, info = TerpClient.connect_with_retry(
            open_recording_transport,
            startup_timeout_seconds=15.0,
            retry_interval_seconds=0.5,
            timeout_seconds=2.0,
            trace=record_frame,
        )
        health = client.get_health()

        events = []
        after_event_id = 0
        for _ in range(256):
            page, next_event_id = client.list_events(after_event_id, 16)
            events.extend(page)
            if next_event_id == 0:
                break
            if next_event_id <= after_event_id:
                raise RuntimeError("LIST_EVENTS cursor did not advance")
            after_event_id = next_event_id
        else:
            raise RuntimeError("LIST_EVENTS exceeded 256 pages")

        listed_event = next(
            (event for event in events if event.event_id == RC013_EVENT_ID),
            None,
        )
        event_readback: dict[str, object] = {
            "event_id": RC013_EVENT_ID,
            "listed": listed_event is not None,
        }
        if listed_event is not None:
            event_info = client.get_event_info(RC013_EVENT_ID)
            client.download_event(RC013_EVENT_ID, event_path)
            event_readback.update(
                {
                    "listed_event": asdict(listed_event),
                    "event_info": asdict(event_info),
                    "download": {
                        "path": str(event_path),
                        "bytes": event_path.stat().st_size,
                        "crc32": _crc32_file(event_path),
                        "sha256": sha256_file(event_path).upper(),
                    },
                }
            )

        result.update(
            {
                "device_info": asdict(info),
                "health": asdict(health),
                "event_count": len(events),
                "event_ids": [event.event_id for event in events],
                "event_readback": event_readback,
            }
        )
        result["pass"] = True
        readback_error = _rc013_readback_failure(result)
        if readback_error is not None:
            result["pass"] = False
            result["error"] = readback_error
    except Exception as error:
        result["error"] = f"{type(error).__name__}: {error}"
    finally:
        if client is not None:
            client.close()
        elif serial_transport is not None:
            serial_transport.close()
        raw_file.close()
        frames_file.close()
        result["ended_at_utc"] = _timestamp()
        result["raw_log"] = {
            "path": str(raw_path),
            "bytes": raw_path.stat().st_size,
            "sha256": sha256_file(raw_path).upper(),
        }
        result["frame_summary"] = {
            "path": str(frames_path),
            "bytes": frames_path.stat().st_size,
            "sha256": sha256_file(frames_path).upper(),
        }
        _write_json_once(result_path, result)
    return result


def _rc013_readback_failure(result: dict[str, object]) -> str | None:
    """Return the first frozen RC-013 readback mismatch."""

    if str(result.get("port", "")).upper() != "COM13":
        return "readback port is not the frozen explicit COM13"
    device_info = result.get("device_info")
    if not isinstance(device_info, dict):
        return "device identity is missing"
    if device_info.get("serial_number") != "recorder-001":
        return "device serial is not recorder-001"
    if device_info.get("capability_flags") != RC013_CAPABILITY_FLAGS:
        return "capability flags are not 491"

    health = result.get("health")
    if not isinstance(health, dict):
        return "health snapshot is missing"
    if health.get("storage_ready") is not True:
        return "storage is not ready"
    if health.get("storage_error_count") != 0:
        return "storage error count is nonzero"
    if health.get("event_export_error_count") != 0:
        return "event export error count is nonzero"

    event = result.get("event_readback")
    if not isinstance(event, dict) or event.get("event_id") != RC013_EVENT_ID:
        return "event 100 readback is missing"
    if event.get("listed") is not True:
        return "event 100 is not listed"
    info = event.get("event_info")
    download = event.get("download")
    if not isinstance(info, dict) or not isinstance(download, dict):
        return "event 100 metadata or download is missing"
    if info.get("total_length") != RC013_EVENT_BYTES:
        return "event 100 device length is not 38560"
    if download.get("bytes") != RC013_EVENT_BYTES:
        return "event 100 downloaded length is not 38560"
    if info.get("event_crc32") != RC013_EVENT_CRC32:
        return "event 100 device CRC32 does not match the frozen baseline"
    if download.get("crc32") != RC013_EVENT_CRC32:
        return "event 100 downloaded CRC32 does not match the device"
    if str(download.get("sha256", "")).upper() != RC013_EVENT_SHA256:
        return "event 100 SHA-256 does not match the frozen baseline"
    if result.get("pass") is not True:
        if "error" in result:
            return str(result["error"])
        return "readback did not report pass=true"
    return None


def collect_stlink_probe_preflight(
    programmer: Path,
    probe_serial: str,
    output_dir: Path,
    runner: CommandRunner = run_logged,
) -> dict[str, object]:
    """Enumerate and validate one exact ST-Link without connecting to a target."""

    programmer = Path(programmer).resolve()
    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    result_path = output_dir / "stlink-probe-preflight.json"
    stdout_path = output_dir / "identity.stdout.log"
    stderr_path = output_dir / "identity.stderr.log"
    for path in (result_path, stdout_path, stderr_path):
        require_fresh_file(path)

    probe_serial = str(probe_serial).strip()
    command = [str(programmer), "-l", "stlink"]
    result: dict[str, object] = {
        "schema": "phase4-stlink-probe-preflight-v1",
        "read_only": True,
        "mutating": False,
        "pass": False,
        "programmer": str(programmer),
        "probe_serial": probe_serial,
        "started_at_utc": _timestamp(),
        "commands": [],
    }

    first_failure: str | None = None
    if not probe_serial:
        first_failure = "probe serial is required"
    elif probe_serial != EXPECTED_STLINK_SERIAL:
        first_failure = "probe serial does not match the frozen ST-Link identity"

    if first_failure is None:
        exit_code: int | None = None
        command_error: str | None = None
        try:
            exit_code = runner(command, stdout_path, stderr_path)
        except Exception as error:  # preserve the first runner failure
            command_error = f"{type(error).__name__}: {error}"
        record = _record_command(
            "identity",
            command,
            stdout_path,
            stderr_path,
            exit_code,
            command_error,
        )
        result["commands"].append(record)

        if command_error is not None:
            first_failure = f"identity: {command_error}"
        elif exit_code != 0:
            first_failure = f"identity: programmer exited with code {exit_code}"
        else:
            stdout = str(record["stdout"])
            result["cli_header"] = next(
                (line.strip() for line in stdout.splitlines() if line.strip()),
                "",
            )
            first_failure = _probe_identity_failure(stdout, probe_serial)

    if first_failure is None:
        result["pass"] = True
    else:
        result["error"] = first_failure
    result["ended_at_utc"] = _timestamp()
    _write_json_once(result_path, result)
    return result


def collect_swd_preflight(
    programmer: Path,
    probe_serial: str,
    output_dir: Path,
    runner: CommandRunner = run_logged,
) -> dict[str, object]:
    """Collect and validate the three read-only SWD preflight commands.

    The first command enumerates ST-Link probes.  The following two commands
    connect to the explicitly supplied serial and read the MCU UID and the
    first four application words.  A failed identity or read check stops the
    sequence before another programmer command is attempted.
    """

    programmer = Path(programmer).resolve()
    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    result_path = output_dir / "swd-preflight.json"
    require_fresh_file(result_path)

    probe_serial = str(probe_serial).strip()
    result: dict[str, object] = {
        "schema": "phase4-swd-preflight-v1",
        "read_only": True,
        "mutating": False,
        "pass": False,
        "programmer": str(programmer),
        "probe_serial": probe_serial,
        "started_at_utc": _timestamp(),
        "commands": [],
    }
    commands: list[tuple[str, list[str]]] = [
        (
            "identity",
            [str(programmer), "-l", "stlink"],
        ),
        (
            "uid",
            [
                str(programmer),
                "-c",
                "port=SWD",
                f"sn={probe_serial}",
                "freq=400",
                "mode=HOTPLUG",
                "-r32",
                f"0x{UID_ADDRESS:08X}",
                "12",
            ],
        ),
        (
            "application",
            [
                str(programmer),
                "-c",
                "port=SWD",
                f"sn={probe_serial}",
                "freq=400",
                "mode=HOTPLUG",
                "-r32",
                f"0x{APPLICATION_ADDRESS:08X}",
                "16",
            ],
        ),
    ]

    log_paths = {
        name: (
            output_dir / f"{name}.stdout.log",
            output_dir / f"{name}.stderr.log",
        )
        for name, _ in commands
    }
    for stdout_path, stderr_path in log_paths.values():
        require_fresh_file(stdout_path)
        require_fresh_file(stderr_path)

    first_failure: str | None = None
    if not probe_serial:
        first_failure = "probe serial is required"
    elif probe_serial != EXPECTED_STLINK_SERIAL:
        first_failure = (
            "probe serial does not match the frozen ST-Link identity"
        )

    for name, command in commands:
        if first_failure is not None:
            break
        stdout_path, stderr_path = log_paths[name]
        exit_code: int | None = None
        command_error: str | None = None
        try:
            exit_code = runner(command, stdout_path, stderr_path)
        except Exception as error:  # preserve the first runner failure
            command_error = f"{type(error).__name__}: {error}"
        record = _record_command(
            name,
            command,
            stdout_path,
            stderr_path,
            exit_code,
            command_error,
        )
        result["commands"].append(record)

        if command_error is not None:
            first_failure = f"{name}: {command_error}"
            continue
        if exit_code != 0:
            first_failure = f"{name}: programmer exited with code {exit_code}"
            continue

        stdout = str(record["stdout"])
        stderr = str(record["stderr"])
        output = f"{stdout}\n{stderr}"
        if name == "identity":
            first_failure = _probe_identity_failure(stdout, probe_serial)
        elif name == "uid":
            first_failure = _target_output_failure(output, "uid")
            if first_failure is None:
                uid_words = _word_values(stdout, UID_ADDRESS, 3)
                if uid_words is None:
                    first_failure = (
                        "uid read does not contain all three words"
                    )
                else:
                    result["uid_words"] = uid_words
                    if tuple(uid_words) != EXPECTED_UID_WORDS:
                        first_failure = "uid words do not match the sealed UID"
        else:
            first_failure = _target_output_failure(output, "application")
            if first_failure is None:
                application_words = _word_values(
                    stdout, APPLICATION_ADDRESS, 4
                )
                if application_words is None:
                    first_failure = (
                        "application read does not contain all four words"
                    )
                else:
                    result["application_words"] = application_words
                    if tuple(application_words) != RC013_APPLICATION_WORDS:
                        first_failure = (
                            "application words do not match the sealed RC-013 "
                            "image"
                        )

    if first_failure is None:
        result["pass"] = True
    else:
        result["error"] = first_failure
    result["ended_at_utc"] = _timestamp()
    _write_json_once(result_path, result)
    return result
