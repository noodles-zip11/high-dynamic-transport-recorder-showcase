"""Validate and create the Phase 4 reliability evidence manifest."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
from typing import Any


SCHEMA_VERSION = "reliability-evidence-phase4-v1"
MANIFEST_TYPE = "phase4_gate"
V1_TAG_PEEL = "a69b6c6c71b91e1267780c169760eca284b7523b"

SOFTWARE_GATE_CASES = (
    "v1_baseline",
    "memory_layout",
    "ev03_el01",
    "event_gate",
    "ai",
    "terp",
    "crash_record",
    "release_profile",
    "controlled_enablement",
    "resource_regression",
    "host_evidence",
)
HARDWARE_GATE_CASES = ("H0", "H1", "H2", "H3", "H4", "H5")
PROFILE_NAMES = ("default_off", "fault_injection")
ARTIFACT_KINDS = ("elf", "bin", "map", "compile_commands")
ARTIFACT_FILENAMES = {
    "elf": "transport_recorder.elf",
    "bin": "transport_recorder.bin",
    "map": "transport_recorder.map",
    "compile_commands": "compile_commands.json",
}
SHA256SUMS_FILENAME = "SHA256SUMS.txt"

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))
VALID_STATUSES = {
    "PASS",
    "FAIL",
    "NOT_EXECUTED",
    "BLOCKED_HARDWARE",
}
GATE_REQUIRED_FIELDS = (
    "schema_version",
    "case",
    "status",
    "evidence_level",
    "source_revision",
    "profile",
    "feature_switches",
    "artifact_hashes",
    "build_id",
    "board_identity",
    "timestamps",
    "reset_source",
    "iteration_count",
    "expected",
    "actual",
    "evidence_paths",
    "raw_evidence_paths",
    "evidence_digests",
    "failure_notes",
    "rollback_notes",
    "reviewed_na",
)
REQUIRED_FEATURE_SWITCHES = (
    "TRANSPORT_RELIABILITY_EVIDENCE_ENABLED",
    "TRANSPORT_FAULT_INJECTION_ENABLED",
)
BOARD_IDENTITY_FIELDS = ("board", "pcb", "mcu", "probe", "uart", "media")
TIMESTAMP_FIELDS = ("started_at_utc", "ended_at_utc", "timezone")
REVIEWED_NA_FIELDS = {
    "profile",
    "artifact_hashes",
    "build_id",
    "board_identity",
    "timestamps",
    "reset_source",
    "evidence_paths",
    "raw_evidence_paths",
    "evidence_digests",
}
RAW_EVIDENCE_PREFIXES = (
    "results/raw/",
    "faults/",
    "events/",
    "storage/",
    "reconnect/",
    "baseline/",
)
SEALED_FIRMWARE_SCHEMA = "phase4-sealed-firmware-v1"
PROFILE_METADATA = {
    "default_off": ("default-off", "Debug"),
    "fault_injection": ("fault-injection", "FaultInjection"),
}
SOFTWARE_RESULT_SCHEMA = "reliability-evidence-software-result-v1"
SOFTWARE_RESULT_FIELDS = (
    "schema_version",
    "case",
    "expected_command",
    "oracle",
    "exit_code",
    "status",
    "pass_marker",
    "profile",
    "build_id",
    "artifact_hashes",
)
SOFTWARE_GATE_EXPECTATIONS = {
    "v1_baseline": {
        "command": "python -m pytest host/tests/test_v1_compatibility_contract.py -q",
        "oracle": "V1 compatibility contract: PASS",
        "profile": "default_off",
    },
    "memory_layout": {
        "command": "python -m pytest scripts/tests/test_crash_record_linker.py -q",
        "oracle": "CrashRecord linker boundaries: PASS",
        "profile": "default_off",
    },
    "ev03_el01": {
        "command": "native test_event_log test_event_export_debug test_ai_result_sidecar",
        "oracle": "EV03 EL01 sidecar compatibility: PASS",
        "profile": "default_off",
    },
    "event_gate": {
        "command": "native test_event_quality test_event_quality_service",
        "oracle": "event quality gate and seam: PASS",
        "profile": "fault_injection",
    },
    "ai": {
        "command": "native test_ai_inference_service test_ai_result_sidecar",
        "oracle": "AI admission and sidecar: PASS",
        "profile": "default_off",
    },
    "terp": {
        "command": "native test_terp_reliability test_terp_service_reliability",
        "oracle": "TERP reliability compatibility: PASS",
        "profile": "fault_injection",
    },
    "crash_record": {
        "command": "native test_crash_record test_reliability_evidence",
        "oracle": "CrashRecord capture and recovery: PASS",
        "profile": "fault_injection",
    },
    "release_profile": {
        "command": (
            "python -m pytest scripts/tests/test_fault_injection_profile.py "
            "scripts/tests/test_fault_injection_absent.py "
            "scripts/tests/test_reliability_build_option.py -q"
        ),
        "oracle": "profile isolation and Release absence: PASS",
        "profile": "fault_injection",
    },
    "controlled_enablement": {
        "command": (
            "python -m pytest scripts/tests/test_reliability_build_option.py "
            "scripts/tests/test_fault_injection_absent.py -q"
        ),
        "oracle": "controlled enablement: PASS",
        "profile": "fault_injection",
    },
    "resource_regression": {
        "command": "pwsh scripts/check_crash_fault_context.ps1",
        "oracle": "fault context and linker boundaries: PASS",
        "profile": "fault_injection",
    },
    "host_evidence": {
        "command": (
            "python -m pytest scripts/tests/test_phase4_evidence.py "
            "scripts/tests/test_phase4_collector.py -q"
        ),
        "oracle": "manifest and collector evidence: PASS",
        "profile": "fault_injection",
    },
}


class ManifestError(ValueError):
    """Raised when a Phase 4 manifest cannot be trusted."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _package_files(root: Path) -> list[tuple[str, Path]]:
    """Return package files in stable order, excluding the checksum file."""
    resolved_root = Path(root).resolve()
    files: list[tuple[str, Path]] = []
    for path in resolved_root.rglob("*"):
        if not path.is_file() or path.name == SHA256SUMS_FILENAME:
            continue
        resolved = path.resolve()
        try:
            relative = resolved.relative_to(resolved_root).as_posix()
        except ValueError as exc:
            raise ManifestError(
                f"package file is outside package root: {path}"
            ) from exc
        files.append((relative, resolved))
    return sorted(files)


def generate_sha256sums(root: Path) -> Path:
    """Write checksums for the manifest and every other package file."""
    root = Path(root).resolve()
    root.mkdir(parents=True, exist_ok=True)
    checksum_path = root / SHA256SUMS_FILENAME
    if checksum_path.exists():
        os.chmod(checksum_path, stat.S_IRUSR | stat.S_IWUSR)
    lines = [
        f"{_sha256(path)}  {relative}"
        for relative, path in _package_files(root)
    ]
    checksum_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return checksum_path


def verify_sha256sums(root: Path) -> dict[str, str]:
    """Verify package checksums and return the covered relative paths."""
    root = Path(root).resolve()
    checksum_path = root / SHA256SUMS_FILENAME
    try:
        lines = checksum_path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise ValueError("missing SHA256SUMS.txt") from exc

    expected: dict[str, str] = {}
    for line_number, line in enumerate(lines, start=1):
        if not line.strip():
            continue
        digest, separator, relative = line.partition("  ")
        if (
            not separator
            or len(digest) != 64
            or any(
                character not in "0123456789abcdefABCDEF"
                for character in digest
            )
        ):
            raise ValueError(f"malformed SHA256SUMS line {line_number}")
        candidate = Path(relative)
        if candidate.is_absolute() or ".." in candidate.parts:
            raise ValueError(
                f"checksum path outside package root: {relative}"
            )
        resolved = (root / candidate).resolve()
        try:
            canonical = resolved.relative_to(root).as_posix()
        except ValueError as exc:
            raise ValueError(
                f"checksum path outside package root: {relative}"
            ) from exc
        if canonical != relative or canonical == SHA256SUMS_FILENAME:
            raise ValueError(f"invalid checksum path: {relative}")
        if canonical in expected:
            raise ValueError(f"duplicate checksum path: {relative}")
        if not resolved.is_file():
            raise ValueError(f"missing checksum file: {relative}")
        expected[canonical] = digest.lower()

    actual_files = dict(_package_files(root))
    if set(expected) != set(actual_files):
        raise ValueError("SHA256SUMS coverage mismatch")
    for relative, path in actual_files.items():
        if _sha256(path) != expected[relative]:
            raise ValueError(f"sha256 mismatch: {relative}")
    return expected


def _resolve_under(root: Path, value: str, label: str) -> Path:
    if not isinstance(value, str) or not value:
        raise ManifestError(f"{label} must be a relative path")
    candidate = Path(value)
    if candidate.is_absolute():
        raise ManifestError(f"{label} must be relative")
    resolved_root = root.resolve()
    resolved = (resolved_root / candidate).resolve()
    try:
        resolved.relative_to(resolved_root)
    except ValueError as exc:
        raise ManifestError(f"{label} is outside manifest root") from exc
    return resolved


def _validate_revision(value: Any, label: str) -> str:
    if not isinstance(value, str) or len(value) != 40:
        raise ManifestError(f"{label} must be a 40-character Git revision")
    if any(character not in "0123456789abcdefABCDEF" for character in value):
        raise ManifestError(f"{label} must be hexadecimal")
    return value.lower()


def _validate_hash(value: Any, label: str, required: bool) -> str:
    if not value and not required:
        return ""
    if not isinstance(value, str) or len(value) != 64:
        raise ManifestError(f"{label} must be a SHA-256 hash")
    if any(character not in "0123456789abcdefABCDEF" for character in value):
        raise ManifestError(f"{label} must be hexadecimal")
    return value.lower()


def _require_reviewed_na(
    row: dict[str, Any], field: str, is_na: bool
) -> None:
    if not is_na:
        return
    reviewed_na = row["reviewed_na"]
    if field not in reviewed_na or not reviewed_na[field].strip():
        raise ManifestError(f"{field} requires an explicit reviewed N/A")


def _validate_evidence_paths(
    row: dict[str, Any], root: Path, required: bool
) -> None:
    paths = row["evidence_paths"]
    raw_paths = row["raw_evidence_paths"]
    digests = row["evidence_digests"]
    if not isinstance(paths, list) or not all(
        isinstance(value, str) for value in paths
    ):
        raise ManifestError("evidence_paths must be a string list")
    if not isinstance(raw_paths, list) or raw_paths != paths:
        raise ManifestError("raw_evidence_paths must match evidence_paths")
    if not isinstance(digests, list):
        raise ManifestError("evidence_digests must be a list")
    if required and not paths:
        raise ManifestError("PASS gate requires raw evidence_paths")
    if len(digests) != len(paths):
        raise ManifestError("evidence_digests must cover raw evidence_paths")

    for index, value in enumerate(paths):
        path = _resolve_under(root, value, f"evidence_paths[{index}]")
        normalized = value.replace("\\", "/")
        if not normalized.startswith(RAW_EVIDENCE_PREFIXES):
            raise ManifestError(
                f"raw evidence path is not an approved raw location: {value}"
            )
        if not path.is_file():
            raise ManifestError(f"missing evidence file: {value}")
        if path.stat().st_size == 0:
            raise ManifestError(f"raw evidence file is empty: {value}")
        digest = digests[index]
        if not isinstance(digest, dict):
            raise ManifestError("evidence digest must be an object")
        if digest.get("path") != value:
            raise ManifestError("evidence digest path mismatch")
        size = digest.get("size_bytes")
        if not isinstance(size, int) or isinstance(size, bool) or size < 1:
            raise ManifestError("evidence digest size_bytes is invalid")
        if size != path.stat().st_size:
            raise ManifestError(f"evidence size mismatch: {value}")
        expected_hash = _validate_hash(
            digest.get("sha256"),
            f"evidence_digests[{index}].sha256",
            True,
        )
        if _sha256(path) != expected_hash:
            raise ManifestError(f"evidence sha256 mismatch: {value}")


def _validate_gate_row(
    row: dict[str, Any],
    expected_cases: tuple[str, ...],
    kind: str,
    root: Path,
    source_revision: str,
    for_release: bool,
) -> None:
    case = row.get("case")
    missing = [field for field in GATE_REQUIRED_FIELDS if field not in row]
    if missing:
        raise ManifestError(
            f"{kind} gate {case!r} missing schema fields: "
            + ", ".join(missing)
        )
    if row["schema_version"] != SCHEMA_VERSION:
        raise ManifestError(f"{kind} gate {case} schema_version mismatch")
    if not isinstance(case, str) or case not in expected_cases:
        raise ManifestError(f"invalid {kind} gate case")
    status = row["status"]
    if status not in VALID_STATUSES:
        raise ManifestError(f"invalid status for {kind} gate {case}")
    evidence_level = row["evidence_level"]
    if evidence_level not in {
        "automated",
        "software",
        "physical_board",
        "blocked_hardware",
    }:
        raise ManifestError(f"invalid evidence_level for {kind} gate {case}")
    row_revision = row["source_revision"]
    if not isinstance(row_revision, str) or (
        row_revision.lower() != source_revision
    ):
        raise ManifestError(f"{kind} gate {case} source_revision mismatch")

    profile = row["profile"]
    if profile is not None and (
        not isinstance(profile, str) or not profile.strip()
    ):
        raise ManifestError(f"{kind} gate {case} profile is invalid")
    _require_reviewed_na(row, "profile", profile is None)

    switches = row["feature_switches"]
    if not isinstance(switches, dict):
        raise ManifestError(f"{kind} gate {case} feature_switches invalid")
    for switch in REQUIRED_FEATURE_SWITCHES:
        if not isinstance(switches.get(switch), bool):
            raise ManifestError(
                f"{kind} gate {case} feature switch {switch} invalid"
            )

    artifact_hashes = row["artifact_hashes"]
    if not isinstance(artifact_hashes, dict):
        raise ManifestError(f"{kind} gate {case} artifact_hashes invalid")
    for label, digest in artifact_hashes.items():
        if not isinstance(label, str) or not label:
            raise ManifestError(f"{kind} gate {case} artifact label invalid")
        _validate_hash(digest, f"{kind}.{case}.{label}", True)
    _require_reviewed_na(row, "artifact_hashes", not artifact_hashes)

    build_id = row["build_id"]
    if build_id is not None and (
        not isinstance(build_id, str) or not build_id.strip()
    ):
        raise ManifestError(f"{kind} gate {case} build_id is invalid")
    _require_reviewed_na(row, "build_id", build_id is None)

    board_identity = row["board_identity"]
    if not isinstance(board_identity, dict) or set(board_identity) != set(
        BOARD_IDENTITY_FIELDS
    ):
        raise ManifestError(f"{kind} gate {case} board_identity invalid")
    for field in BOARD_IDENTITY_FIELDS:
        value = board_identity[field]
        if value is not None and (
            not isinstance(value, str) or not value.strip()
        ):
            raise ManifestError(
                f"{kind} gate {case} board_identity.{field} invalid"
            )
    _require_reviewed_na(
        row,
        "board_identity",
        any(board_identity[field] is None for field in BOARD_IDENTITY_FIELDS),
    )
    if status == "PASS" and kind == "hardware" and any(
        board_identity[field] is None for field in BOARD_IDENTITY_FIELDS
    ):
        raise ManifestError(
            f"hardware gate {case} PASS requires complete board identity"
        )

    timestamps = row["timestamps"]
    if not isinstance(timestamps, dict) or set(timestamps) != set(
        TIMESTAMP_FIELDS
    ):
        raise ManifestError(f"{kind} gate {case} timestamps invalid")
    if not isinstance(timestamps["timezone"], str) or not timestamps[
        "timezone"
    ].strip():
        raise ManifestError(f"{kind} gate {case} timezone invalid")
    for field in ("started_at_utc", "ended_at_utc"):
        value = timestamps[field]
        if value is not None and (
            not isinstance(value, str) or not value.strip()
        ):
            raise ManifestError(f"{kind} gate {case} {field} invalid")
    _require_reviewed_na(
        row,
        "timestamps",
        timestamps["started_at_utc"] is None
        or timestamps["ended_at_utc"] is None,
    )

    reset_source = row["reset_source"]
    if reset_source is not None and (
        not isinstance(reset_source, str) or not reset_source.strip()
    ):
        raise ManifestError(f"{kind} gate {case} reset_source invalid")
    _require_reviewed_na(row, "reset_source", reset_source is None)

    iteration_count = row["iteration_count"]
    if (
        not isinstance(iteration_count, int)
        or isinstance(iteration_count, bool)
        or iteration_count < 0
    ):
        raise ManifestError(f"{kind} gate {case} iteration_count invalid")
    if status == "PASS" and iteration_count == 0:
        raise ManifestError(f"{kind} gate {case} PASS needs iteration_count")

    for field in ("expected", "actual"):
        value = row[field]
        if not isinstance(value, dict) or not value:
            raise ManifestError(f"{kind} gate {case} {field} invalid")
    if status == "PASS" and row["actual"].get("status") != "PASS":
        raise ManifestError(f"{kind} gate {case} actual status is not PASS")
    if not isinstance(row["failure_notes"], str):
        raise ManifestError(f"{kind} gate {case} failure_notes invalid")
    if not isinstance(row["rollback_notes"], str):
        raise ManifestError(f"{kind} gate {case} rollback_notes invalid")

    reviewed_na = row["reviewed_na"]
    if not isinstance(reviewed_na, dict):
        raise ManifestError(f"{kind} gate {case} reviewed_na invalid")
    if any(
        field not in REVIEWED_NA_FIELDS
        or not isinstance(reason, str)
        or not reason.strip()
        for field, reason in reviewed_na.items()
    ):
        raise ManifestError(
            f"{kind} gate {case} reviewed_na has invalid field"
        )
    _validate_evidence_paths(row, root, status == "PASS")
    _require_reviewed_na(row, "evidence_paths", not row["evidence_paths"])
    _require_reviewed_na(
        row, "raw_evidence_paths", not row["raw_evidence_paths"]
    )
    _require_reviewed_na(
        row, "evidence_digests", not row["evidence_digests"]
    )

    if status == "PASS" and kind == "hardware":
        if evidence_level != "physical_board":
            raise ManifestError(
                "hardware gate "
                f"{case} PASS requires physical_board evidence"
            )
    if for_release and status != "PASS":
        if kind == "hardware":
            raise ManifestError(
                "H0-H5 require physical_board/PASS for release"
            )
        raise ManifestError(f"software gate {case} is not PASS")


def _validate_gate_rows(
    rows: Any,
    expected_cases: tuple[str, ...],
    kind: str,
    root: Path,
    source_revision: str,
    for_release: bool,
) -> None:
    if not isinstance(rows, list):
        raise ManifestError(f"{kind} gates must be a list")
    cases = [
        row.get("case") if isinstance(row, dict) else None
        for row in rows
    ]
    if not all(isinstance(case, str) for case in cases):
        raise ManifestError(f"{kind} gate cases must be strings")
    if len(cases) != len(set(cases)):
        raise ManifestError(f"duplicate {kind} gate case")
    if set(cases) != set(expected_cases):
        raise ManifestError(f"{kind} gate cases do not match frozen set")

    for row in rows:
        if not isinstance(row, dict):
            raise ManifestError(f"{kind} gate row must be an object")
        _validate_gate_row(
            row,
            expected_cases,
            kind,
            root,
            source_revision,
            for_release,
        )


def _read_json(path: Path, label: str) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read {label}: {path}") from exc


def _read_sealed_profile_manifest(
    metadata_path: Path,
    root: Path,
    expected_profile: str,
    source_revision: str,
) -> dict[str, Any]:
    metadata = _read_json(metadata_path, "sealed artifact manifest")
    if not isinstance(metadata, dict):
        raise ManifestError(
            "sealed artifact manifest is not an object: "
            f"{metadata_path}"
        )
    if metadata.get("schema_version") != SEALED_FIRMWARE_SCHEMA:
        raise ManifestError(
            f"sealed artifact schema mismatch: {metadata_path}"
        )
    if metadata.get("profile") != expected_profile:
        raise ManifestError(
            f"sealed artifact profile mismatch: {metadata_path}"
        )
    metadata_revision = metadata.get("source_revision")
    if not isinstance(metadata_revision, str) or (
        metadata_revision.lower() != source_revision
    ):
        raise ManifestError(
            f"sealed artifact source_revision mismatch: {metadata_path}"
        )
    build_id = metadata.get("build_id")
    if not isinstance(build_id, str) or not build_id.strip():
        raise ManifestError(
            f"sealed artifact build_id is missing: {metadata_path}"
        )
    device_serial = metadata.get("device_serial")
    if not isinstance(device_serial, str) or not device_serial.strip():
        raise ManifestError(
            f"sealed artifact device_serial is missing: {metadata_path}"
        )

    entries = metadata.get("artifacts")
    if not isinstance(entries, list) or len(entries) != len(ARTIFACT_KINDS):
        raise ManifestError(
            f"sealed artifact list is incomplete: {metadata_path}"
        )
    expected_filenames = set(ARTIFACT_FILENAMES.values())
    by_filename: dict[str, dict[str, Any]] = {}
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise ManifestError(f"sealed artifact entry {index} is invalid")
        filename = entry.get("path")
        if not isinstance(filename, str) or Path(filename).name != filename:
            raise ManifestError(f"sealed artifact path {index} is invalid")
        if filename in by_filename or filename not in expected_filenames:
            raise ManifestError(
                f"sealed artifact path {filename} is unexpected"
            )
        size = entry.get("size_bytes")
        if not isinstance(size, int) or isinstance(size, bool) or size < 1:
            raise ManifestError(f"sealed artifact size is invalid: {filename}")
        digest = _validate_hash(
            entry.get("sha256"),
            f"sealed artifact {filename}.sha256",
            True,
        )
        candidate = (metadata_path.parent / filename).resolve()
        try:
            candidate.relative_to(root)
        except ValueError as exc:
            raise ManifestError(
                f"sealed artifact path is outside package: {filename}"
            ) from exc
        if not candidate.is_file():
            raise ManifestError(f"missing sealed artifact file: {candidate}")
        if candidate.stat().st_size != size:
            raise ManifestError(f"sealed artifact size mismatch: {filename}")
        if _sha256(candidate) != digest:
            raise ManifestError(f"sealed artifact sha256 mismatch: {filename}")
        by_filename[filename] = {
            "path": candidate.relative_to(root).as_posix(),
            "size_bytes": size,
            "sha256": digest,
        }
    if set(by_filename) != expected_filenames:
        raise ManifestError(
            f"sealed artifact kinds are incomplete: {metadata_path}"
        )
    return {
        "profile": expected_profile,
        "source_revision": source_revision,
        "build_id": build_id,
        "device_serial": device_serial,
        "artifacts": by_filename,
    }


def _validate_artifacts(
    artifacts: Any,
    root: Path,
    source_revision: str,
    for_release: bool,
) -> tuple[set[str], set[str]]:
    if not isinstance(artifacts, dict):
        raise ManifestError("artifacts must be an object")
    hashes: set[str] = set()
    build_ids: set[str] = set()
    for profile in PROFILE_NAMES:
        entry = artifacts.get(profile)
        if not isinstance(entry, dict):
            raise ManifestError(f"missing artifacts for {profile}")
        entry_revision = entry.get("source_revision")
        if not isinstance(entry_revision, str) or (
            entry_revision.lower() != source_revision
        ):
            raise ManifestError(f"{profile} artifact source_revision mismatch")
        has_paths = bool(entry.get("artifact_manifest"))
        artifact_entries = [entry.get(kind) for kind in ARTIFACT_KINDS]
        if not all(
            isinstance(artifact, dict) for artifact in artifact_entries
        ):
            raise ManifestError(f"{profile} artifact entries are incomplete")
        if not has_paths and not for_release and all(
            not artifact["path"] and not artifact.get("sha256", "")
            for artifact in artifact_entries
        ):
            continue
        metadata_relative = entry.get("artifact_manifest")
        if not isinstance(metadata_relative, str) or not metadata_relative:
            raise ManifestError(f"missing {profile} artifact manifest")
        metadata_path = _resolve_under(
            root,
            metadata_relative,
            f"{profile}.artifact_manifest",
        )
        if not metadata_path.is_file():
            raise ManifestError(
                f"missing artifact manifest: {metadata_relative}"
            )
        expected_profile = PROFILE_METADATA[profile][1]
        metadata = _read_sealed_profile_manifest(
            metadata_path,
            root,
            expected_profile,
            source_revision,
        )
        if entry.get("profile") != metadata["profile"]:
            raise ManifestError(f"{profile} profile metadata mismatch")
        if entry.get("build_id") != metadata["build_id"]:
            raise ManifestError(f"{profile} build_id metadata mismatch")
        build_ids.add(metadata["build_id"])
        for kind in ARTIFACT_KINDS:
            artifact = entry.get(kind)
            if not isinstance(artifact, dict):
                raise ManifestError(f"missing {profile} {kind} artifact")
            relative = artifact.get("path", "")
            digest = _validate_hash(
                artifact.get("sha256"),
                f"{profile}.{kind}.sha256",
                True,
            )
            size = artifact.get("size_bytes")
            if not isinstance(size, int) or isinstance(size, bool) or size < 1:
                raise ManifestError(f"{profile}.{kind}.size_bytes is invalid")
            path = _resolve_under(root, relative, f"{profile}.{kind}.path")
            metadata_artifact = metadata["artifacts"][ARTIFACT_FILENAMES[kind]]
            if relative != metadata_artifact["path"]:
                raise ManifestError(f"{profile}.{kind}.path metadata mismatch")
            if size != metadata_artifact["size_bytes"]:
                raise ManifestError(f"{profile}.{kind}.size metadata mismatch")
            if digest != metadata_artifact["sha256"]:
                raise ManifestError(
                    f"{profile}.{kind}.sha256 metadata mismatch"
                )
            if not path.is_file():
                raise ManifestError(f"missing artifact file: {relative}")
            if path.stat().st_size != size:
                raise ManifestError(f"{profile}.{kind} size mismatch")
            if _sha256(path) != digest:
                raise ManifestError(f"{profile}.{kind} sha256 mismatch")
            hashes.add(digest)
    return hashes, build_ids


def _validate_gate_artifact_bindings(
    rows: list[dict[str, Any]],
    artifact_hashes: set[str],
    build_ids: set[str],
    kind: str,
) -> None:
    for row in rows:
        case = row["case"]
        for digest in row["artifact_hashes"].values():
            if digest.lower() not in artifact_hashes:
                raise ManifestError(
                    f"{kind} gate {case} references an unsealed artifact"
                )
        build_id = row["build_id"]
        if build_id is not None and build_id not in build_ids:
            raise ManifestError(
                f"{kind} gate {case} references an unknown build_id"
            )


def _validate_result_file(
    root: Path,
    filename: str,
    rows: list[dict[str, Any]],
    kind: str,
) -> None:
    result_path = root / "results" / filename
    if not result_path.is_file():
        raise ManifestError(f"missing {kind} gate result file: {result_path}")
    decoded = _read_json(result_path, f"{kind} gate results")
    if decoded != rows:
        raise ManifestError(f"{filename} does not match manifest rows")


def validate_manifest(
    manifest_path: Path,
    *,
    expected_source_revision: str | None = None,
    for_release: bool = False,
) -> dict[str, Any]:
    """Validate a manifest and return its decoded object."""
    manifest_path = Path(manifest_path)
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read manifest: {manifest_path}") from exc
    if not isinstance(manifest, dict):
        raise ManifestError("manifest must be an object")
    if manifest.get("schema_version") != SCHEMA_VERSION:
        raise ManifestError("unsupported schema_version")
    if manifest.get("manifest_type") != MANIFEST_TYPE:
        raise ManifestError("unsupported manifest_type")
    source_revision = _validate_revision(
        manifest.get("source_revision"), "source_revision"
    )
    if expected_source_revision is not None:
        expected = _validate_revision(
            expected_source_revision,
            "expected_source_revision",
        )
        if source_revision != expected:
            raise ManifestError(
                "source_revision does not match current source"
            )
    if manifest.get("v1_tag_peel") != V1_TAG_PEEL:
        raise ManifestError("v1_tag_peel is not the frozen V1 tag peel")

    root = manifest_path.parent.resolve()
    if for_release:
        try:
            verify_sha256sums(root)
        except ValueError as exc:
            raise ManifestError(str(exc)) from exc
    _validate_gate_rows(
        manifest.get("hardware_gates"),
        HARDWARE_GATE_CASES,
        "hardware",
        root,
        source_revision,
        for_release,
    )
    _validate_gate_rows(
        manifest.get("software_gates"),
        SOFTWARE_GATE_CASES,
        "software",
        root,
        source_revision,
        for_release,
    )
    artifact_hashes, build_ids = _validate_artifacts(
        manifest.get("artifacts"), root, source_revision, for_release
    )
    software_rows = manifest["software_gates"]
    hardware_rows = manifest["hardware_gates"]
    _validate_gate_artifact_bindings(
        software_rows, artifact_hashes, build_ids, "software"
    )
    _validate_gate_artifact_bindings(
        hardware_rows, artifact_hashes, build_ids, "hardware"
    )
    _validate_result_file(
        root,
        "software-gates.json",
        software_rows,
        "software",
    )
    _validate_result_file(
        root,
        "hardware-gates.json",
        hardware_rows,
        "hardware",
    )
    return manifest


def _write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _load_sealed_artifacts(
    root: Path, artifact_root: Path, revision: str
) -> dict[str, Any]:
    """Bind the two required sealed profiles to a package manifest."""
    root = Path(root).resolve()
    artifact_root = Path(artifact_root).resolve()
    try:
        artifact_root.relative_to(root)
    except ValueError as exc:
        raise ManifestError(
            "artifact_root must be inside the evidence package"
        ) from exc

    artifacts: dict[str, Any] = {}
    for profile, (directory, expected_profile) in PROFILE_METADATA.items():
        profile_root = artifact_root / "firmware" / directory
        metadata_path = profile_root / "artifact-manifest.json"
        if not metadata_path.is_file():
            raise ManifestError(
                "missing sealed artifact manifest: "
                f"{metadata_path}"
            )
        metadata = _read_sealed_profile_manifest(
            metadata_path,
            root,
            expected_profile,
            revision,
        )
        profile_artifacts: dict[str, Any] = {
            "source_revision": revision,
            "profile": metadata["profile"],
            "build_id": metadata["build_id"],
            "artifact_manifest": metadata_path.relative_to(root).as_posix(),
        }
        by_filename = metadata["artifacts"]
        for kind, filename in ARTIFACT_FILENAMES.items():
            profile_artifacts[kind] = dict(by_filename[filename])
        artifacts[profile] = profile_artifacts
    return artifacts


def _write_hash_index(root: Path, revision: str) -> None:
    """Index sealed firmware metadata without inventing missing hashes."""
    profiles: dict[str, Any] = {}
    for directory in (
        "default-off",
        "debug-reliability",
        "fault-injection",
        "release-off",
    ):
        metadata_path = (
            root / "firmware" / directory / "artifact-manifest.json"
        )
        if metadata_path.is_file():
            profiles[directory] = json.loads(
                metadata_path.read_text(encoding="utf-8")
            )
        else:
            artifact_rows = []
            for filename in ARTIFACT_FILENAMES.values():
                path = root / "firmware" / directory / filename
                if path.is_file():
                    artifact_rows.append(
                        {
                            "path": filename,
                            "size_bytes": path.stat().st_size,
                            "sha256": _sha256(path),
                        }
                    )
            profiles[directory] = {
                "status": (
                    "SEALED" if artifact_rows else "NOT_EXECUTED"
                ),
                "source_revision": revision,
                "artifacts": artifact_rows,
            }
    _write_json(
        root / "firmware" / "hashes.json",
        {
            "schema_version": "phase4-firmware-hashes-v1",
            "source_revision": revision,
            "profiles": profiles,
        },
    )


def _dry_run_directories(root: Path) -> None:
    directories = [
        "firmware",
        "baseline/v1-tag",
        "baseline/old-client",
        "baseline/default-off",
        "faults/hardfault",
        "faults/memmanage",
        "faults/busfault",
        "faults/usagefault",
        "events/sequence-gap",
        "events/pretrigger-short",
        "events/duration-cap",
        "events/pool-pressure",
        "events/queue-pressure",
        "storage/u2",
        "storage/qspi-ota-model-sidecar",
        "reconnect",
        "results",
    ]
    for directory in directories:
        (root / directory).mkdir(parents=True, exist_ok=True)


def _dry_run_gate_row(
    case: str,
    kind: str,
    revision: str,
) -> dict[str, Any]:
    hardware = kind == "hardware"
    return {
        "schema_version": SCHEMA_VERSION,
        "case": case,
        "status": "NOT_EXECUTED",
        "evidence_level": "blocked_hardware" if hardware else "software",
        "source_revision": revision,
        "profile": None,
        "feature_switches": {
            "TRANSPORT_RELIABILITY_EVIDENCE_ENABLED": False,
            "TRANSPORT_FAULT_INJECTION_ENABLED": False,
        },
        "artifact_hashes": {},
        "build_id": None,
        "board_identity": {
            field: None for field in BOARD_IDENTITY_FIELDS
        },
        "timestamps": {
            "started_at_utc": None,
            "ended_at_utc": None,
            "timezone": "Asia/Shanghai",
        },
        "reset_source": None,
        "iteration_count": 0,
        "expected": {"status": "not_executed"},
        "actual": {"status": "not_executed"},
        "evidence_paths": [],
        "raw_evidence_paths": [],
        "evidence_digests": [],
        "failure_notes": (
            "Hardware evidence was not collected in the software-only gate."
            if hardware
            else "Software gate evidence is not populated in dry-run mode."
        ),
        "rollback_notes": "Reliability Release remains locked.",
        "reviewed_na": {
            "profile": "not executed",
            "artifact_hashes": "not executed",
            "build_id": "not executed",
            "board_identity": "not executed",
            "timestamps": "not executed",
            "reset_source": "not executed",
            "evidence_paths": "not executed",
            "raw_evidence_paths": "not executed",
            "evidence_digests": "not executed",
        },
    }


def _validate_software_result(
    payload: Any,
    case: str,
    manifest: dict[str, Any],
) -> tuple[dict[str, Any], str]:
    if not isinstance(payload, dict):
        raise ManifestError(f"software result is not an object: {case}")
    if set(payload) != set(SOFTWARE_RESULT_FIELDS):
        raise ManifestError(f"software result schema fields mismatch: {case}")
    if payload["schema_version"] != SOFTWARE_RESULT_SCHEMA:
        raise ManifestError(f"software result schema mismatch: {case}")
    if payload["case"] != case:
        raise ManifestError(f"software result case mismatch: {case}")

    expectation = SOFTWARE_GATE_EXPECTATIONS.get(case)
    if expectation is None:
        raise ManifestError(f"software result expectation is missing: {case}")
    if payload["expected_command"] != expectation["command"]:
        raise ManifestError(f"software result command mismatch: {case}")
    if payload["oracle"] != expectation["oracle"]:
        raise ManifestError(f"software result oracle mismatch: {case}")
    if payload["exit_code"] != 0 or isinstance(payload["exit_code"], bool):
        raise ManifestError(f"software result exit_code is not zero: {case}")
    if payload["status"] != "PASS" or payload["pass_marker"] != "PASS":
        raise ManifestError(f"software result is not PASS: {case}")

    profile_key = expectation["profile"]
    sealed = manifest["artifacts"].get(profile_key)
    if not isinstance(sealed, dict):
        raise ManifestError(f"software result profile is not sealed: {case}")
    expected_profile = PROFILE_METADATA[profile_key][1]
    if payload["profile"] != expected_profile:
        raise ManifestError(f"software result profile mismatch: {case}")
    build_id = sealed.get("build_id")
    if payload["build_id"] != build_id:
        raise ManifestError(f"software result build_id mismatch: {case}")

    artifact_hashes = payload["artifact_hashes"]
    if not isinstance(artifact_hashes, dict):
        raise ManifestError(f"software result artifact hashes invalid: {case}")
    if set(artifact_hashes) != set(ARTIFACT_KINDS):
        raise ManifestError(
            f"software result artifact hashes incomplete: {case}"
        )
    for kind in ARTIFACT_KINDS:
        digest = _validate_hash(
            artifact_hashes[kind],
            f"software result {case}.{kind}",
            True,
        )
        expected_digest = sealed[kind]["sha256"]
        if digest != expected_digest:
            raise ManifestError(
                f"software result artifact hash mismatch: {case}.{kind}"
            )
    return payload, profile_key


def populate_software_gates(
    package_root: Path,
    evidence_root: Path | None = None,
) -> Path:
    """Promote software gates from structured, packageable results."""
    root = Path(package_root).resolve()
    manifest_path = root / "manifest.json"
    manifest = validate_manifest(manifest_path)
    if not isinstance(manifest.get("artifacts"), dict):
        raise ManifestError("software evidence requires sealed artifacts")

    raw_root = root / "results" / "raw"
    source_root = (
        raw_root
        if evidence_root is None
        else Path(evidence_root).resolve()
    )
    timestamp = (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )
    rows: list[dict[str, Any]] = []
    for case in SOFTWARE_GATE_CASES:
        filename = f"software-{case}.json"
        source_path = source_root / filename
        if not source_path.is_file():
            raise ManifestError(f"missing software raw evidence: {filename}")
        payload = _read_json(source_path, f"software result {case}")
        result, profile_key = _validate_software_result(
            payload,
            case,
            manifest,
        )
        destination = raw_root / filename
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source_path.resolve() != destination.resolve():
            destination.write_bytes(source_path.read_bytes())
        relative = destination.relative_to(root).as_posix()
        digest = _sha256(destination)
        feature_enabled = profile_key == "fault_injection"
        rows.append(
            {
                "schema_version": SCHEMA_VERSION,
                "case": case,
                "status": "PASS",
                "evidence_level": "automated",
                "source_revision": manifest["source_revision"],
                "profile": result["profile"],
                "feature_switches": {
                    "TRANSPORT_RELIABILITY_EVIDENCE_ENABLED": feature_enabled,
                    "TRANSPORT_FAULT_INJECTION_ENABLED": feature_enabled,
                },
                "artifact_hashes": dict(result["artifact_hashes"]),
                "build_id": result["build_id"],
                "board_identity": {
                    field: None for field in BOARD_IDENTITY_FIELDS
                },
                "timestamps": {
                    "started_at_utc": timestamp,
                    "ended_at_utc": timestamp,
                    "timezone": "Asia/Shanghai",
                },
                "reset_source": "not_applicable_software_only",
                "iteration_count": 1,
                "expected": {
                    "case": case,
                    "command": result["expected_command"],
                    "oracle": result["oracle"],
                    "exit_code": 0,
                    "pass_marker": "PASS",
                },
                "actual": {
                    "case": case,
                    "status": result["status"],
                    "command": result["expected_command"],
                    "oracle": result["oracle"],
                    "exit_code": result["exit_code"],
                    "pass_marker": result["pass_marker"],
                    "profile": result["profile"],
                    "build_id": result["build_id"],
                },
                "evidence_paths": [relative],
                "raw_evidence_paths": [relative],
                "evidence_digests": [
                    {
                        "path": relative,
                        "size_bytes": destination.stat().st_size,
                        "sha256": digest,
                    }
                ],
                "failure_notes": "",
                "rollback_notes": (
                    "Keep the sealed default-off artifact as the "
                    "rollback path."
                ),
                "reviewed_na": {
                    "board_identity": (
                        "software-only gate; no board measurement"
                    )
                },
            }
        )

    manifest["software_gates"] = rows
    _write_json(manifest_path, manifest)
    _write_json(root / "results" / "software-gates.json", rows)
    _write_hash_index(root, manifest["source_revision"])
    generate_sha256sums(root)
    validate_manifest(manifest_path)
    return manifest_path


def create_dry_run(
    output: Path,
    *,
    source_revision: str,
    artifact_root: Path | None = None,
    software_evidence_root: Path | None = None,
) -> Path:
    """Create a non-release skeleton with all hardware gates blocked."""
    revision = _validate_revision(source_revision, "source_revision")
    root = Path(output).resolve()
    root.mkdir(parents=True, exist_ok=True)
    _dry_run_directories(root)
    artifacts = {}
    for profile in PROFILE_NAMES:
        artifacts[profile] = {
            "source_revision": revision,
            **{
                kind: {"path": "", "sha256": ""}
                for kind in ARTIFACT_KINDS
            },
        }
    software_gates = [
        _dry_run_gate_row(case, "software", revision)
        for case in SOFTWARE_GATE_CASES
    ]
    hardware_gates = [
        _dry_run_gate_row(case, "hardware", revision)
        for case in HARDWARE_GATE_CASES
    ]
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "manifest_type": MANIFEST_TYPE,
        "source_revision": revision,
        "v1_tag_peel": V1_TAG_PEEL,
        "artifacts": artifacts,
        "software_gates": software_gates,
        "hardware_gates": hardware_gates,
        "release": {
            "reliability_enabled": False,
            "status": "LOCKED",
        },
    }
    if artifact_root is not None:
        manifest["artifacts"] = _load_sealed_artifacts(
            root,
            artifact_root,
            revision,
        )
    _write_json(root / "manifest.json", manifest)
    _write_json(
        root / "metadata.json",
        {
            "schema_version": SCHEMA_VERSION,
            "mode": "dry-run",
            "source_revision": revision,
            "v1_tag_peel": V1_TAG_PEEL,
            "hardware_status": "BLOCKED_HARDWARE",
        },
    )
    _write_json(root / "results" / "software-gates.json", software_gates)
    _write_json(root / "results" / "hardware-gates.json", hardware_gates)
    _write_json(
        root / "results" / "resource-report.json",
        {
            "schema_version": SCHEMA_VERSION,
            "status": "NOT_EXECUTED",
            "evidence_level": "blocked_hardware",
            "source_revision": revision,
            "resources": {
                "heap_bytes": None,
                "stack_bytes": None,
                "dma_bytes": None,
                "rom_bytes": None,
                "ram_bytes": None,
            },
            "timing": {
                "fault_to_reset_us": None,
                "reconnect_ms": None,
                "startup_recovery_ms": None,
                "event_path_us": None,
            },
            "note": "No hardware measurements were taken in dry-run mode.",
        },
    )
    empty_hash = {"before_sha256": None, "after_sha256": None}
    _write_json(
        root / "storage" / "pre-post-hashes.json",
        {
            "schema_version": SCHEMA_VERSION,
            "status": "NOT_EXECUTED",
            "evidence_level": "blocked_hardware",
            "source_revision": revision,
            "u2": dict(empty_hash),
            "qspi": dict(empty_hash),
            "ota": dict(empty_hash),
            "model": dict(empty_hash),
            "sidecar": dict(empty_hash),
            "note": "No storage hashes were taken in dry-run mode.",
        },
    )
    _write_json(
        root / "baseline" / "v1-tag.json",
        {
            "schema_version": SCHEMA_VERSION,
            "status": "NOT_EXECUTED",
            "source_revision": revision,
            "v1_tag_peel": V1_TAG_PEEL,
            "note": "Immutable V1 tag baseline was not read in dry-run mode.",
        },
    )
    (root / "summary.md").write_text(
        "# Phase 4 software-preparation summary\n\n"
        "S0-S4 are software-only. H0-H5 remain "
        "`BLOCKED_HARDWARE/NOT_EXECUTED`.\n",
        encoding="utf-8",
    )
    (root / "operator-checklist.md").write_text(
        "# Phase 4 software-preparation dry run\n\n"
        "H0-H5 are BLOCKED_HARDWARE/NOT_EXECUTED.\n",
        encoding="utf-8",
    )
    from scripts.phase4_collector import build_dry_run_commands

    (root / "commands").mkdir(parents=True, exist_ok=True)
    (root / "commands" / "h0-runner.txt").write_text(
        build_dry_run_commands(root),
        encoding="utf-8",
    )
    _write_hash_index(root, revision)
    generate_sha256sums(root)
    if software_evidence_root is not None:
        populate_software_gates(root, software_evidence_root)
    return root / "manifest.json"


def _current_revision(repo_root: Path) -> str:
    completed = subprocess.run(
        ["git", "-C", str(repo_root), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    return _validate_revision(completed.stdout.strip(), "source_revision")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create")
    create.add_argument("--dry-run", action="store_true")
    create.add_argument("--output", type=Path, required=True)
    create.add_argument("--source-revision", default=None)
    create.add_argument("--artifact-root", type=Path, default=None)
    create.add_argument("--software-evidence-root", type=Path, default=None)

    verify = subparsers.add_parser("verify")
    verify.add_argument("--input", type=Path, required=True)
    verify.add_argument("--for-release", action="store_true")
    verify.add_argument("--source-revision", default=None)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "create":
            if not args.dry_run:
                raise ManifestError(
                    "create requires --dry-run during Phase 4 S0-S4"
                )
            revision = args.source_revision or _current_revision(
                Path(__file__).resolve().parents[1]
            )
            manifest_path = create_dry_run(
                args.output,
                source_revision=revision,
                artifact_root=args.artifact_root,
                software_evidence_root=args.software_evidence_root,
            )
            print(json.dumps({"created": str(manifest_path)}))
            return 0

        expected = args.source_revision
        manifest = validate_manifest(
            args.input / "manifest.json",
            expected_source_revision=expected,
            for_release=args.for_release,
        )
        print(
            json.dumps(
                {
                    "status": "PASS",
                    "for_release": args.for_release,
                    "source_revision": manifest["source_revision"],
                },
                sort_keys=True,
            )
        )
        return 0
    except (ManifestError, OSError, subprocess.SubprocessError) as exc:
        print(f"Phase 4 evidence: REJECTED: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
