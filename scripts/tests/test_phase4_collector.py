from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

import scripts.phase4_collector as phase4_collector
from host.transport_recorder.cli import build_parser
from scripts.phase4_collector import (
    build_dry_run_commands,
    _download_exact_crash_record,
    _record_identity,
    _write_crash_record_raw,
    write_failure_log,
)
from scripts.phase4_evidence import (
    create_dry_run,
    generate_sha256sums,
    verify_sha256sums,
)
from host.transport_recorder.protocol.client import CrashRecordChunk


SOURCE_REVISION = "1" * 40


def test_dry_run_writes_commands_and_verified_package_checksums(
    tmp_path: Path,
) -> None:
    manifest_path = create_dry_run(
        tmp_path,
        source_revision=SOURCE_REVISION,
    )

    commands_path = tmp_path / "commands" / "h0-runner.txt"
    assert commands_path.is_file()
    commands = commands_path.read_text(encoding="utf-8")
    assert "--port <EXPLICIT_COM>" in commands
    assert "--sealed-bin" in commands
    assert "ACK_CRASH_RECORD" in commands
    assert "auto" in commands.lower()

    checksums_path = tmp_path / "SHA256SUMS.txt"
    assert checksums_path.is_file()
    covered = verify_sha256sums(tmp_path)
    assert "manifest.json" in covered
    assert "commands/h0-runner.txt" in covered
    assert str(manifest_path.relative_to(tmp_path)) in covered

    resource_report = json.loads(
        (tmp_path / "results" / "resource-report.json").read_text(
            encoding="utf-8"
        )
    )
    assert resource_report["status"] == "NOT_EXECUTED"
    assert resource_report["resources"]["heap_bytes"] is None
    assert resource_report["timing"]["fault_to_reset_us"] is None

    ownership = json.loads(
        (tmp_path / "storage" / "pre-post-hashes.json").read_text(
            encoding="utf-8"
        )
    )
    assert ownership["status"] == "NOT_EXECUTED"
    assert ownership["u2"]["before_sha256"] is None
    assert ownership["qspi"]["after_sha256"] is None
    assert ownership["ota"]["before_sha256"] is None
    assert ownership["model"]["after_sha256"] is None
    assert ownership["sidecar"]["before_sha256"] is None

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert all(
        row["status"] == "NOT_EXECUTED"
        for row in manifest["hardware_gates"]
    )


def test_sha256sums_catches_mutation_and_path_escape(tmp_path: Path) -> None:
    payload = tmp_path / "results" / "sample.bin"
    payload.parent.mkdir(parents=True)
    payload.write_bytes(b"before")

    generate_sha256sums(tmp_path)
    assert verify_sha256sums(tmp_path)["results/sample.bin"] == hashlib.sha256(
        b"before"
    ).hexdigest()

    payload.write_bytes(b"after")
    with pytest.raises(ValueError, match="sha256 mismatch"):
        verify_sha256sums(tmp_path)

    payload.write_bytes(b"before")
    checksums = tmp_path / "SHA256SUMS.txt"
    checksums.write_text(
        "0" * 64 + "  ../outside.bin\n",
        encoding="utf-8",
    )
    with pytest.raises(ValueError, match="outside package root"):
        verify_sha256sums(tmp_path)


def test_failure_log_is_create_once_and_read_only(tmp_path: Path) -> None:
    path = write_failure_log(
        tmp_path,
        "faults/hardfault/failure.json",
        {"status": "FAIL", "reason": "probe unavailable"},
    )
    assert path.is_file()
    assert json.loads(path.read_text(encoding="utf-8"))["status"] == "FAIL"
    assert not path.stat().st_mode & 0o200

    with pytest.raises(FileExistsError):
        write_failure_log(tmp_path, "faults/hardfault/failure.json", {})


def test_crash_collector_preserves_exact_record_and_pc_lr() -> None:
    record = bytearray(128)
    record[52:56] = (0x08001235).to_bytes(4, "little")
    record[56:60] = (0x08004567).to_bytes(4, "little")

    class FakeClient:
        def __init__(self) -> None:
            self.calls: list[tuple[int, int, int]] = []

        def get_crash_record_chunk(
            self,
            sequence: int,
            offset: int,
            requested_length: int,
        ) -> CrashRecordChunk:
            self.calls.append((sequence, offset, requested_length))
            data = bytes(record[offset:offset + requested_length])
            return CrashRecordChunk(
                sequence=sequence,
                actual_offset=offset,
                total_length=128,
                actual_length=len(data),
                chunk_crc32=0,
                data=data,
            )

    client = FakeClient()
    sequence, downloaded = _download_exact_crash_record(client, 17, 32)

    assert sequence == 17
    assert downloaded == bytes(record)
    assert len(downloaded) == 128
    assert all(call[0] == 17 for call in client.calls)
    assert _record_identity(downloaded)["lr"] == 0x08001235
    assert _record_identity(downloaded)["pc"] == 0x08004567


def test_crash_record_raw_is_sealed_after_identity(tmp_path: Path) -> None:
    record = bytes(range(128))
    record_path = tmp_path / "crash-record-17.bin"

    identity = _write_crash_record_raw(record_path, record)

    assert record_path.read_bytes() == record
    assert identity["length"] == 128
    assert identity["sha256"] == hashlib.sha256(record).hexdigest().upper()
    assert not record_path.stat().st_mode & 0o200


def test_failure_log_rejects_a_path_outside_session(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="outside"):
        write_failure_log(tmp_path, "../failure.json", {})


def test_dry_run_commands_are_explicit_and_do_not_select_hardware(
    tmp_path: Path,
) -> None:
    commands = build_dry_run_commands(tmp_path)
    assert "COM12" not in commands
    assert "list_serial_ports" not in commands
    assert "--port <EXPLICIT_COM>" in commands
    assert "--ack-crash-sequence <SEQUENCE>" in commands


def test_host_cli_exposes_additive_reliability_operations() -> None:
    evidence = build_parser().parse_args(
        ["events", "evidence", "--port", "COM12", "--id", "7"]
    )
    assert evidence.event_command == "evidence"
    assert evidence.id == 7

    crash = build_parser().parse_args(
        [
            "crash-record",
            "download",
            "--port",
            "COM12",
            "--sequence",
            "9",
            "--output",
            "record.bin",
        ]
    )
    assert crash.sequence == 9
    assert crash.output == Path("record.bin")

    ack = build_parser().parse_args(
        ["crash-record", "ack", "--port", "COM12", "--sequence", "9"]
    )
    assert ack.sequence == 9


def test_ack_result_is_explicitly_mutating_and_not_read_only() -> None:
    result = {"read_only": True}

    assert hasattr(phase4_collector, "_set_operation_semantics")
    phase4_collector._set_operation_semantics(result, "ack-crash-record")

    assert result["read_only"] is False
    assert result["mutating"] is True
    assert result["explicit_ack"] is True


def test_collector_requires_exact_session_artifact_binding(
    tmp_path: Path,
) -> None:
    for profile in ("default-off", "fault-injection"):
        profile_value = (
            "Debug" if profile == "default-off" else "FaultInjection"
        )
        profile_dir = tmp_path / "firmware" / profile
        metadata_artifacts = []
        for filename in (
            "transport_recorder.elf",
            "transport_recorder.bin",
            "transport_recorder.map",
            "compile_commands.json",
        ):
            path = profile_dir / filename
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(f"{profile}:{filename}".encode("ascii"))
            metadata_artifacts.append(
                {
                    "path": filename,
                    "size_bytes": path.stat().st_size,
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                }
            )
        (profile_dir / "artifact-manifest.json").write_text(
            json.dumps(
                {
                    "schema_version": "phase4-sealed-firmware-v1",
                    "profile": profile_value,
                    "source_revision": SOURCE_REVISION,
                    "device_serial": "test-device",
                    "build_id": f"{profile_value.lower()}-build",
                    "artifacts": metadata_artifacts,
                }
            ),
            encoding="utf-8",
        )
    manifest_path = create_dry_run(
        tmp_path,
        source_revision=SOURCE_REVISION,
        artifact_root=tmp_path,
    )
    sealed_bin = (
        tmp_path
        / "firmware"
        / "fault-injection"
        / "transport_recorder.bin"
    )

    assert hasattr(phase4_collector, "_load_session_binding")
    binding = phase4_collector._load_session_binding(
        manifest_path, sealed_bin
    )

    assert binding["source_revision"] == SOURCE_REVISION
    assert binding["profile"] == "FaultInjection"
    assert binding["build_id"] == "faultinjection-build"
    assert binding["sealed_bin_sha256"] == hashlib.sha256(
        sealed_bin.read_bytes()
    ).hexdigest()
