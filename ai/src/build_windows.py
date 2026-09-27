"""Build immutable, fixed-shape six-axis windows from validated events."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import yaml

from host.transport_recorder.analysis.event_record import DecodedEvent


@dataclass(frozen=True)
class TriggerWindowSpec:
    pretrigger_samples: int
    posttrigger_samples: int

    def validate(self) -> None:
        if self.pretrigger_samples <= 0 or self.posttrigger_samples <= 0:
            raise ValueError("trigger window sample counts must be positive")


@dataclass(frozen=True)
class SlidingWindowSpec:
    length_samples: int
    step_samples: int

    def validate(self) -> None:
        if self.length_samples <= 0 or self.step_samples <= 0:
            raise ValueError("sliding window length and step must be positive")


@dataclass(frozen=True)
class WindowConfig:
    trigger_aligned: TriggerWindowSpec | None
    background_sliding: SlidingWindowSpec | None


@dataclass(frozen=True)
class SensorWindow:
    session_id: str
    event_id: int
    sample_rate_hz: int
    start_sample: int
    end_sample: int
    trigger_offset: int | None
    accel_counts: np.ndarray
    gyro_counts: np.ndarray

    @property
    def sample_count(self) -> int:
        return self.end_sample - self.start_sample


def build_trigger_window(
    event: DecodedEvent,
    spec: TriggerWindowSpec,
    *,
    session_id: str,
) -> SensorWindow:
    """Return one exact trigger-aligned window without padding or interpolation."""

    spec.validate()
    _reject_data_loss(event)
    start = event.trigger_index - spec.pretrigger_samples
    end = event.trigger_index + spec.posttrigger_samples
    if start < 0 or end > event.sample_count:
        raise ValueError("event has insufficient samples for trigger window")
    return _slice_window(event, start, end, session_id)


def build_sliding_windows(
    event: DecodedEvent,
    spec: SlidingWindowSpec,
    *,
    session_id: str,
) -> tuple[SensorWindow, ...]:
    """Return configured overlapping windows while retaining event boundaries."""

    spec.validate()
    _reject_data_loss(event)
    if event.sample_count < spec.length_samples:
        raise ValueError("event has insufficient samples for sliding window")
    return tuple(
        _slice_window(event, start, start + spec.length_samples, session_id)
        for start in range(
            0,
            event.sample_count - spec.length_samples + 1,
            spec.step_samples,
        )
    )


def load_window_config(path: Path) -> WindowConfig:
    """Load frozen window parameters from the versioned dataset config."""

    raw = yaml.safe_load(path.resolve().read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or raw.get("version") != 1:
        raise ValueError("dataset config version must be 1")
    windows = raw.get("windows")
    if not isinstance(windows, dict):
        raise ValueError("dataset config windows must be a mapping")
    trigger = _optional_pair(
        windows.get("trigger_aligned"),
        "pretrigger_samples",
        "posttrigger_samples",
        "trigger_aligned",
    )
    sliding = _optional_pair(
        windows.get("background_sliding"),
        "length_samples",
        "step_samples",
        "background_sliding",
    )
    trigger_spec = None if trigger is None else TriggerWindowSpec(*trigger)
    sliding_spec = None if sliding is None else SlidingWindowSpec(*sliding)
    if trigger_spec is not None:
        trigger_spec.validate()
    if sliding_spec is not None:
        sliding_spec.validate()
    return WindowConfig(trigger_spec, sliding_spec)


def build_configured_windows(
    event: DecodedEvent,
    label: str,
    config: WindowConfig,
    *,
    session_id: str,
    background_label: str = "background",
) -> tuple[SensorWindow, ...]:
    """Apply the configured strategy while preserving event grouping."""

    if label == background_label:
        if config.background_sliding is None:
            raise ValueError("background sliding-window parameters are not frozen")
        return build_sliding_windows(
            event,
            config.background_sliding,
            session_id=session_id,
        )
    if config.trigger_aligned is None:
        raise ValueError("trigger-aligned window parameters are not frozen")
    return (build_trigger_window(
        event,
        config.trigger_aligned,
        session_id=session_id,
    ),)


def _optional_pair(
    raw: object,
    first_key: str,
    second_key: str,
    section: str,
) -> tuple[int, int] | None:
    if not isinstance(raw, dict):
        raise ValueError(f"windows.{section} must be a mapping")
    first = raw.get(first_key)
    second = raw.get(second_key)
    if first is None and second is None:
        return None
    if first is None or second is None:
        raise ValueError(
            f"windows.{section} values must both be null or both be integers"
        )
    if (
        isinstance(first, bool)
        or isinstance(second, bool)
        or not isinstance(first, int)
        or not isinstance(second, int)
    ):
        raise ValueError(
            f"windows.{section} values must both be null or both be integers"
        )
    return first, second


def _reject_data_loss(event: DecodedEvent) -> None:
    if event.metadata.lost_sample_count:
        raise ValueError("cannot build training windows from an event with data loss")


def _slice_window(
    event: DecodedEvent,
    start: int,
    end: int,
    session_id: str,
) -> SensorWindow:
    if not session_id:
        raise ValueError("session_id must be nonempty")
    trigger_offset = (
        event.trigger_index - start
        if start <= event.trigger_index < end
        else None
    )
    accel = np.array(event.accel_counts[start:end], copy=True)
    gyro = np.array(event.gyro_counts[start:end], copy=True)
    accel.setflags(write=False)
    gyro.setflags(write=False)
    return SensorWindow(
        session_id=session_id,
        event_id=event.metadata.event_id,
        sample_rate_hz=event.metadata.sample_rate_hz,
        start_sample=start,
        end_sample=end,
        trigger_offset=trigger_offset,
        accel_counts=accel,
        gyro_counts=gyro,
    )
