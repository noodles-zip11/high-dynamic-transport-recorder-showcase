from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]


def test_build_wrapper_seals_each_profile_to_an_external_directory() -> None:
    script = (PROJECT_ROOT / "scripts" / "build_firmware.ps1").read_text(
        encoding="utf-8-sig"
    )

    assert "ArtifactOutputDirectory" in script
    assert "TRANSPORT_RECORDER_EVIDENCE_ROOT" not in script
    assert "Get-FileHash" in script
    assert "compile_commands.json" in script
    assert "Set-ItemProperty" in script or "chmod" in script
    assert "Sealed firmware artifacts" in script


def test_artifact_seal_rejects_a_worktree_output_directory() -> None:
    script = (PROJECT_ROOT / "scripts" / "build_firmware.ps1").read_text(
        encoding="utf-8-sig"
    )

    assert "ProjectRoot" in script
    assert "outside" in script.lower()
    assert "ArtifactOutputDirectory" in script
