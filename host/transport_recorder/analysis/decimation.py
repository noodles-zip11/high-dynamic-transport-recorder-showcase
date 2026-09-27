"""Bounded replay data that preserves extrema rather than striding samples."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass(frozen=True)
class Envelope:
    timestamps_us: np.ndarray
    values: np.ndarray


def min_max_envelope(
    timestamps_us: np.ndarray,
    values: np.ndarray,
    *,
    maximum_points: int,
) -> Envelope:
    """Return chronologically ordered min/max representatives per source bucket."""

    times = np.asarray(timestamps_us)
    source = np.asarray(values)
    if times.ndim != 1 or source.ndim != 1:
        raise ValueError("min/max envelope expects one-dimensional inputs")
    if len(times) != len(source):
        raise ValueError("timestamps and values must have equal length")
    if maximum_points < 2:
        raise ValueError("maximum_points must be at least two")
    if len(source) <= maximum_points:
        return _freeze(Envelope(times.copy(), source.copy()))

    bucket_count = maximum_points // 2
    boundaries = np.linspace(0, len(source), bucket_count + 1, dtype=np.int64)
    selected: list[int] = []
    for start, end in zip(boundaries[:-1], boundaries[1:], strict=True):
        if end <= start:
            continue
        bucket = source[start:end]
        minimum_index = int(start + np.argmin(bucket))
        maximum_index = int(start + np.argmax(bucket))
        for index in sorted({minimum_index, maximum_index}):
            selected.append(index)

    return _freeze(Envelope(times[selected].copy(), source[selected].copy()))


def _freeze(envelope: Envelope) -> Envelope:
    envelope.timestamps_us.setflags(write=False)
    envelope.values.setflags(write=False)
    return envelope
