from __future__ import annotations

import numpy as np

from host.transport_recorder.analysis.decimation import min_max_envelope


def test_min_max_envelope_retains_an_impulse_between_bucket_edges() -> None:
    timestamps = np.arange(6, dtype=np.int64)
    values = np.array([0, 0, 97, 0, 0, 0], dtype=np.int16)

    envelope = min_max_envelope(timestamps, values, maximum_points=4)

    assert 97 in envelope.values
    assert len(envelope.timestamps_us) == len(envelope.values)
    assert len(envelope.values) <= 4


def test_min_max_envelope_keeps_small_series_unchanged() -> None:
    timestamps = np.array([10, 20, 30], dtype=np.int64)
    values = np.array([-1, 2, 3], dtype=np.int16)

    envelope = min_max_envelope(timestamps, values, maximum_points=4)

    assert envelope.timestamps_us.tolist() == [10, 20, 30]
    assert envelope.values.tolist() == [-1, 2, 3]
