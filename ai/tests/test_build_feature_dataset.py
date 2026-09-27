from __future__ import annotations

from pathlib import Path

import numpy as np
import yaml

from ai.src.build_feature_dataset import build_feature_dataset
from ai.tests.test_validate_dataset import _add_hardware_profile, _write_dataset


def test_feature_dataset_uses_validated_event_and_session_group(tmp_path: Path) -> None:
    config_path = _write_dataset(tmp_path, label="impact")
    raw = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    raw["allowed_labels"] = ["background", "impact"]
    _add_hardware_profile(config_path)
    raw = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    raw["quality"]["expected_sample_rate_hz"] = 1600
    raw["windows"] = {"event": {"length_samples": 4}}
    raw["features"] = {
        "version": "counts_v1",
        "axis_order": ["ax", "ay", "az", "gx", "gy", "gz"],
        "names": [
            "accel_peak_magnitude_counts",
            "accel_peak_to_peak_counts",
            "accel_rms_magnitude_counts",
            "duration_seconds",
            "accel_crest_factor",
            "gyro_peak_magnitude_counts",
        ],
    }
    raw["model"] = {"class_names": ["background", "impact"], "quantization": "int8"}
    config_path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")
    output = tmp_path / "features"

    metadata = build_feature_dataset(config_path, output)

    values = np.load(output / "features.npy", allow_pickle=False)
    assert values.shape == (1, 6)
    assert metadata["event_count"] == 1
    assert (output / "labels.json").read_text(encoding="utf-8").strip() == '[\n  "impact"\n]'
    assert (output / "groups.json").read_text(encoding="utf-8").strip() == '[\n  "20260811_01"\n]'


def test_feature_dataset_includes_all_configured_four_classes(
    tmp_path: Path,
) -> None:
    config_path = _write_dataset(tmp_path, label="drop")
    _add_hardware_profile(config_path)
    manifest_path = tmp_path / "manifest.yaml"
    manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
    manifest["sessions"][0]["action_class"] = "drop"
    manifest_path.write_text(yaml.safe_dump(manifest, sort_keys=False), encoding="utf-8")
    raw = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    raw["quality"]["expected_sample_rate_hz"] = 1600
    raw["windows"] = {"event": {"length_samples": 4}}
    raw["features"] = {
        "version": "counts_v1",
        "axis_order": ["ax", "ay", "az", "gx", "gy", "gz"],
        "names": [
            "accel_peak_magnitude_counts",
            "accel_peak_to_peak_counts",
            "accel_rms_magnitude_counts",
            "duration_seconds",
            "accel_crest_factor",
            "gyro_peak_magnitude_counts",
        ],
    }
    raw["model"] = {
        "class_names": [
            "background",
            "impact",
            "continuous_vibration",
            "drop",
        ],
        "quantization": "int8",
    }
    config_path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")

    metadata = build_feature_dataset(config_path, tmp_path / "features")

    assert metadata["class_names"] == [
        "background",
        "impact",
        "continuous_vibration",
        "drop",
    ]
    assert (tmp_path / "features" / "labels.json").read_text(
        encoding="utf-8"
    ).strip() == '[\n  "drop"\n]'
