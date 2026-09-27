from __future__ import annotations

from dataclasses import FrozenInstanceError
from pathlib import Path

import pytest

from host.transport_recorder.analysis.event_record import (
    EventFormatError,
    load_event,
)


FIXTURE_EVENTS = Path(__file__).parents[1] / "fixtures" / "events"


def test_load_event_decodes_a_valid_ev02_fixture() -> None:
    event = load_event(FIXTURE_EVENTS / "valid-42.terp-event")

    assert event.metadata.event_id == 42
    assert event.metadata.format_version == 2
    assert event.metadata.sample_rate_hz == 1600
    assert event.sample_count == 4
    assert event.trigger_index == 1
    assert event.accel_counts.tolist() == [[-7, 3, 1], [97, 4, 2], [-5, 5, 3], [6, 6, 4]]
    assert event.gyro_counts.tolist() == [[10, 11, 12], [13, 14, 15], [16, 17, 18], [19, 20, 21]]
    assert event.timestamps_us.tolist() == [0, 625, 1250, 1875]
    assert 97.0 < event.accel_magnitude_counts[1] < 98.0


def test_load_event_decodes_immutable_ev03_loss_metadata() -> None:
    event = load_event(FIXTURE_EVENTS / "valid-v3-loss-43.terp-event")

    assert event.metadata.format_version == 3
    assert event.metadata.event_id == 43
    assert event.metadata.lost_sample_count == 6
    assert event.metadata.first_lost_sequence == 102
    assert event.metadata.last_lost_sequence == 109
    assert event.metadata.loss_episode_count == 2
    assert event.metadata.first_loss_monotonic_us == 2_001_250
    assert event.metadata.last_loss_monotonic_us == 2_005_625
    with pytest.raises(FrozenInstanceError):
        event.metadata.lost_sample_count = 0  # type: ignore[misc]


@pytest.mark.parametrize(
    ("name", "error"),
    [
        ("crc-invalid-42.terp-event", "payload CRC"),
        ("truncated-42.terp-event", "record length"),
        ("unknown-version-42.terp-event", "unsupported event version"),
    ],
)
def test_load_event_rejects_invalid_ev02_fixture(name: str, error: str) -> None:
    with pytest.raises(EventFormatError, match=error):
        load_event(FIXTURE_EVENTS / name)
