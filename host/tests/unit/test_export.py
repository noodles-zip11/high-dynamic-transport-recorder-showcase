from __future__ import annotations

import csv
from hashlib import sha256
import json
from pathlib import Path

import pytest

from host.transport_recorder.analysis.event_record import load_event
from host.transport_recorder.analysis.export import (
    export_csv,
    export_json_summary,
    export_raw,
)
from host.transport_recorder.protocol.client import AiResult


FIXTURE_EVENTS = Path(__file__).parents[1] / "fixtures" / "events"


def test_csv_round_trip_preserves_sample_count_endpoints_and_peak(tmp_path: Path) -> None:
    event = load_event(FIXTURE_EVENTS / "valid-42.terp-event")

    output = export_csv(event, tmp_path / "event.csv")

    with output.open(newline="", encoding="utf-8") as exported:
        rows = list(csv.DictReader(line for line in exported if not line.startswith("#")))
    assert len(rows) == event.sample_count
    assert int(rows[0]["time_us"]) == int(event.timestamps_us[0])
    assert int(rows[-1]["time_us"]) == int(event.timestamps_us[-1])
    assert max(int(row["accel_x_count"]) for row in rows) == 97


def test_json_export_keeps_event_validation_and_annotations(tmp_path: Path) -> None:
    event = load_event(FIXTURE_EVENTS / "valid-42.terp-event")

    output = export_json_summary(
        event,
        tmp_path / "event.json",
        validation_state="valid",
        label="impact",
        note="right-side collision",
    )

    exported = json.loads(output.read_text(encoding="utf-8"))
    assert exported["event"]["event_id"] == 42
    assert exported["validation_state"] == "valid"
    assert exported["annotation"] == {"label": "impact", "note": "right-side collision"}


def test_json_export_keeps_model_result_separate_from_annotation(tmp_path: Path) -> None:
    event = load_event(FIXTURE_EVENTS / "valid-42.terp-event")
    result = AiResult(
        event_id=42,
        model_version=7,
        status=1,
        class_index=2,
        class_count=4,
        quality_flags=0x03,
        event_flags=0x01,
        sample_count=event.sample_count,
        model_crc32=0x12345678,
        confidence=0.875,
        logits=(0.1, 0.2, 0.3, 0.4),
        failure_reason=0,
        result_sequence=9,
    )

    output = export_json_summary(
        event,
        tmp_path / "event.json",
        validation_state="valid",
        label="impact",
        note="right-side collision",
        ai_result=result,
    )

    exported = json.loads(output.read_text(encoding="utf-8"))
    assert exported["model_result"]["event_id"] == 42
    assert exported["model_result"]["class_index"] == 2
    assert exported["model_result"]["logits"] == [0.1, 0.2, 0.3, 0.4]
    assert exported["annotation"] == {"label": "impact", "note": "right-side collision"}


def test_raw_export_is_byte_identical_and_writes_sha256(tmp_path: Path) -> None:
    source = FIXTURE_EVENTS / "valid-42.terp-event"

    output, digest_path = export_raw(source, tmp_path / "raw" / "event.terp-event")

    assert output.read_bytes() == source.read_bytes()
    assert digest_path.read_text(encoding="utf-8").strip() == sha256(output.read_bytes()).hexdigest()


def test_exports_do_not_overwrite_existing_files(tmp_path: Path) -> None:
    event = load_event(FIXTURE_EVENTS / "valid-42.terp-event")
    target = tmp_path / "event.csv"
    export_csv(event, target)

    with pytest.raises(FileExistsError, match="already exists"):
        export_csv(event, target)
