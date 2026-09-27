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
    with pytest.raises(
        RuntimeError,
        match="reliability_evidence must be '0' or '1'",
    ):
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


def test_sconstruct_uses_the_shared_reliability_policy() -> None:
    sconstruct = (FIRMWARE_ROOT / "SConstruct").read_text(encoding="utf-8")

    assert (
        "from build_options import resolve_reliability_evidence_enabled"
        in sconstruct
    )
    assert "RELIABILITY_EVIDENCE_ENABLED =" in sconstruct
    assert "resolve_reliability_evidence_enabled(" in sconstruct
    assert "ARGUMENTS, BUILD_PROFILE" in sconstruct
    assert (
        'env.Append(CPPDEFINES=["TRANSPORT_RELIABILITY_EVIDENCE_ENABLED"])'
        in sconstruct
    )


def test_powershell_wrappers_forward_reliability_opt_in() -> None:
    build_script = (PROJECT_ROOT / "scripts" / "build_firmware.ps1").read_text(
        encoding="utf-8-sig"
    )
    test_script = (PROJECT_ROOT / "scripts" / "run_tests.ps1").read_text(
        encoding="utf-8-sig"
    )

    assert "[switch]$ReliabilityEvidence" in build_script
    assert (
        "$sconsArguments = @('-C', $firmwareDir, '-j4', '-Q')"
        in build_script
    )
    assert "$sconsArguments += 'reliability_evidence=1'" in build_script
    assert (
        "Release reliability evidence remains locked until Phase 4"
        in build_script
    )
    assert "[switch]$ReliabilityEvidence" in test_script
    assert "ReliabilityEvidence = $ReliabilityEvidence" in test_script
