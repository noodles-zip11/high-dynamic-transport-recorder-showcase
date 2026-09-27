"""Frozen input contract shared by host training and firmware inference."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import yaml

from host.transport_recorder.analysis.event_record import DecodedEvent


FEATURE_NAMES = (
    "accel_peak_magnitude_counts",
    "accel_peak_to_peak_counts",
    "accel_rms_magnitude_counts",
    "duration_seconds",
    "accel_crest_factor",
    "gyro_peak_magnitude_counts",
)
AXIS_ORDER = ("ax", "ay", "az", "gx", "gy", "gz")
SUPPORTED_CLASS_NAMES = (
    "background",
    "impact",
    "continuous_vibration",
    "drop",
)
LEGACY_CLASS_NAMES = SUPPORTED_CLASS_NAMES[:2]
MAX_CLASS_COUNT = len(SUPPORTED_CLASS_NAMES)


@dataclass(frozen=True)
class DatasetContract:
    """The values that must agree before a model may be used."""

    sample_rate_hz: int
    event_length_samples: int
    axis_order: tuple[str, ...]
    feature_version: str
    feature_names: tuple[str, ...]
    class_names: tuple[str, ...]
    quantization: str


def load_dataset_contract(path: Path) -> DatasetContract:
    """Load and strictly validate the frozen model input section."""

    raw = yaml.safe_load(path.resolve().read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or raw.get("version") != 1:
        raise ValueError("dataset config version must be 1")

    hardware = raw.get("hardware")
    quality = raw.get("quality")
    sample_rate = _positive_int(
        hardware.get("sample_rate_hz") if isinstance(hardware, dict) else None,
        "hardware.sample_rate_hz",
    )
    if isinstance(quality, dict) and quality.get("expected_sample_rate_hz") is not None:
        expected_rate = _positive_int(
            quality.get("expected_sample_rate_hz"),
            "quality.expected_sample_rate_hz",
        )
        if expected_rate != sample_rate:
            raise ValueError("hardware and quality sample rates must match")

    windows = raw.get("windows")
    event_window = windows.get("event") if isinstance(windows, dict) else None
    event_length = _positive_int(
        event_window.get("length_samples")
        if isinstance(event_window, dict) else None,
        "windows.event.length_samples",
    )

    features = raw.get("features")
    if not isinstance(features, dict):
        raise ValueError("dataset config features must be a mapping")
    feature_version = features.get("version")
    axis_order = _text_tuple(features.get("axis_order"), "axis_order")
    feature_names = _text_tuple(features.get("names"), "feature names")
    if feature_version != "counts_v1":
        raise ValueError("unsupported feature version")
    if axis_order != AXIS_ORDER:
        raise ValueError("axis order must be ax, ay, az, gx, gy, gz")
    if feature_names != FEATURE_NAMES:
        raise ValueError("feature names do not match counts_v1 order")

    model = raw.get("model")
    if not isinstance(model, dict):
        raise ValueError("dataset config model must be a mapping")
    class_names = validate_class_names(
        model.get("class_names"), "model class names"
    )
    quantization = model.get("quantization")
    if quantization != "int8":
        raise ValueError("model quantization must be int8")

    return DatasetContract(
        sample_rate_hz=sample_rate,
        event_length_samples=event_length,
        axis_order=axis_order,
        feature_version=feature_version,
        feature_names=feature_names,
        class_names=class_names,
        quantization=quantization,
    )


def validate_event_contract(event: DecodedEvent, contract: DatasetContract) -> None:
    """Reject an event that cannot be represented by the frozen model input."""

    if event.metadata.lost_sample_count:
        raise ValueError("event has data loss")
    if event.metadata.sample_rate_hz != contract.sample_rate_hz:
        raise ValueError("event sample rate does not match contract")
    if event.sample_count != contract.event_length_samples:
        raise ValueError(
            "event must contain exactly "
            f"{contract.event_length_samples} samples"
        )
    if event.accel_counts.shape != (contract.event_length_samples, 3):
        raise ValueError("event acceleration axes do not match contract")
    if event.gyro_counts.shape != (contract.event_length_samples, 3):
        raise ValueError("event gyroscope axes do not match contract")


def _positive_int(value: object, name: str) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{name} must be a positive integer")
    try:
        result = int(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"{name} must be a positive integer") from error
    if result <= 0:
        raise ValueError(f"{name} must be a positive integer")
    return result


def _text_tuple(value: object, name: str) -> tuple[str, ...]:
    if not isinstance(value, list) or not value or not all(
        isinstance(item, str) and item.strip() for item in value
    ):
        raise ValueError(f"{name} must be a nonempty list of strings")
    return tuple(item.strip() for item in value)


def validate_class_names(
    value: object,
    name: str = "class names",
) -> tuple[str, ...]:
    """Validate the fixed class-order contract while retaining 2-class inputs."""

    if not isinstance(value, (list, tuple)) or not value or not all(
        isinstance(item, str) and item.strip() for item in value
    ):
        raise ValueError(f"{name} must be a nonempty list of strings")
    names = tuple(item.strip() for item in value)
    if len(names) < 2 or len(names) > MAX_CLASS_COUNT:
        raise ValueError(
            f"{name} must contain between 2 and {MAX_CLASS_COUNT} classes"
        )
    if names != SUPPORTED_CLASS_NAMES[:len(names)]:
        raise ValueError(
            f"{name} must use the fixed prefix/order: "
            f"{', '.join(SUPPORTED_CLASS_NAMES)}"
        )
    return names
