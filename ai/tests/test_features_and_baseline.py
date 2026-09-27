from __future__ import annotations

import math
from dataclasses import replace
from pathlib import Path

import numpy as np
import pytest

from ai.src.build_windows import SensorWindow
from ai.src.features import extract_features
from ai.src.train_baseline import (
    BaselineRule,
    FeatureCondition,
    RuleBaseline,
    load_rule_baseline,
)


def _window(accel: list[list[int]], gyro: list[list[int]], rate: int = 1000) -> SensorWindow:
    accel_values = np.asarray(accel, dtype=np.int16)
    gyro_values = np.asarray(gyro, dtype=np.int16)
    accel_values.setflags(write=False)
    gyro_values.setflags(write=False)
    return SensorWindow(
        session_id="fixture-session",
        event_id=1,
        sample_rate_hz=rate,
        start_sample=0,
        end_sample=len(accel),
        trigger_offset=0,
        accel_counts=accel_values,
        gyro_counts=gyro_values,
    )


def test_extracts_count_domain_features_with_hand_checked_values() -> None:
    window = _window(
        accel=[[3, 4, 0], [0, 0, 0]],
        gyro=[[0, 0, 2], [0, 0, -6]],
    )

    features = extract_features(window)

    assert features.feature_version == "counts_v1"
    assert features.accel_peak_magnitude_counts == 5.0
    assert features.accel_peak_to_peak_counts == 5.0
    assert features.accel_rms_magnitude_counts == pytest.approx(math.sqrt(12.5))
    assert features.duration_seconds == 0.002
    assert features.accel_crest_factor == pytest.approx(math.sqrt(2.0))
    assert features.gyro_peak_magnitude_counts == 6.0


def test_zero_signal_has_zero_crest_factor() -> None:
    features = extract_features(_window(
        accel=[[0, 0, 0], [0, 0, 0]],
        gyro=[[0, 0, 0], [0, 0, 0]],
    ))

    assert features.accel_rms_magnitude_counts == 0.0
    assert features.accel_crest_factor == 0.0


def test_rule_baseline_uses_explicit_ordered_conditions() -> None:
    features = extract_features(_window(
        accel=[[3, 4, 0], [0, 0, 0]],
        gyro=[[0, 0, 0], [0, 0, 0]],
    ))
    baseline = RuleBaseline(
        rules=(
            BaselineRule(
                label="impact",
                conditions=(FeatureCondition(
                    feature_name="accel_peak_magnitude_counts",
                    minimum=4.0,
                ),),
            ),
            BaselineRule(
                label="background",
                conditions=(FeatureCondition(
                    feature_name="accel_peak_magnitude_counts",
                    maximum=1.0,
                ),),
            ),
        ),
        default_label="unknown",
    )

    assert baseline.predict(features) == "impact"


def test_rule_baseline_rejects_unknown_features_and_unbounded_conditions() -> None:
    with pytest.raises(ValueError, match="at least one bound"):
        FeatureCondition("accel_peak_magnitude_counts").validate()
    baseline = RuleBaseline(
        rules=(BaselineRule(
            label="impact",
            conditions=(FeatureCondition("not_a_feature", minimum=1.0),),
        ),),
        default_label="unknown",
    )
    with pytest.raises(ValueError, match="unknown feature"):
        baseline.predict(extract_features(_window(
            accel=[[0, 0, 0]],
            gyro=[[0, 0, 0]],
        )))


def test_rule_baseline_rejects_incompatible_feature_version() -> None:
    features = extract_features(_window(
        accel=[[0, 0, 0]],
        gyro=[[0, 0, 0]],
    ))

    with pytest.raises(ValueError, match="feature version"):
        RuleBaseline(rules=()).predict(replace(
            features,
            feature_version="counts_v2",
        ))


def test_rule_baseline_loads_and_validates_versioned_yaml(tmp_path: Path) -> None:
    config_path = tmp_path / "dataset.yaml"
    config_path.write_text(
        """\
version: 1
allowed_labels: [background, impact, unknown]
baseline:
  feature_version: counts_v1
  default_label: unknown
  rules:
    - label: impact
      conditions:
        - feature_name: accel_peak_magnitude_counts
          minimum: 4.0
""",
        encoding="utf-8",
    )

    baseline = load_rule_baseline(config_path)
    features = extract_features(_window(
        accel=[[3, 4, 0]],
        gyro=[[0, 0, 0]],
    ))

    assert baseline.predict(features) == "impact"


def test_rule_baseline_config_rejects_labels_outside_dataset_contract(
    tmp_path: Path,
) -> None:
    config_path = tmp_path / "dataset.yaml"
    config_path.write_text(
        """\
version: 1
allowed_labels: [background, impact, unknown]
baseline:
  feature_version: counts_v1
  default_label: unknown
  rules:
    - label: drop
      conditions:
        - feature_name: accel_peak_magnitude_counts
          minimum: 4.0
""",
        encoding="utf-8",
    )

    with pytest.raises(ValueError, match="not allowed"):
        load_rule_baseline(config_path)
