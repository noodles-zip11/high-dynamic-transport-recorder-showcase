from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

import pytest
import yaml

from ai.src.split_dataset import (
    PARTITIONS,
    SplitRatios,
    build_split_manifest,
    write_split_manifest,
)
from ai.src.validate_dataset import EventValidation, ValidationReport
from ai.tests.test_validate_dataset import PROJECT_ROOT, _write_dataset


FOUR_CLASS_NAMES = (
    "background",
    "impact",
    "continuous_vibration",
    "drop",
)


def _event(session_index: int, label: str, event_index: int = 1) -> EventValidation:
    event_id = session_index * 100 + event_index
    return EventValidation(
        session_id=f"session_{session_index:02d}",
        event_id=event_id,
        source_path=f"event-{event_id}.terp-event",
        source_sha256=f"{event_id:064x}",
        label=label,
        eligible_for_training=True,
        format_version=3,
        sample_rate_hz=1600,
        sample_count=64,
        lost_sample_count=0,
        saturation_ratio=0.0,
    )


def _report(events: list[EventValidation]) -> ValidationReport:
    return ValidationReport(
        manifest_path=Path("manifest.yaml"),
        manifest_sha256="a" * 64,
        issues=(),
        events=tuple(events),
    )


def test_split_is_deterministic_stratified_and_keeps_sessions_together() -> None:
    events: list[EventValidation] = []
    labels = ("background", "impact")
    for label_index, label in enumerate(labels):
        for offset in range(5):
            session_index = label_index * 10 + offset + 1
            events.extend((_event(session_index, label, 1), _event(session_index, label, 2)))

    first = build_split_manifest(
        _report(events),
        seed=20260811,
        ratios=SplitRatios(train=0.6, validation=0.2, test=0.2),
    )
    second = build_split_manifest(
        _report(list(reversed(events))),
        seed=20260811,
        ratios=SplitRatios(train=0.6, validation=0.2, test=0.2),
    )

    assert first == second
    assert first["grouping"] == "session_id"
    assert first["source_manifest_sha256"] == "a" * 64
    session_partitions: dict[str, set[str]] = {}
    for partition, records in first["partitions"].items():
        for record in records:
            session_partitions.setdefault(record["session_id"], set()).add(partition)
    assert all(len(partitions) == 1 for partitions in session_partitions.values())
    for label in labels:
        assert first["class_session_counts"][label] == {
            "train": 3,
            "validation": 1,
            "test": 1,
        }


def test_split_rejects_mixed_labels_and_ignores_ineligible_events() -> None:
    mixed = [_event(1, "impact", 1), _event(1, "drop", 2)]
    ineligible = _event(2, "impact")
    ineligible = EventValidation(**{
        **ineligible.__dict__,
        "eligible_for_training": False,
    })

    with pytest.raises(ValueError, match="multiple labels"):
        build_split_manifest(_report(mixed), seed=1, ratios=SplitRatios(0.8, 0.1, 0.1))
    eligible_events = [_event(index, "impact") for index in range(3, 6)]
    split = build_split_manifest(
        _report([ineligible, *eligible_events]),
        seed=1,
        ratios=SplitRatios(0.8, 0.1, 0.1),
    )
    partitioned_ids = {
        record["event_id"]
        for records in split["partitions"].values()
        for record in records
    }
    assert partitioned_ids == {event.event_id for event in eligible_events}


def test_split_refuses_classes_without_one_session_per_partition() -> None:
    events = [_event(index, "impact") for index in range(1, 3)]

    with pytest.raises(ValueError, match="at least 3 eligible sessions"):
        build_split_manifest(
            _report(events),
            seed=1,
            ratios=SplitRatios(0.7, 0.15, 0.15),
        )


def test_session_split_applies_the_three_session_gate_to_configured_classes() -> None:
    events = [
        _event(index, "background") for index in range(1, 4)
    ] + [
        _event(index, "impact") for index in range(4, 7)
    ]

    with pytest.raises(ValueError, match="continuous_vibration"):
        build_split_manifest(
            _report(events),
            seed=1,
            ratios=SplitRatios(0.7, 0.15, 0.15),
            class_names=FOUR_CLASS_NAMES,
        )


def test_pilot_event_stratified_split_is_explicit_and_deterministic() -> None:
    events: list[EventValidation] = []
    for class_index, label in enumerate(FOUR_CLASS_NAMES, start=1):
        events.extend(
            _event(class_index, label, event_index)
            for event_index in range(1, 10)
        )

    manifest = build_split_manifest(
        _report(events),
        seed=20260823,
        ratios=SplitRatios(train=0.70, validation=0.15, test=0.15),
        grouping="pilot_event_stratified",
        class_names=FOUR_CLASS_NAMES,
    )

    assert manifest["grouping"] == "pilot_event_stratified"
    assert manifest["within_session_pilot"] is True
    assert "does not represent independent-session generalization" in manifest[
        "generalization_warning"
    ]
    class_event_counts = manifest["class_event_counts"]
    for label in FOUR_CLASS_NAMES:
        assert all(
            class_event_counts[label][partition] > 0
            for partition in PARTITIONS
        )
    identities = [
        (record["session_id"], record["event_id"])
        for records in manifest["partitions"].values()
        for record in records
    ]
    assert len(identities) == len(set(identities)) == len(events)

    assert manifest == build_split_manifest(
        _report(list(reversed(events))),
        seed=20260823,
        ratios=SplitRatios(train=0.70, validation=0.15, test=0.15),
        grouping="pilot_event_stratified",
        class_names=FOUR_CLASS_NAMES,
    )


def test_split_manifest_freezes_existing_assignment(tmp_path: Path) -> None:
    manifest = build_split_manifest(
        _report([_event(index, "impact") for index in range(1, 6)]),
        seed=7,
        ratios=SplitRatios(0.6, 0.2, 0.2),
    )
    output_path = tmp_path / "split.json"

    write_split_manifest(manifest, output_path)
    write_split_manifest(manifest, output_path)

    changed = dict(manifest)
    changed["seed"] = 8
    with pytest.raises(FileExistsError, match="frozen"):
        write_split_manifest(changed, output_path)


def test_cli_validates_and_writes_a_frozen_split(tmp_path: Path) -> None:
    config_path = _write_dataset(tmp_path)
    manifest_path = tmp_path / "manifest.yaml"
    manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
    template_session = manifest["sessions"][0]
    template_event = manifest["events"][0]
    template_source = (tmp_path / "raw" / template_event["source_path"]).read_bytes()
    for index, event_id in enumerate((43, 44), start=2):
        session = dict(template_session)
        session["session_id"] = f"20260811_0{index}"
        session["repetition"] = f"0{index}"
        source_bytes = bytearray(template_source)
        struct.pack_into("<I", source_bytes, 8, event_id)
        source_name = f"event-{event_id}.terp-event"
        (tmp_path / "raw" / source_name).write_bytes(source_bytes)
        event = dict(template_event)
        event.update({
            "session_id": session["session_id"],
            "event_id": event_id,
            "source_path": source_name,
            "source_sha256": hashlib.sha256(source_bytes).hexdigest(),
        })
        manifest["sessions"].append(session)
        manifest["events"].append(event)
    manifest_path.write_text(
        yaml.safe_dump(manifest, sort_keys=False),
        encoding="utf-8",
    )
    raw = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    raw["split"] = {
        "seed": 20260811,
        "ratios": {"train": 0.6, "validation": 0.2, "test": 0.2},
        "output_path": "split_manifest.json",
    }
    config_path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")

    completed = subprocess.run(
        [sys.executable, "-m", "ai.src.split_dataset", "--config", str(config_path)],
        cwd=PROJECT_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )

    assert completed.returncode == 0
    summary = json.loads(completed.stdout)
    output_path = tmp_path / "split_manifest.json"
    assert summary["output_path"] == str(output_path)
    assert json.loads(output_path.read_text(encoding="utf-8"))["grouping"] == "session_id"
