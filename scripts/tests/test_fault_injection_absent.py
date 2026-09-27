from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys

import pytest


PROJECT_ROOT = Path(__file__).resolve().parents[2]
FIRMWARE_ROOT = PROJECT_ROOT / "firmware"
CHECKER = PROJECT_ROOT / "scripts" / "check_fault_injection_absent.ps1"
sys.path.insert(0, str(FIRMWARE_ROOT))


def _fixture_paths(tmp_path: Path) -> tuple[Path, Path, Path, Path]:
    elf = tmp_path / "release.elf"
    map_file = tmp_path / "release.map"
    compile_commands = tmp_path / "compile_commands.json"
    fake_tool = tmp_path / "empty-tool.cmd"
    elf.write_bytes(b"ELF")
    map_file.write_text("release map\n", encoding="utf-8")
    compile_commands.write_text(
        json.dumps([{"command": "arm-none-eabi-gcc -c app.c"}]),
        encoding="utf-8",
    )
    fake_tool.write_text("@echo off\r\nexit /b 0\r\n", encoding="ascii")
    return elf, map_file, compile_commands, fake_tool


def _run_checker(
    elf: Path,
    map_file: Path,
    compile_commands: Path,
    fake_tool: Path,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "pwsh",
            "-NoProfile",
            "-File",
            str(CHECKER),
            "-ElfPath",
            str(elf),
            "-MapPath",
            str(map_file),
            "-CompileCommandsPath",
            str(compile_commands),
            "-ObjdumpPath",
            str(fake_tool),
            "-StringsPath",
            str(fake_tool),
        ],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )


def test_absence_checker_accepts_clean_synthetic_artifacts(
    tmp_path: Path,
) -> None:
    paths = _fixture_paths(tmp_path)

    result = _run_checker(*paths)

    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout


@pytest.mark.parametrize(
    "forbidden",
    [
        "TRANSPORT_FAULT_INJECTION_ENABLED",
        "fault_injection.c",
        "reliability_inject",
        "CONFIRM_RESET",
        "hardfault",
    ],
)
def test_absence_checker_rejects_synthetic_injection_tokens(
    tmp_path: Path, forbidden: str
) -> None:
    elf, map_file, compile_commands, fake_tool = _fixture_paths(tmp_path)
    map_file.write_text(forbidden + "\n", encoding="utf-8")

    result = _run_checker(elf, map_file, compile_commands, fake_tool)

    assert result.returncode != 0
    assert forbidden in result.stdout + result.stderr


def test_release_policy_requires_a_validated_manifest() -> None:
    from build_options import resolve_reliability_evidence_enabled

    with pytest.raises(RuntimeError, match="Phase 4 manifest"):
        resolve_reliability_evidence_enabled(
            {"reliability_evidence": "1"},
            "release",
            release_manifest_valid=False,
        )
    assert resolve_reliability_evidence_enabled(
        {"reliability_evidence": "1"},
        "release",
        release_manifest_valid=True,
    )


def test_release_policy_is_wired_to_external_manifest() -> None:
    sconstruct = (FIRMWARE_ROOT / "SConstruct").read_text(encoding="utf-8")
    build_script = (
        PROJECT_ROOT / "scripts" / "build_firmware.ps1"
    ).read_text(encoding="utf-8-sig")
    release_gate = (
        PROJECT_ROOT / "scripts" / "release_gate.ps1"
    ).read_text(encoding="utf-8")

    assert "TRANSPORT_RELIABILITY_EVIDENCE_MANIFEST" in sconstruct
    assert "validate_manifest" in sconstruct
    assert "TRANSPORT_RELIABILITY_EVIDENCE_MANIFEST" in build_script
    assert "check_fault_injection_absent.ps1" in release_gate


def test_release_manifest_path_is_checked_absolute_before_resolution() -> None:
    build_script = (
        PROJECT_ROOT / "scripts" / "build_firmware.ps1"
    ).read_text(encoding="utf-8-sig")
    get_full_path = build_script.index("[System.IO.Path]::GetFullPath(")
    absolute_check = build_script.index(
        "[System.IO.Path]::IsPathFullyQualified("
    )

    assert absolute_check < get_full_path


def test_direct_release_checks_head_and_clean_tree() -> None:
    sconstruct = (FIRMWARE_ROOT / "SConstruct").read_text(encoding="utf-8")

    assert "git" in sconstruct
    assert "rev-parse" in sconstruct
    assert "status" in sconstruct
    assert "--porcelain" in sconstruct
    assert (
        "current HEAD" in sconstruct or "working tree" in sconstruct
    )


def test_direct_release_uses_scons_root_for_git_checks() -> None:
    sconstruct = (FIRMWARE_ROOT / "SConstruct").read_text(encoding="utf-8")

    assert "Dir('#').abspath" in sconstruct
    assert "Path(__file__)" not in sconstruct
    assert "os.path.dirname(__file__)" not in sconstruct


def test_wrapper_release_checks_head_and_clean_tree() -> None:
    build_script = (
        PROJECT_ROOT / "scripts" / "build_firmware.ps1"
    ).read_text(encoding="utf-8-sig")

    assert "git -C $ProjectRoot" in build_script
    assert "rev-parse" in build_script
    assert "status --porcelain" in build_script
    assert "clean working tree" in build_script


def test_release_gate_accepts_formal_manifest_path() -> None:
    release_gate = (
        PROJECT_ROOT / "scripts" / "release_gate.ps1"
    ).read_text(encoding="utf-8")
    run_tests = (
        PROJECT_ROOT / "scripts" / "run_tests.ps1"
    ).read_text(encoding="utf-8")

    assert "ReliabilityEvidenceManifest" in release_gate
    assert "ReliabilityEvidenceManifest" in run_tests


def test_release_gate_splats_named_run_test_arguments() -> None:
    release_gate = (
        PROJECT_ROOT / "scripts" / "release_gate.ps1"
    ).read_text(encoding="utf-8")

    assert "$testArguments = @{" in release_gate
    assert "WithFirmware = $true" in release_gate
    assert "FirmwareProfile = 'Release'" in release_gate
    assert "FirmwareRevision = $resolvedGitRevision" in release_gate
    assert "FirmwareSerial = $DeviceSerial" in release_gate
    assert "$testArguments['ReliabilityEvidence'] = $true" in release_gate
    assert (
        "$testArguments['ReliabilityEvidenceManifest'] = "
        "$ReliabilityEvidenceManifest"
    ) in release_gate


def test_run_tests_splats_named_firmware_build_arguments() -> None:
    run_tests = (
        PROJECT_ROOT / "scripts" / "run_tests.ps1"
    ).read_text(encoding="utf-8")

    assert "$firmwareArguments = @{" in run_tests
    assert "BuildProfile = $FirmwareProfile" in run_tests
    assert "GitRevision = $FirmwareRevision" in run_tests
    assert "DeviceSerial = $FirmwareSerial" in run_tests
    assert "ReliabilityEvidence = $ReliabilityEvidence" in run_tests
    assert "RequireElf = $true" in run_tests
    assert (
        "$firmwareArguments['ReliabilityEvidenceManifest'] = "
        "$ReliabilityEvidenceManifest"
    ) in run_tests


def test_absence_checker_defaults_to_current_release_artifacts() -> None:
    text = CHECKER.read_text(encoding="utf-8-sig")

    assert "if (-not $ElfPath)" in text
    assert "firmware\\build\\transport_recorder.elf" in text
    assert "firmware\\build\\transport_recorder.map" in text
    assert "firmware\\compile_commands.json" in text
