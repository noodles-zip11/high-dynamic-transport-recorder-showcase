from __future__ import annotations

import sqlite3
from pathlib import Path

import pytest

from host.transport_recorder.repository.event_repository import (
    EventRepository,
    ValidationState,
)
from host.transport_recorder.protocol.client import AiResult


FIXTURE_EVENTS = Path(__file__).parents[1] / "fixtures" / "events"


def test_import_validated_event_places_bytes_before_committing_index(tmp_path: Path) -> None:
    repository = EventRepository(tmp_path)

    record = repository.import_event(
        FIXTURE_EVENTS / "valid-42.terp-event", device_serial="SIM-0001"
    )

    assert record.validation_state is ValidationState.VALID
    assert record.path.read_bytes() == (FIXTURE_EVENTS / "valid-42.terp-event").read_bytes()
    assert record.path == tmp_path / "events" / "SIM-0001" / "42.terp-event"
    assert repository.list_events() == [record]


def test_failed_index_transaction_leaves_no_valid_record(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    repository = EventRepository(tmp_path)

    def fail_insert(*args: object) -> None:
        raise sqlite3.Error("injected database failure")

    monkeypatch.setattr(repository, "_insert_record", fail_insert)

    with pytest.raises(sqlite3.Error, match="injected database failure"):
        repository.import_event(
            FIXTURE_EVENTS / "valid-42.terp-event", device_serial="SIM-0001"
        )

    assert repository.list_events() == []
    assert not (tmp_path / "events" / "SIM-0001" / "42.terp-event").exists()


def test_missing_raw_file_is_reported_without_deleting_annotation(tmp_path: Path) -> None:
    repository = EventRepository(tmp_path)
    record = repository.import_event(
        FIXTURE_EVENTS / "valid-42.terp-event", device_serial="SIM-0001"
    )
    repository.update_annotation(record.key, label="impact", note="right-side collision")
    record.path.unlink()

    missing = repository.get_event(record.key)

    assert missing.validation_state is ValidationState.MISSING
    assert missing.label == "impact"
    assert missing.note == "right-side collision"


def test_event_can_be_resolved_from_its_repository_path(tmp_path: Path) -> None:
    repository = EventRepository(tmp_path)
    record = repository.import_event(
        FIXTURE_EVENTS / "valid-42.terp-event", device_serial="SIM-0001"
    )

    assert repository.find_event_by_path(record.path) == record
    assert repository.find_event_by_path(tmp_path / "outside.terp-event") is None


def test_model_result_is_persisted_separately_from_human_annotation(tmp_path: Path) -> None:
    repository = EventRepository(tmp_path)
    record = repository.import_event(
        FIXTURE_EVENTS / "valid-42.terp-event", device_serial="SIM-0001"
    )
    result = AiResult(
        event_id=42,
        model_version=2,
        status=1,
        class_index=1,
        class_count=2,
        quality_flags=3,
        event_flags=8,
        sample_count=2400,
        model_crc32=0xAABBCCDD,
        confidence=0.875,
        logits=(-1.0, 1.0, 0.0, 0.0),
        failure_reason=0,
        result_sequence=9,
    )

    repository.update_ai_result(record.key, result)
    restored = repository.get_event(record.key)

    assert restored.ai_result == result
    assert restored.label is None
    assert restored.note is None


def test_schema_v1_is_upgraded_before_model_results_are_used(tmp_path: Path) -> None:
    repository = EventRepository(tmp_path)
    record = repository.import_event(
        FIXTURE_EVENTS / "valid-42.terp-event", device_serial="SIM-0001"
    )
    with sqlite3.connect(tmp_path / "recorder.db") as connection:
        connection.execute("DROP TABLE ai_results")
        connection.execute("UPDATE schema_version SET version = 1")

    migrated = EventRepository(tmp_path)

    assert migrated.get_event(record.key).ai_result is None
    with sqlite3.connect(tmp_path / "recorder.db") as connection:
        assert connection.execute("SELECT version FROM schema_version").fetchone() == (2,)
