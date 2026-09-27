"""Versioned, count-domain features shared by the first rule baseline."""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from ai.src.build_windows import SensorWindow


FEATURE_VERSION = "counts_v1"


@dataclass(frozen=True)
class FeatureVector:
    feature_version: str
    accel_peak_magnitude_counts: float
    accel_peak_to_peak_counts: float
    accel_rms_magnitude_counts: float
    duration_seconds: float
    accel_crest_factor: float
    gyro_peak_magnitude_counts: float

    def as_dict(self) -> dict[str, float | str]:
        return asdict(self)


def extract_features(window: SensorWindow) -> FeatureVector:
    """Compute transparent features without assuming physical-unit calibration."""

    if window.sample_count <= 0 or window.sample_rate_hz <= 0:
        raise ValueError("window must contain samples at a positive sample rate")
    if window.accel_counts.shape != (window.sample_count, 3):
        raise ValueError("acceleration window must have shape (samples, 3)")
    if window.gyro_counts.shape != (window.sample_count, 3):
        raise ValueError("gyroscope window must have shape (samples, 3)")

    accel = window.accel_counts.astype(np.float64)
    gyro = window.gyro_counts.astype(np.float64)
    accel_magnitude = np.sqrt(np.sum(accel * accel, axis=1, dtype=np.float64))
    gyro_magnitude = np.sqrt(np.sum(gyro * gyro, axis=1, dtype=np.float64))
    peak = float(np.max(accel_magnitude))
    rms = float(np.sqrt(np.mean(accel_magnitude * accel_magnitude)))
    crest = peak / rms if rms > 0.0 else 0.0
    return FeatureVector(
        feature_version=FEATURE_VERSION,
        accel_peak_magnitude_counts=peak,
        accel_peak_to_peak_counts=float(np.ptp(accel_magnitude)),
        accel_rms_magnitude_counts=rms,
        duration_seconds=window.sample_count / window.sample_rate_hz,
        accel_crest_factor=crest,
        gyro_peak_magnitude_counts=float(np.max(gyro_magnitude)),
    )
