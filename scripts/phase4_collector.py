"""Collect additive Phase 4 TERP evidence without performing board actions."""

from __future__ import annotations

import argparse
from dataclasses import asdict, is_dataclass
from hashlib import sha256
import json
from pathlib import Path
import stat
import struct
import sys
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "host"))

from scripts.phase4_evidence import (  # noqa: E402
    _sha256,
    _resolve_under,
    create_dry_run,
    validate_manifest,
)
from scripts.hardware_acceptance_readback import (  # noqa: E402
    RecordingTransport,
    digest,
    timestamp,
)
from transport_recorder.protocol.client import TerpClient  # noqa: E402
from transport_recorder.protocol.serial_transport import (  # noqa: E402
    SerialTransport,
)


OPERATIONS = ("event-evidence", "crash-record", "ack-crash-record")
SESSION_PROFILE = "FaultInjection"


def _json_value(value: Any) -> Any:
    if is_dataclass(value):
        return {key: _json_value(item) for key, item in asdict(value).items()}
    if isinstance(value, dict):
        return {key: _json_value(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_value(item) for item in value]
    if hasattr(value, "value"):
        return value.value
    return value


def build_dry_run_commands(root: Path) -> str:
    """Render explicit, non-executing operator commands for H0."""
    root = Path(root).resolve()
    sealed_bin = (
        root
        / "firmware"
        / "fault-injection"
        / "transport_recorder.bin"
    )
    collector = "python scripts/phase4_collector.py collect"
    return (
        "# Phase 4 H0-H5 command plan (DRY RUN; not executed)\n"
        "# Replace <EXPLICIT_COM> and placeholders after board identity "
        "checks.\n"
        "# Never auto-select a port, flash, trigger a fault, or ACK a "
        "record.\n"
        f"sealed_bin={sealed_bin}\n"
        f"{collector} --port <EXPLICIT_COM> --output-dir "
        f"{root / 'results'} --operation event-evidence "
        f"--session-manifest {root / 'manifest.json'} "
        f"--sealed-bin {sealed_bin} --event-id <EVENT_ID>\n"
        f"{collector} --port <EXPLICIT_COM> --output-dir "
        f"{root / 'faults'} --operation crash-record --sequence <SEQUENCE> "
        f"--session-manifest {root / 'manifest.json'} "
        f"--sealed-bin {sealed_bin}\n"
        f"{collector} --port <EXPLICIT_COM> --output-dir "
        f"{root / 'results'} --operation ack-crash-record "
        f"--session-manifest {root / 'manifest.json'} "
        f"--sealed-bin {sealed_bin} --sequence <SEQUENCE>\n"
        "# Sequence-exact ACK is a separate explicit command: "
        "--ack-crash-sequence <SEQUENCE>.\n"
        "# ACK_CRASH_RECORD is never issued by the download operation.\n"
    )


def write_failure_log(root: Path, relative_path: str, payload: object) -> Path:
    """Create one immutable failure log; never replace an existing one."""
    path = _resolve_under(Path(root).resolve(), relative_path, "failure log")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(
            payload,
            stream,
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        )
        stream.write("\n")
    os_mode = path.stat().st_mode
    path.chmod(os_mode & ~stat.S_IWUSR & ~stat.S_IWGRP & ~stat.S_IWOTH)
    return path


def _parse_uint32(value: int, label: str, *, allow_zero: bool = False) -> int:
    minimum = 0 if allow_zero else 1
    if not minimum <= value <= 0xFFFFFFFF:
        raise ValueError(f"{label} must be a uint32")
    return value


def _download_exact_crash_record(
    client: TerpClient,
    sequence: int,
    chunk_bytes: int,
) -> tuple[int, bytes]:
    if not 0 < chunk_bytes <= 128:
        raise ValueError("CrashRecord chunk size must be between 1 and 128")
    offset = 0
    received_sequence = sequence
    record = bytearray()
    while offset < 128:
        requested = min(chunk_bytes, 128 - offset)
        chunk = client.get_crash_record_chunk(
            received_sequence,
            offset,
            requested,
        )
        if received_sequence == 0:
            received_sequence = chunk.sequence
        if chunk.sequence != received_sequence:
            raise ValueError("CrashRecord sequence changed during download")
        record.extend(chunk.data)
        offset += chunk.actual_length
    if len(record) != 128:
        raise ValueError("CrashRecord download is not exactly 128 bytes")
    return received_sequence, bytes(record)


def _record_identity(record: bytes) -> dict[str, Any]:
    return {
        "length": len(record),
        "sha256": sha256(record).hexdigest().upper(),
        "lr": struct.unpack_from("<I", record, 52)[0],
        "pc": struct.unpack_from("<I", record, 56)[0],
        "symbolization": "not_attempted",
    }


def _write_crash_record_raw(path: Path, record: bytes) -> dict[str, Any]:
    if len(record) != 128:
        raise ValueError("CrashRecord raw payload must be exactly 128 bytes")
    _new_path(path)
    path.write_bytes(record)
    identity = _record_identity(record)
    path.chmod(
        path.stat().st_mode
        & ~stat.S_IWUSR
        & ~stat.S_IWGRP
        & ~stat.S_IWOTH
    )
    return identity


def _load_session_binding(
    manifest_path: Path,
    sealed_bin: Path,
) -> dict[str, str]:
    """Validate and bind collection to the session's sealed FI image."""
    manifest_path = Path(manifest_path).resolve()
    manifest = validate_manifest(manifest_path)
    root = manifest_path.parent.resolve()
    sealed_bin = Path(sealed_bin).resolve()
    try:
        relative_bin = sealed_bin.relative_to(root).as_posix()
    except ValueError as exc:
        raise ValueError(
            "sealed BIN must be inside the session manifest"
        ) from exc
    entry = manifest["artifacts"]["fault_injection"]
    expected_path = entry["bin"]["path"]
    if relative_bin != expected_path:
        raise ValueError("sealed BIN is not the manifest FaultInjection BIN")
    if not sealed_bin.is_file():
        raise ValueError("sealed BIN does not exist")
    actual_hash = _sha256(sealed_bin)
    if actual_hash != entry["bin"]["sha256"]:
        raise ValueError("sealed BIN sha256 does not match session manifest")
    if entry.get("profile") != SESSION_PROFILE:
        raise ValueError("session FaultInjection profile is missing")
    build_id = entry.get("build_id")
    if not isinstance(build_id, str) or not build_id:
        raise ValueError("session build_id is missing")
    return {
        "source_revision": manifest["source_revision"],
        "profile": entry["profile"],
        "build_id": build_id,
        "sealed_bin": relative_bin,
        "sealed_bin_sha256": actual_hash,
    }


def _set_operation_semantics(result: dict[str, Any], operation: str) -> None:
    is_ack = operation == "ack-crash-record"
    result["read_only"] = not is_ack
    result["mutating"] = is_ack
    result["explicit_ack"] = is_ack


def _new_path(path: Path) -> None:
    if path.exists():
        raise FileExistsError(
            f"immutable evidence path already exists: {path}"
        )


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    collect = commands.add_parser("collect")
    collect.add_argument("--dry-run", action="store_true")
    collect.add_argument("--output-dir", required=True, type=Path)
    collect.add_argument("--source-revision", default=None)
    collect.add_argument("--session-manifest", type=Path)
    collect.add_argument("--port")
    collect.add_argument("--operation", choices=OPERATIONS)
    collect.add_argument("--event-id", type=int)
    collect.add_argument("--sequence", type=int)
    collect.add_argument("--chunk-bytes", type=int, default=128)
    collect.add_argument("--sealed-bin", type=Path)
    collect.add_argument("--label", default="phase4")
    collect.add_argument("--timeout", type=float, default=2.0)
    return parser


def _collect(args: argparse.Namespace) -> int:
    if args.dry_run:
        revision = args.source_revision or "0" * 40
        manifest = create_dry_run(args.output_dir, source_revision=revision)
        print(json.dumps({"created": str(manifest)}, sort_keys=True))
        return 0
    if not args.port:
        raise ValueError("--port is required for collection")
    if args.session_manifest is None:
        raise ValueError("--session-manifest is required for collection")
    if args.sealed_bin is None:
        raise ValueError("--sealed-bin is required for collection")
    if args.operation is None:
        raise ValueError("--operation is required for collection")
    if args.operation == "event-evidence":
        if args.event_id is None or args.event_id <= 0:
            raise ValueError("event-evidence requires a positive --event-id")
    elif args.sequence is None:
        raise ValueError(f"{args.operation} requires --sequence")
    else:
        _parse_uint32(args.sequence, "--sequence", allow_zero=True)
    binding = _load_session_binding(args.session_manifest, args.sealed_bin)
    if args.source_revision is not None and (
        args.source_revision.lower() != binding["source_revision"]
    ):
        raise ValueError("source revision does not match session manifest")

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    raw_path = output_dir / f"{args.label}.raw.jsonl"
    frames_path = output_dir / f"{args.label}.frames.jsonl"
    result_path = output_dir / f"{args.label}.json"
    _new_path(raw_path)
    _new_path(frames_path)
    result: dict[str, Any] = {
        "schema": "phase4-collector-v1",
        "operation": args.operation,
        "port": args.port,
        "pass": False,
        "started_at_utc": timestamp(),
        "source_revision": binding["source_revision"],
        "profile": binding["profile"],
        "build_id": binding["build_id"],
        "sealed_bin": binding["sealed_bin"],
        "sealed_bin_sha256": binding["sealed_bin_sha256"],
        "session_manifest": str(args.session_manifest.resolve()),
    }
    _set_operation_semantics(result, args.operation)
    frames_file = frames_path.open("x", encoding="utf-8", newline="\n")
    transport: RecordingTransport | None = None
    try:
        serial_transport = SerialTransport.open(
            args.port,
            baudrate=115200,
            timeout_seconds=0.2,
        )
        transport = RecordingTransport(serial_transport, raw_path)
        client = TerpClient(transport, timeout_seconds=args.timeout)
        client.trace = lambda frame: frames_file.write(
            json.dumps(
                {"timestamp_utc": timestamp(), **asdict(frame)},
                separators=(",", ":"),
            )
            + "\n"
        )
        client.hello()
        if args.operation == "event-evidence":
            evidence = client.get_event_evidence(args.event_id)
            result["event_evidence"] = _json_value(evidence)
        elif args.operation == "crash-record":
            actual_sequence, record = _download_exact_crash_record(
                client,
                args.sequence,
                args.chunk_bytes,
            )
            record_path = output_dir / f"crash-record-{actual_sequence}.bin"
            identity = _write_crash_record_raw(record_path, record)
            result["sequence"] = actual_sequence
            result["record"] = {
                "path": str(record_path),
                **identity,
            }
        else:
            acknowledged = client.ack_crash_record(args.sequence)
            result["sequence"] = acknowledged
            result["ack"] = {"sequence": acknowledged, "sent": True}
        result["pass"] = True
    except Exception as error:  # preserve failure evidence, never hide it
        result["error"] = f"{type(error).__name__}: {error}"
        failure_path = output_dir / "failure.json"
        if not failure_path.exists():
            write_failure_log(output_dir, "failure.json", result)
    finally:
        frames_file.close()
        if transport is not None:
            transport.close()
        for path in (raw_path, frames_path):
            if path.exists():
                path.chmod(path.stat().st_mode & ~stat.S_IWUSR & ~stat.S_IWGRP)
        result["ended_at_utc"] = timestamp()
        result["raw_log"] = digest(raw_path) if raw_path.exists() else None
        result["frame_summary"] = (
            digest(frames_path) if frames_path.exists() else None
        )
        result_path.write_text(
            json.dumps(
                result,
                ensure_ascii=False,
                indent=2,
                default=_json_value,
            )
            + "\n",
            encoding="utf-8",
        )
    print(
        json.dumps(
            result,
            ensure_ascii=False,
            indent=2,
            default=_json_value,
        )
    )
    return 0 if result["pass"] else 1


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.command == "collect":
        return _collect(args)
    raise AssertionError("unhandled Phase 4 collector command")


if __name__ == "__main__":
    raise SystemExit(main())
