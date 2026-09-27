from __future__ import annotations

from copy import deepcopy
import hashlib
from io import BytesIO
import json
from pathlib import Path
import subprocess
import tarfile

import pytest

from host.tools.v1_evidence_package import (
    CLASS_NAMES,
    DEFAULT_EVENT_IDS,
    EXPECTED_SOURCE_SUMMARY_SHA256,
    build_event_metadata,
    clear_generated_desktop_outputs,
    generate_package,
    load_source_summary,
    resolve_source_commits,
    tracked_evidence_inventory,
    validate_markdown_links,
    verify_package,
    write_checksums,
    write_manifest,
)


SOURCE_BACKUP = Path(r"C:\v1-hw-backup-20260825-001\full-qspi-event-backup")
PACKAGE_ROOT = Path(__file__).parents[2] / "evidence" / "releases" / "v1.0.0"


def _require_source_backup() -> None:
    if not SOURCE_BACKUP.is_dir():
        pytest.skip("V1 HIL source backup is not available in this checkout")


def test_default_desktop_selection_covers_three_observed_classes_and_planned_drops() -> None:
    _require_source_backup()
    assert DEFAULT_EVENT_IDS == (180, 181, 191, 195, 202)
    assert {
        int(load_source_summary(SOURCE_BACKUP)[0]["events"][event_id - 1]["ai_result"]["class_index"])
        for event_id in DEFAULT_EVENT_IDS
    } == {0, 1, 3}


def test_event_metadata_keeps_human_action_plan_separate_from_ai_prediction() -> None:
    _require_source_backup()
    summary, summary_hash = load_source_summary(SOURCE_BACKUP)
    entry = next(item for item in summary["events"] if item["event_id"] == 191)

    metadata = build_event_metadata(
        SOURCE_BACKUP / "event-191.terp-event",
        entry,
        source_summary_sha256=summary_hash,
    )

    assert summary_hash == EXPECTED_SOURCE_SUMMARY_SHA256
    assert metadata["human_label"]["value"] is None
    assert metadata["human_label"]["planned_action"] == "drop"
    assert metadata["human_label"]["verified_individual"] is False
    assert metadata["human_label"]["source"].startswith("evidence/")
    assert metadata["ai_prediction"]["class_name"] == "drop"
    assert metadata["ai_prediction"]["class_index"] == 3
    assert metadata["derived_from"]["summary"] == "full-qspi-event-backup/summary.json"
    assert metadata["derived_from"]["summary_sha256"] == summary_hash
    assert metadata["ai_prediction"]["source"]["summary_sha256"] == summary_hash
    assert metadata["decoded"]["sample_count"] == 2400
    assert metadata["decoded"]["lost_sample_count"] == 0
    assert metadata["decoded"]["subtrigger_count"] == 0
    assert metadata["crc"]["record_crc_verified"] is True
    assert metadata["label_semantics"]


@pytest.mark.parametrize(
    ("section", "field", "value"),
    [
        ("entry", "record_crc_verified_by_client", False),
        ("decoded", "sample_count", 2399),
        ("decoded", "lost_sample_count", 1),
        ("decoded", "subtrigger_count", 1),
        ("decoded", "posttrigger_samples", 1591),
        ("ai_result", "status", 0),
    ],
)
def test_event_metadata_rejects_non_acceptance_inputs(
    section: str, field: str, value: int | bool
) -> None:
    _require_source_backup()
    summary, summary_hash = load_source_summary(SOURCE_BACKUP)
    entry = deepcopy(next(item for item in summary["events"] if item["event_id"] == 180))
    target = entry if section == "entry" else entry[section]
    target[field] = value

    with pytest.raises(ValueError):
        build_event_metadata(
            SOURCE_BACKUP / "event-180.terp-event",
            entry,
            source_summary_sha256=summary_hash,
        )


def test_event_metadata_rejects_decoded_metadata_mismatch_even_when_count_is_2400() -> None:
    _require_source_backup()
    summary, summary_hash = load_source_summary(SOURCE_BACKUP)
    entry = deepcopy(next(item for item in summary["events"] if item["event_id"] == 180))
    entry["decoded"]["pretrigger_samples"] += 1
    entry["decoded"]["posttrigger_samples"] -= 1

    with pytest.raises(ValueError):
        build_event_metadata(
            SOURCE_BACKUP / "event-180.terp-event",
            entry,
            source_summary_sha256=summary_hash,
        )


def test_tag_commit_remains_source_when_main_advances(monkeypatch: pytest.MonkeyPatch) -> None:
    import host.tools.v1_evidence_package as package

    tag_commit = "a69b6c6c71b91e1267780c169760eca284b7523b"
    main_commit = "76d0de1d3a3b8f1796783d3ad0a370088a928d45"
    tag_object = "75d183e21bd99795ec5d487d0363d0bdc83aaf1e"

    def fake_git(_repo_root: Path, *args: str) -> str:
        if args == ("rev-parse", "refs/tags/v1.0.0"):
            return tag_object
        if args == ("rev-parse", "refs/tags/v1.0.0^{}"):
            return tag_commit
        if args == ("rev-parse", "main"):
            return main_commit
        if args == ("merge-base", "--is-ancestor", tag_commit, main_commit):
            return ""
        raise AssertionError(args)

    monkeypatch.setattr(package, "_git", fake_git)
    assert resolve_source_commits(Path("unused")) == (
        tag_commit,
        tag_commit,
        tag_object,
    )


def test_inventory_reads_source_commit_blobs_after_worktree_changes(tmp_path: Path) -> None:
    def run_git(*args: str) -> str:
        completed = subprocess.run(
            ["git", "-C", str(tmp_path), *args],
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        return completed.stdout.strip()

    run_git("init", "-b", "main")
    run_git("config", "user.email", "test@example.invalid")
    run_git("config", "user.name", "Evidence Test")
    source_file = tmp_path / "evidence" / "phase" / "record.txt"
    source_file.parent.mkdir(parents=True)
    original = b"tag-version\n"
    source_file.write_bytes(original)
    run_git("add", "evidence/phase/record.txt")
    run_git("commit", "-m", "source")
    source_commit = run_git("rev-parse", "HEAD")

    source_file.write_bytes(b"working-tree-version-is-different\n")
    rows = tracked_evidence_inventory(tmp_path, source_commit=source_commit)

    assert rows == [
        {
            "relative_path": "evidence/phase/record.txt",
            "size_bytes": len(original),
            "sha256": hashlib.sha256(original).hexdigest(),
            "file_type": "txt",
            "phase": "phase",
        }
    ]


def test_force_cleanup_removes_only_known_desktop_outputs(tmp_path: Path) -> None:
    desktop = tmp_path / "desktop"
    desktop.mkdir()
    for name in ("event-999.png", "event-999.json", "index.json"):
        (desktop / name).write_text("stale", encoding="utf-8")
    keep = desktop / "operator-note.txt"
    keep.write_text("keep", encoding="utf-8")

    clear_generated_desktop_outputs(tmp_path)

    assert not (desktop / "event-999.png").exists()
    assert not (desktop / "event-999.json").exists()
    assert not (desktop / "index.json").exists()
    assert keep.read_text(encoding="utf-8") == "keep"


def test_force_generation_does_not_publish_stale_event_files(tmp_path: Path) -> None:
    _require_source_backup()
    repo_root = Path(__file__).parents[2]
    output = tmp_path / "package"
    generate_package(repo_root, SOURCE_BACKUP, output, event_ids=(180,))
    (output / "desktop" / "event-999.png").write_bytes(b"stale")
    (output / "desktop" / "event-999.json").write_text("stale", encoding="utf-8")

    generate_package(repo_root, SOURCE_BACKUP, output, event_ids=(180,), overwrite=True)

    assert not (output / "desktop" / "event-999.png").exists()
    assert not (output / "desktop" / "event-999.json").exists()
    assert verify_package(output)["file_count"] == 16


def test_force_generation_rejects_unknown_existing_files(tmp_path: Path) -> None:
    _require_source_backup()
    output = tmp_path / "package"
    (output / "desktop").mkdir(parents=True)
    (output / "desktop" / "operator-note.txt").write_text("keep", encoding="utf-8")

    with pytest.raises(ValueError, match="unknown files"):
        generate_package(
            Path(__file__).parents[2],
            SOURCE_BACKUP,
            output,
            event_ids=(180,),
            overwrite=True,
        )


def test_force_generation_rejects_nested_desktop_outputs(tmp_path: Path) -> None:
    _require_source_backup()
    output = tmp_path / "package"
    nested = output / "desktop" / "nested"
    nested.mkdir(parents=True)
    (nested / "event-999.png").write_bytes(b"stale")

    with pytest.raises(ValueError, match="unknown files"):
        generate_package(
            Path(__file__).parents[2],
            SOURCE_BACKUP,
            output,
            event_ids=(180,),
            overwrite=True,
        )


def test_generation_rejects_symlink_output(tmp_path: Path) -> None:
    _require_source_backup()
    target = tmp_path / "target"
    target.mkdir()
    output = tmp_path / "package-link"
    try:
        output.symlink_to(target, target_is_directory=True)
    except (OSError, NotImplementedError) as exc:
        pytest.skip(f"directory symlinks unavailable: {exc}")

    with pytest.raises(ValueError, match="symlink/reparse"):
        generate_package(
            Path(__file__).parents[2],
            SOURCE_BACKUP,
            output,
            event_ids=(180,),
            overwrite=True,
        )


def test_git_archive_package_is_lf_normalized_and_verifiable(tmp_path: Path) -> None:
    archive = subprocess.run(
        ["git", "-C", str(Path(__file__).parents[2]), "archive", "HEAD", "evidence/releases/v1.0.0"],
        check=True,
        capture_output=True,
    )
    with tarfile.open(fileobj=BytesIO(archive.stdout), mode="r:") as tar:
        tar.extractall(tmp_path)
    archived_package = tmp_path / "evidence" / "releases" / "v1.0.0"
    assert verify_package(archived_package)["file_count"] == 24
    for path in archived_package.rglob("*"):
        if path.is_file() and path.suffix.lower() in {".md", ".json", ".csv", ".txt"}:
            assert b"\r" not in path.read_bytes(), path


def test_manifest_and_checksum_validation_cover_package_without_self_hash(tmp_path: Path) -> None:
    (tmp_path / "README.md").write_text("# Package\n\n[manifest](manifest.json)\n", encoding="utf-8")
    (tmp_path / "desktop").mkdir()
    (tmp_path / "desktop" / "event-001.json").write_text(
        json.dumps({"event_id": 1, "evidence_level": "derived"}) + "\n",
        encoding="utf-8",
    )

    write_manifest(
        tmp_path,
        source_commit="a69b6c6c71b91e1267780c169760eca284b7523b",
        tag_commit="a69b6c6c71b91e1267780c169760eca284b7523b",
        tag_object="tag-object",
    )
    write_checksums(tmp_path)
    counts = verify_package(tmp_path)

    manifest = json.loads((tmp_path / "manifest.json").read_text(encoding="utf-8"))
    covered = {entry["path"] for entry in manifest["package_files"]}
    assert "manifest.json" not in covered
    assert "SHA256SUMS.txt" not in covered
    assert counts["file_count"] == 4


def test_markdown_links_are_checked_relative_to_package(tmp_path: Path) -> None:
    (tmp_path / "README.md").write_text("[ok](docs.md)\n", encoding="utf-8")
    (tmp_path / "docs.md").write_text("# docs\n", encoding="utf-8")

    validate_markdown_links(tmp_path)


def test_replay_export_is_a_nonempty_png_for_a_real_hil_event(tmp_path: Path) -> None:
    _require_source_backup()
    from host.tools.v1_evidence_package import export_replay_png

    output = export_replay_png(
        SOURCE_BACKUP / "event-180.terp-event",
        tmp_path / "event-180.png",
    )

    assert output.stat().st_size > 0
    assert output.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"


def test_replay_export_can_refresh_a_generated_png(tmp_path: Path) -> None:
    _require_source_backup()
    from host.tools.v1_evidence_package import export_replay_png

    target = tmp_path / "event-180.png"
    export_replay_png(SOURCE_BACKUP / "event-180.terp-event", target)
    refreshed = export_replay_png(SOURCE_BACKUP / "event-180.terp-event", target)

    assert refreshed == target
    assert refreshed.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"


def test_replay_font_is_available_in_the_offscreen_qt_application() -> None:
    from PySide6.QtGui import QFontDatabase

    from host.tools.v1_evidence_package import choose_replay_font

    family = choose_replay_font()

    assert family in set(QFontDatabase.families())


def test_cli_module_exposes_explicit_generation_arguments() -> None:
    from scripts.generate_v1_evidence_package import build_parser

    args = build_parser().parse_args(
        [
            "--repo-root",
            ".",
            "--source-backup",
            str(SOURCE_BACKUP),
            "--output",
            "evidence/releases/v1.0.0",
            "--verify",
        ]
    )

    assert args.source_backup == SOURCE_BACKUP
    assert args.verify is True

    force_args = build_parser().parse_args(["--source-backup", str(SOURCE_BACKUP), "--force"])
    assert force_args.force is True


def test_cli_verify_does_not_require_source_backup(capsys: pytest.CaptureFixture[str]) -> None:
    from scripts.generate_v1_evidence_package import main

    assert main(["--verify", "--output", str(PACKAGE_ROOT)]) == 0
    result = json.loads(capsys.readouterr().out)

    assert result["action"] == "verified"
    assert result["file_count"] == 24


def test_cli_generation_still_requires_source_backup(tmp_path: Path) -> None:
    from scripts.generate_v1_evidence_package import main

    with pytest.raises(
        SystemExit,
        match="--source-backup is required when generating a package",
    ):
        main(["--output", str(tmp_path / "package")])


def test_generated_desktop_index_matches_png_and_metadata_contract() -> None:
    index = json.loads((PACKAGE_ROOT / "desktop" / "index.json").read_text(encoding="utf-8"))
    assert [item["event_id"] for item in index["events"]] == [180, 181, 191, 195, 202]

    for item in index["events"]:
        metadata = json.loads((PACKAGE_ROOT / "desktop" / item["metadata"]).read_text(encoding="utf-8"))
        png = PACKAGE_ROOT / "desktop" / item["png"]
        assert metadata["event_id"] == item["event_id"]
        assert metadata["derived_from"]["summary_sha256"] == EXPECTED_SOURCE_SUMMARY_SHA256
        assert len(metadata["source_raw"]["sha256"]) == 64
        assert metadata["ai_prediction"]["class_name"] in {"background", "impact", "drop"}
        assert metadata["decoded"]["sample_count"] == 2400
        assert metadata["decoded"]["lost_sample_count"] == 0
        assert metadata["decoded"]["subtrigger_count"] == 0
        assert metadata["crc"]["record_crc_verified"] is True
        assert metadata["human_label"]["value"] is None
        assert metadata["human_label"]["verified_individual"] is False
        assert item["planned_action"] in {None, "drop"}
        assert png.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
