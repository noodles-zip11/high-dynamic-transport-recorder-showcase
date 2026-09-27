from __future__ import annotations

from dataclasses import replace
import json
from pathlib import Path
import threading
from types import SimpleNamespace

import pytest

from host.transport_recorder.analysis.event_record import load_event
from host.transport_recorder.protocol.client import AiResult, DownloadProgress, EventInfo
from host.transport_recorder.device.session import EventPage, SessionState
from host.transport_recorder.repository.event_repository import EventRepository
from host.transport_recorder.ui.main_window import MainWindow


FIXTURE_EVENTS = Path(__file__).parents[1] / "fixtures" / "events"


def test_main_window_opens_local_event_and_keeps_replay_when_disconnected(
    qtbot, tmp_path: Path
) -> None:
    window = MainWindow(repository=EventRepository(tmp_path), session_factory=lambda port: None)
    qtbot.addWidget(window)

    with qtbot.waitSignal(window.event_loaded, timeout=1_000):
        window.open_local_event(FIXTURE_EVENTS / "valid-42.terp-event")

    window.show_disconnected("cable removed")

    assert window.replay_widget.event_id_text() == "42"
    assert not window.download_action.isEnabled()
    assert "Disconnected: cable removed" == window.statusBar().currentMessage()


def test_main_window_lists_port_identity_without_opening_it(qtbot, tmp_path: Path) -> None:
    window = MainWindow(repository=EventRepository(tmp_path), session_factory=lambda port: None)
    qtbot.addWidget(window)

    window.set_ports(
        [
            {
                "device": "COM7",
                "description": "Transport recorder",
                "serial_number": "USB-ABC",
            }
        ]
    )

    assert window.port_list.topLevelItemCount() == 1
    assert window.port_list.topLevelItem(0).text(0) == "COM7"
    assert window.port_list.topLevelItem(0).text(2) == "USB-ABC"


def test_replay_cursor_uses_one_event_time_axis_for_channel_values(qtbot, tmp_path: Path) -> None:
    window = MainWindow(repository=EventRepository(tmp_path), session_factory=lambda port: None)
    qtbot.addWidget(window)
    window.show_decoded_event(load_event(FIXTURE_EVENTS / "valid-42.terp-event"))

    window.replay_widget.set_cursor_time(0.000625)

    assert "t=0.000625 s" in window.replay_widget.cursor_text()
    assert "ax=97" in window.replay_widget.cursor_text()
    assert "gx=13" in window.replay_widget.cursor_text()


def test_replay_exports_png_without_overwriting_existing_file(qtbot, tmp_path: Path) -> None:
    window = MainWindow(repository=EventRepository(tmp_path), session_factory=lambda port: None)
    qtbot.addWidget(window)
    window.show_decoded_event(load_event(FIXTURE_EVENTS / "valid-42.terp-event"))
    target = tmp_path / "event.png"

    output = window.replay_widget.export_png(target)

    assert output.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    with pytest.raises(FileExistsError, match="already exists"):
        window.replay_widget.export_png(target)


def test_download_status_shows_verified_bytes_speed_and_eta(qtbot, tmp_path: Path) -> None:
    window = MainWindow(repository=EventRepository(tmp_path), session_factory=lambda port: None)
    qtbot.addWidget(window)
    window._download_started_at = 1.0

    window._on_download_progress(
        DownloadProgress(event_id=42, total_bytes=1_000, verified_bytes=500)
    )

    status = window.statusBar().currentMessage()
    assert "500/1000 bytes" in status
    assert "B/s" in status
    assert "ETA" in status


def test_device_actions_are_gated_while_a_device_worker_runs(qtbot, tmp_path: Path) -> None:
    window = MainWindow(repository=EventRepository(tmp_path), session_factory=lambda port: None)
    qtbot.addWidget(window)
    entered = threading.Event()
    release = threading.Event()
    window._session = SimpleNamespace(  # type: ignore[assignment]
        state=SessionState.READY,
        close=lambda: None,
    )
    window.download_action.setEnabled(True)

    def blocking_operation() -> object:
        entered.set()
        assert release.wait(timeout=2.0)
        return object()

    window._start_device_operation(blocking_operation, lambda result: None)
    assert entered.wait(timeout=1.0)
    qtbot.waitUntil(lambda: not window.connect_action.isEnabled(), timeout=1_000)
    assert not window.download_action.isEnabled()

    release.set()
    qtbot.waitUntil(lambda: not window._device_workers, timeout=2_000)
    assert window.connect_action.isEnabled()
    assert window.download_action.isEnabled()


def test_event_pages_append_and_load_more_stops_at_zero_cursor(qtbot, tmp_path: Path) -> None:
    window = MainWindow(repository=EventRepository(tmp_path), session_factory=lambda port: None)
    qtbot.addWidget(window)
    window._session = SimpleNamespace(  # type: ignore[assignment]
        state=SessionState.READY,
        close=lambda: None,
    )

    window._on_event_page(
        EventPage(events=(EventInfo(41, 1_000, 0x11111111),), next_event_id=41),
        append=False,
    )

    assert window.event_list.topLevelItemCount() == 1
    assert window.load_more_button.isEnabled()

    window._on_event_page(
        EventPage(events=(EventInfo(42, 2_000, 0x22222222),), next_event_id=0),
        append=True,
    )

    assert [window.event_list.topLevelItem(index).text(0) for index in range(2)] == [
        "41",
        "42",
    ]
    assert not window.load_more_button.isEnabled()


def test_annotation_is_persisted_and_json_export_reads_repository(
    qtbot, tmp_path: Path
) -> None:
    repository = EventRepository(tmp_path)
    stored = repository.import_event(
        FIXTURE_EVENTS / "valid-42.terp-event", device_serial="SIM-0001"
    )
    window = MainWindow(repository=repository, session_factory=lambda port: None)
    qtbot.addWidget(window)
    window.show_decoded_event(load_event(stored.path), stored.path)

    window.save_current_annotation(label="impact", note="right-side collision")
    output = window.export_current_json(tmp_path / "event.json")

    persisted = repository.get_event(stored.key)
    exported = json.loads(output.read_text(encoding="utf-8"))
    assert persisted.label == "impact"
    assert persisted.note == "right-side collision"
    assert exported["annotation"] == {
        "label": "impact",
        "note": "right-side collision",
    }


def test_model_result_is_presented_separately_from_human_annotation(
    qtbot, tmp_path: Path
) -> None:
    repository = EventRepository(tmp_path)
    stored = repository.import_event(
        FIXTURE_EVENTS / "valid-42.terp-event", device_serial="SIM-0001"
    )
    repository.update_ai_result(
        stored.key,
        AiResult(
            event_id=42,
            model_version=2,
            status=1,
            class_index=1,
            class_count=2,
            quality_flags=3,
            event_flags=0,
            sample_count=2400,
            model_crc32=0xAABBCCDD,
            confidence=0.875,
            logits=(-1.0, 1.0, 0.0, 0.0),
            failure_reason=0,
            result_sequence=9,
        ),
    )
    window = MainWindow(repository=repository, session_factory=lambda port: None)
    qtbot.addWidget(window)

    window.show_decoded_event(load_event(stored.path), stored.path)

    assert "Model prediction" in window.ai_result_label.text()
    assert "impact" not in window.ai_result_label.text()
    assert window.annotation_label_edit.isEnabled()


def test_replay_shows_distinct_data_quality_warnings(qtbot, tmp_path: Path) -> None:
    window = MainWindow(repository=EventRepository(tmp_path), session_factory=lambda port: None)
    qtbot.addWidget(window)
    normal = load_event(FIXTURE_EVENTS / "valid-42.terp-event")
    loss = load_event(FIXTURE_EVENTS / "valid-v3-loss-43.terp-event")
    saturated_accel = normal.accel_counts.copy()
    saturated_accel[0, 0] = 32767
    saturated = replace(normal, accel_counts=saturated_accel)
    utc_invalid = replace(
        normal,
        metadata=replace(
            normal.metadata,
            context_valid_flags=normal.metadata.context_valid_flags & ~0x0001,
        ),
    )
    storage_error = replace(
        normal,
        metadata=replace(normal.metadata, storage_error_count=3),
    )

    window.show_decoded_event(loss)
    assert "DATA LOSS" in window.replay_widget.warning_text()
    window.show_decoded_event(saturated)
    assert "SATURATED SAMPLE" in window.replay_widget.warning_text()
    window.show_decoded_event(utc_invalid)
    assert "UTC INVALID" in window.replay_widget.warning_text()
    window.show_decoded_event(storage_error)
    assert "STORAGE ERRORS" in window.replay_widget.warning_text()
