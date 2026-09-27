from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from ai.src.build_windows import (
    SlidingWindowSpec,
    TriggerWindowSpec,
    build_configured_windows,
    build_sliding_windows,
    build_trigger_window,
    load_window_config,
)
from host.transport_recorder.analysis.event_record import load_event


PROJECT_ROOT = Path(__file__).resolve().parents[2]
FIXTURES = PROJECT_ROOT / "host" / "tests" / "fixtures" / "events"


def test_build_trigger_window_uses_exact_trigger_boundary() -> None:
    event = load_event(FIXTURES / "valid-42.terp-event")

    window = build_trigger_window(event, TriggerWindowSpec(
        pretrigger_samples=1,
        posttrigger_samples=2,
    ), session_id="fixture-session")

    assert window.session_id == "fixture-session"
    assert window.event_id == 42
    assert window.start_sample == 0
    assert window.end_sample == 3
    assert window.trigger_offset == 1
    assert window.accel_counts.tolist() == event.accel_counts[:3].tolist()
    assert window.gyro_counts.tolist() == event.gyro_counts[:3].tolist()
    assert window.accel_counts.flags.writeable is False


def test_window_owns_an_immutable_copy_of_event_samples() -> None:
    event = load_event(FIXTURES / "valid-42.terp-event")
    window = build_trigger_window(
        event,
        TriggerWindowSpec(1, 2),
        session_id="fixture-session",
    )

    assert np.shares_memory(window.accel_counts, event.accel_counts) is False
    assert np.shares_memory(window.gyro_counts, event.gyro_counts) is False


def test_trigger_window_refuses_padding_and_data_loss() -> None:
    short_event = load_event(FIXTURES / "valid-42.terp-event")
    loss_event = load_event(FIXTURES / "valid-v3-loss-43.terp-event")

    with pytest.raises(ValueError, match="insufficient samples"):
        build_trigger_window(
            short_event,
            TriggerWindowSpec(2, 3),
            session_id="fixture-session",
        )
    with pytest.raises(ValueError, match="data loss"):
        build_trigger_window(
            loss_event,
            TriggerWindowSpec(1, 2),
            session_id="fixture-session",
        )


def test_sliding_windows_use_configured_length_and_step() -> None:
    event = load_event(FIXTURES / "valid-42.terp-event")

    windows = build_sliding_windows(event, SlidingWindowSpec(
        length_samples=2,
        step_samples=1,
    ), session_id="fixture-session")

    assert [(item.start_sample, item.end_sample) for item in windows] == [
        (0, 2),
        (1, 3),
        (2, 4),
    ]
    assert [item.trigger_offset for item in windows] == [1, 0, None]


def test_window_config_loads_and_selects_strategy_by_label(tmp_path: Path) -> None:
    config_path = tmp_path / "dataset.yaml"
    config_path.write_text(
        """\
version: 1
windows:
  trigger_aligned:
    pretrigger_samples: 1
    posttrigger_samples: 2
  background_sliding:
    length_samples: 2
    step_samples: 1
""",
        encoding="utf-8",
    )
    event = load_event(FIXTURES / "valid-42.terp-event")

    config = load_window_config(config_path)

    assert config.trigger_aligned == TriggerWindowSpec(1, 2)
    assert config.background_sliding == SlidingWindowSpec(2, 1)
    assert len(build_configured_windows(
        event,
        "impact",
        config,
        session_id="fixture-session",
    )) == 1
    assert len(build_configured_windows(
        event,
        "background",
        config,
        session_id="fixture-session",
    )) == 3


def test_window_config_allows_unfrozen_nulls_but_not_partial_values(
    tmp_path: Path,
) -> None:
    config_path = tmp_path / "dataset.yaml"
    config_path.write_text(
        """\
version: 1
windows:
  trigger_aligned:
    pretrigger_samples: null
    posttrigger_samples: null
  background_sliding:
    length_samples: 2
    step_samples: null
""",
        encoding="utf-8",
    )

    with pytest.raises(ValueError, match="both be null or both be integers"):
        load_window_config(config_path)


@pytest.mark.parametrize(
    "spec",
    [
        TriggerWindowSpec(0, 2),
        TriggerWindowSpec(1, 0),
        SlidingWindowSpec(0, 1),
        SlidingWindowSpec(2, 0),
    ],
)
def test_window_specs_must_be_positive(spec) -> None:
    with pytest.raises(ValueError, match="positive"):
        spec.validate()
