from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

import scripts.phase4_evidence as phase4_evidence
from scripts.phase4_evidence import (
    ARTIFACT_FILENAMES,
    HARDWARE_GATE_CASES,
    SOFTWARE_GATE_CASES,
    V1_TAG_PEEL,
    ManifestError,
    create_dry_run,
    generate_sha256sums,
    validate_manifest,
)


SOURCE_REVISION = "1" * 40
SOFTWARE_RESULT_SCHEMA = "reliability-evidence-software-result-v1"
SOFTWARE_RESULT_EXPECTATIONS = {
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
        "command": "python -m pytest scripts/tests/test_fault_injection_profile.py scripts/tests/test_fault_injection_absent.py scripts/tests/test_reliability_build_option.py -q",
        "oracle": "profile isolation and Release absence: PASS",
        "profile": "fault_injection",
    },
    "controlled_enablement": {
        "command": "python -m pytest scripts/tests/test_reliability_build_option.py scripts/tests/test_fault_injection_absent.py -q",
        "oracle": "controlled enablement: PASS",
        "profile": "fault_injection",
    },
    "resource_regression": {
        "command": "pwsh scripts/check_crash_fault_context.ps1",
        "oracle": "fault context and linker boundaries: PASS",
        "profile": "fault_injection",
    },
    "host_evidence": {
        "command": "python -m pytest scripts/tests/test_phase4_evidence.py scripts/tests/test_phase4_collector.py -q",
        "oracle": "manifest and collector evidence: PASS",
        "profile": "fault_injection",
    },
}


def _write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _complete_manifest(tmp_path: Path) -> Path:
    artifacts = {}
    profile_values = {
        "default_off": ("default-off", "Debug"),
        "fault_injection": ("fault-injection", "FaultInjection"),
    }
    for profile, (directory, profile_value) in profile_values.items():
        profile_dir = Path("firmware") / directory
        profile_artifacts = {
            "source_revision": SOURCE_REVISION,
            "profile": profile_value,
            "build_id": f"{profile_value.lower()}-build",
        }
        metadata_artifacts = []
        for kind, filename in ARTIFACT_FILENAMES.items():
            relative = (
                profile_dir / filename
            )
            content = f"{profile}:{kind}\n".encode("ascii")
            path = tmp_path / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
            profile_artifacts[kind] = {
                "path": relative.as_posix(),
                "size_bytes": len(content),
                "sha256": hashlib.sha256(content).hexdigest(),
            }
            metadata_artifacts.append(
                {
                    "path": filename,
                    "size_bytes": len(content),
                    "sha256": hashlib.sha256(content).hexdigest(),
                }
            )
        metadata_relative = profile_dir / "artifact-manifest.json"
        profile_artifacts["artifact_manifest"] = metadata_relative.as_posix()
        _write_json(
            tmp_path / metadata_relative,
            {
                "schema_version": "phase4-sealed-firmware-v1",
                "profile": profile_value,
                "source_revision": SOURCE_REVISION,
                "device_serial": "test-device",
                "build_id": profile_artifacts["build_id"],
                "artifacts": metadata_artifacts,
            },
        )
        artifacts[profile] = profile_artifacts

    artifact_hashes = {
        "default_off.bin": artifacts["default_off"]["bin"]["sha256"],
        "fault_injection.bin": artifacts["fault_injection"]["bin"][
            "sha256"
        ],
    }

    def gate_row(case: str, evidence_level: str) -> dict[str, object]:
        relative = Path("results") / "raw" / f"{evidence_level}-{case}.log"
        evidence = tmp_path / relative
        evidence.parent.mkdir(parents=True, exist_ok=True)
        evidence.write_text(
            f"verified case={case} level={evidence_level}\n",
            encoding="utf-8",
        )
        digest = hashlib.sha256(evidence.read_bytes()).hexdigest()
        return {
            "schema_version": "reliability-evidence-phase4-v1",
            "case": case,
            "status": "PASS",
            "evidence_level": evidence_level,
            "source_revision": SOURCE_REVISION,
            "profile": "FaultInjection",
            "feature_switches": {
                "TRANSPORT_RELIABILITY_EVIDENCE_ENABLED": True,
                "TRANSPORT_FAULT_INJECTION_ENABLED": True,
            },
            "artifact_hashes": artifact_hashes,
            "build_id": "faultinjection-build",
            "board_identity": {
                "board": "test-board",
                "pcb": "test-pcb",
                "mcu": "STM32H743",
                "probe": "test-probe",
                "uart": "test-uart",
                "media": "test-media",
            },
            "timestamps": {
                "started_at_utc": "2026-08-26T00:00:00Z",
                "ended_at_utc": "2026-08-26T00:00:01Z",
                "timezone": "Asia/Shanghai",
            },
            "reset_source": "software-test",
            "iteration_count": 1,
            "expected": {"status": "PASS"},
            "actual": {"status": "PASS"},
            "evidence_paths": [relative.as_posix()],
            "raw_evidence_paths": [relative.as_posix()],
            "evidence_digests": [
                {
                    "path": relative.as_posix(),
                    "size_bytes": evidence.stat().st_size,
                    "sha256": digest,
                }
            ],
            "failure_notes": "",
            "rollback_notes": "",
            "reviewed_na": {},
        }

    software_gates = []
    for case in SOFTWARE_GATE_CASES:
        software_gates.append(gate_row(case, "automated"))

    hardware_gates = []
    for case in HARDWARE_GATE_CASES:
        hardware_gates.append(gate_row(case, "physical_board"))

    manifest = {
        "schema_version": "reliability-evidence-phase4-v1",
        "manifest_type": "phase4_gate",
        "source_revision": SOURCE_REVISION,
        "v1_tag_peel": V1_TAG_PEEL,
        "artifacts": artifacts,
        "software_gates": software_gates,
        "hardware_gates": hardware_gates,
        "release": {"reliability_enabled": False, "status": "LOCKED"},
    }
    manifest_path = tmp_path / "manifest.json"
    _write_json(manifest_path, manifest)
    _write_json(tmp_path / "results" / "software-gates.json", software_gates)
    _write_json(tmp_path / "results" / "hardware-gates.json", hardware_gates)
    generate_sha256sums(tmp_path)
    return manifest_path


def test_dry_run_freezes_all_rows_and_is_not_release_eligible(
    tmp_path: Path,
) -> None:
    manifest_path = create_dry_run(tmp_path, source_revision=SOURCE_REVISION)

    manifest = validate_manifest(manifest_path)
    assert {row["case"] for row in manifest["software_gates"]} == set(
        SOFTWARE_GATE_CASES
    )
    assert {row["case"] for row in manifest["hardware_gates"]} == set(
        HARDWARE_GATE_CASES
    )
    assert all(
        row["status"] == "NOT_EXECUTED"
        and row["evidence_level"] == "blocked_hardware"
        for row in manifest["hardware_gates"]
    )

    with pytest.raises(ManifestError, match="physical_board/PASS"):
        validate_manifest(manifest_path, for_release=True)


def _write_structured_software_results(
    tmp_path: Path, manifest: dict[str, object]
) -> None:
    raw_root = tmp_path / "results" / "raw"
    artifacts = manifest["artifacts"]
    for case, expectation in SOFTWARE_RESULT_EXPECTATIONS.items():
        profile_key = expectation["profile"]
        profile_artifacts = artifacts[profile_key]
        _write_json(
            raw_root / f"software-{case}.json",
            {
                "schema_version": SOFTWARE_RESULT_SCHEMA,
                "case": case,
                "expected_command": expectation["command"],
                "oracle": expectation["oracle"],
                "exit_code": 0,
                "status": "PASS",
                "pass_marker": "PASS",
                "profile": profile_artifacts["profile"],
                "build_id": profile_artifacts["build_id"],
                "artifact_hashes": {
                    kind: profile_artifacts[kind]["sha256"]
                    for kind in (
                        "elf",
                        "bin",
                        "map",
                        "compile_commands",
                    )
                },
            },
        )


def test_software_gate_promotion_requires_structured_raw_evidence(
    tmp_path: Path,
) -> None:
    manifest_path = _complete_manifest(tmp_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    _write_structured_software_results(tmp_path, manifest)

    phase4_evidence.populate_software_gates(tmp_path)

    manifest = validate_manifest(manifest_path)
    assert len(manifest["software_gates"]) == len(SOFTWARE_GATE_CASES)
    assert all(
        row["status"] == "PASS"
        and row["raw_evidence_paths"]
        and row["evidence_digests"]
        for row in manifest["software_gates"]
    )
    assert json.loads(
        (tmp_path / "results" / "software-gates.json").read_text(
            encoding="utf-8"
        )
    ) == manifest["software_gates"]


@pytest.mark.parametrize(
    "mutation",
    ["wrong_case", "wrong_command", "wrong_profile", "placeholder"],
)
def test_software_gate_promotion_rejects_unbound_result(
    tmp_path: Path, mutation: str
) -> None:
    manifest_path = _complete_manifest(tmp_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    _write_structured_software_results(tmp_path, manifest)
    result_path = tmp_path / "results" / "raw" / "software-v1_baseline.json"
    if mutation == "placeholder":
        result_path.write_text(
            "exit_code=0\nPASS\n", encoding="utf-8"
        )
    else:
        result = json.loads(result_path.read_text(encoding="utf-8"))
        if mutation == "wrong_case":
            result["case"] = "memory_layout"
        elif mutation == "wrong_command":
            result["expected_command"] = "wrong command"
        else:
            result["profile"] = "FaultInjection"
        _write_json(result_path, result)

    with pytest.raises(ManifestError, match="software result|software raw"):
        phase4_evidence.populate_software_gates(tmp_path)


def test_complete_physical_manifest_and_hashes_validate(
    tmp_path: Path,
) -> None:
    manifest_path = _complete_manifest(tmp_path)

    validate_manifest(
        manifest_path,
        for_release=True,
        expected_source_revision=SOURCE_REVISION,
    )


def test_missing_gate_or_evidence_is_rejected(tmp_path: Path) -> None:
    manifest_path = _complete_manifest(tmp_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    manifest["software_gates"].pop()
    _write_json(manifest_path, manifest)
    with pytest.raises(ManifestError, match="software gate cases"):
        validate_manifest(manifest_path)

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    missing_case = SOFTWARE_GATE_CASES[-1]
    manifest["software_gates"].append(
        {
            "case": missing_case,
            "status": "PASS",
            "evidence_level": "automated",
            "source_revision": SOURCE_REVISION,
            "evidence_paths": [],
        }
    )
    _write_json(manifest_path, manifest)
    with pytest.raises(ManifestError, match="schema_version|evidence_paths"):
        validate_manifest(manifest_path)


def test_non_physical_hardware_pass_is_rejected(tmp_path: Path) -> None:
    manifest_path = _complete_manifest(tmp_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["hardware_gates"][0]["evidence_level"] = "native"
    _write_json(manifest_path, manifest)

    with pytest.raises(ManifestError, match="physical_board|evidence_level"):
        validate_manifest(manifest_path)


def test_revision_mismatch_is_rejected(tmp_path: Path) -> None:
    manifest_path = _complete_manifest(tmp_path)

    with pytest.raises(ManifestError, match="source_revision"):
        validate_manifest(
            manifest_path,
            expected_source_revision="2" * 40,
        )


def test_duplicate_case_is_rejected(tmp_path: Path) -> None:
    manifest_path = _complete_manifest(tmp_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["software_gates"].append(manifest["software_gates"][0])
    _write_json(manifest_path, manifest)

    with pytest.raises(ManifestError, match="duplicate"):
        validate_manifest(manifest_path)


def test_path_escape_is_rejected(tmp_path: Path) -> None:
    manifest_path = _complete_manifest(tmp_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["software_gates"][0]["evidence_paths"] = ["../outside.json"]
    manifest["software_gates"][0]["raw_evidence_paths"] = [
        "../outside.json"
    ]
    _write_json(manifest_path, manifest)

    with pytest.raises(ManifestError, match="outside manifest root"):
        validate_manifest(manifest_path)


def test_hash_mismatch_is_rejected(tmp_path: Path) -> None:
    manifest_path = _complete_manifest(tmp_path)
    artifact = (
        tmp_path
        / "firmware"
        / "default-off"
        / "transport_recorder.elf"
    )
    artifact.write_bytes(b"changed\n")

    with pytest.raises(ManifestError, match="sha256 mismatch"):
        validate_manifest(manifest_path, for_release=True)


def test_v1_tag_peel_is_frozen(tmp_path: Path) -> None:
    manifest_path = _complete_manifest(tmp_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["v1_tag_peel"] = "2" * 40
    _write_json(manifest_path, manifest)

    with pytest.raises(ManifestError, match="v1_tag_peel"):
        validate_manifest(manifest_path)


def test_dry_run_can_bind_sealed_profile_artifacts(tmp_path: Path) -> None:
    for profile in ("default-off", "fault-injection"):
        profile_value = (
            "Debug" if profile == "default-off" else "FaultInjection"
        )
        profile_dir = tmp_path / "firmware" / profile
        metadata_artifacts = []
        for kind in ("elf", "bin", "map", "compile_commands"):
            filename = (
                "compile_commands.json"
                if kind == "compile_commands"
                else f"transport_recorder.{kind}"
            )
            path = profile_dir / filename
            path.parent.mkdir(parents=True, exist_ok=True)
            content = f"{profile}:{kind}".encode("ascii")
            path.write_bytes(content)
            metadata_artifacts.append(
                {
                    "path": filename,
                    "size_bytes": len(content),
                    "sha256": hashlib.sha256(content).hexdigest(),
                }
            )
        _write_json(
            profile_dir / "artifact-manifest.json",
            {
                "schema_version": "phase4-sealed-firmware-v1",
                "profile": profile_value,
                "source_revision": SOURCE_REVISION,
                "device_serial": "test-device",
                "build_id": f"{profile_value.lower()}-build",
                "artifacts": metadata_artifacts,
            },
        )

    manifest_path = create_dry_run(
        tmp_path,
        source_revision=SOURCE_REVISION,
        artifact_root=tmp_path,
    )
    manifest = validate_manifest(manifest_path)

    for profile in ("default_off", "fault_injection"):
        assert manifest["artifacts"][profile]["source_revision"] == (
            SOURCE_REVISION
        )
        assert all(
            artifact["path"] and artifact["sha256"]
            for kind, artifact in manifest["artifacts"][profile].items()
            if kind not in {
                "source_revision",
                "profile",
                "build_id",
                "artifact_manifest",
            }
        )

    hashes = json.loads(
        (tmp_path / "firmware" / "hashes.json").read_text(
            encoding="utf-8"
        )
    )
    assert hashes["source_revision"] == SOURCE_REVISION
    assert hashes["profiles"]["fault-injection"]["artifacts"]


def test_pass_gate_rejects_arbitrary_existing_file_as_evidence(
    tmp_path: Path,
) -> None:
    manifest_path = _complete_manifest(tmp_path)
    arbitrary = tmp_path / "results" / "arbitrary.txt"
    arbitrary.write_text("not a raw gate log\n", encoding="utf-8")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["software_gates"][0]["evidence_paths"] = [
        "results/arbitrary.txt"
    ]
    manifest["software_gates"][0]["raw_evidence_paths"] = [
        "results/arbitrary.txt"
    ]
    manifest["software_gates"][0]["evidence_digests"] = [
        {
            "path": "results/arbitrary.txt",
            "size_bytes": arbitrary.stat().st_size,
            "sha256": hashlib.sha256(arbitrary.read_bytes()).hexdigest(),
        }
    ]
    _write_json(manifest_path, manifest)

    with pytest.raises(ManifestError, match="raw evidence"):
        validate_manifest(manifest_path)


def test_gate_result_files_must_match_manifest_rows(tmp_path: Path) -> None:
    manifest_path = _complete_manifest(tmp_path)
    result_path = tmp_path / "results" / "software-gates.json"
    rows = json.loads(result_path.read_text(encoding="utf-8"))
    rows[0]["actual"] = {"status": "FAIL"}
    _write_json(result_path, rows)

    with pytest.raises(ManifestError, match="software-gates.json"):
        validate_manifest(manifest_path)


def test_release_validation_verifies_package_checksums(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    manifest_path = _complete_manifest(tmp_path)
    called = False

    def verify(root: Path) -> dict[str, str]:
        nonlocal called
        called = True
        return {"manifest.json": "0" * 64}

    monkeypatch.setattr(phase4_evidence, "verify_sha256sums", verify)
    validate_manifest(manifest_path, for_release=True)
    assert called


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("profile", "Debug"),
        ("source_revision", "2" * 40),
        ("size_bytes", 999999),
        ("sha256", "0" * 64),
    ],
)
def test_sealed_artifact_metadata_provenance_is_verified(
    tmp_path: Path, field: str, value: object
) -> None:
    manifest_path = _complete_manifest(tmp_path)
    metadata_path = (
        tmp_path / "firmware" / "fault-injection" / "artifact-manifest.json"
    )
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    if field in {"profile", "source_revision"}:
        metadata[field] = value
    else:
        metadata["artifacts"][0][field] = value
    _write_json(metadata_path, metadata)

    with pytest.raises(ManifestError):
        validate_manifest(manifest_path, for_release=True)


def test_stale_sha256sums_rejects_release_manifest(tmp_path: Path) -> None:
    manifest_path = _complete_manifest(tmp_path)
    checksums = tmp_path / "SHA256SUMS.txt"
    lines = checksums.read_text(encoding="utf-8").splitlines()
    checksums.write_text(
        "\n".join(
            ("0" * 64 + line[64:]) if line.endswith("manifest.json") else line
            for line in lines
        )
        + "\n",
        encoding="utf-8",
    )

    with pytest.raises((ManifestError, ValueError), match="sha256"):
        validate_manifest(manifest_path, for_release=True)
