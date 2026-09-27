from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from ai.src.model_package import (
    MODEL_PACKAGE_VERSION,
    load_model_package,
    save_model_package,
)
from ai.src.train_model import (
    MLPTrainingConfig,
    train_mlp,
)


def _dataset() -> tuple[np.ndarray, tuple[str, ...], tuple[str, ...]]:
    background = np.asarray([
        [10, 2, 3, 1, 2, 4],
        [11, 3, 2, 1, 2, 5],
        [9, 2, 4, 1, 3, 4],
        [12, 4, 3, 2, 2, 5],
        [10, 3, 3, 2, 1, 4],
        [11, 2, 4, 1, 1, 5],
    ], dtype=np.float64)
    impact = np.asarray([
        [100, 80, 70, 3, 90, 120],
        [110, 70, 75, 3, 95, 130],
        [95, 90, 80, 4, 85, 125],
        [120, 85, 90, 4, 100, 140],
        [105, 75, 85, 3, 92, 135],
        [115, 82, 88, 4, 98, 145],
    ], dtype=np.float64)
    features = np.vstack((background, impact))
    labels = ("background",) * len(background) + ("impact",) * len(impact)
    groups = (
        "bg-1", "bg-1", "bg-2", "bg-2", "bg-3", "bg-3",
        "impact-1", "impact-1", "impact-2", "impact-2", "impact-3", "impact-3",
    )
    return features, labels, groups


def test_training_uses_train_only_normalization_and_is_deterministic() -> None:
    features, labels, groups = _dataset()
    first = train_mlp(
        features,
        labels,
        groups,
        config=MLPTrainingConfig(seed=7, epochs=120, hidden_units=4),
    )
    second = train_mlp(
        features,
        labels,
        groups,
        config=MLPTrainingConfig(seed=7, epochs=120, hidden_units=4),
    )

    np.testing.assert_array_equal(first.model.weights1, second.model.weights1)
    np.testing.assert_array_equal(first.model.feature_mean, second.model.feature_mean)
    assert first.split == second.split
    assert first.metrics == second.metrics
    assert first.model.class_names == ("background", "impact")


def test_training_refuses_group_leakage_and_unknown_labels() -> None:
    features, labels, groups = _dataset()
    with pytest.raises(ValueError, match="at least 3 groups per class"):
        train_mlp(features[:4], labels[:4], groups[:4])
    bad_labels = labels[:-1] + ("drop",)
    with pytest.raises(ValueError, match="unknown class"):
        train_mlp(features, bad_labels, groups)


def test_model_package_round_trip_and_corruption_rejection(tmp_path: Path) -> None:
    features, labels, groups = _dataset()
    result = train_mlp(
        features,
        labels,
        groups,
        config=MLPTrainingConfig(seed=3, epochs=30, hidden_units=4),
    )
    package_path = tmp_path / "model"
    save_model_package(
        result.model,
        package_path,
        dataset_manifest_sha256="a" * 64,
        training_revision="test-revision",
    )

    loaded = load_model_package(package_path)
    assert loaded.manifest["package_version"] == MODEL_PACKAGE_VERSION
    assert loaded.manifest["model_version"] == 2
    np.testing.assert_array_equal(
        loaded.model.predict_logits(features[:2]),
        result.model.predict_logits(features[:2]),
    )

    manifest_path = package_path / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["model_version"] = 1
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    with pytest.raises(ValueError, match="runtime version"):
        load_model_package(package_path)
    manifest["model_version"] = 2
    manifest["weight_scales"]["weights1"] = 123.0
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    with pytest.raises(ValueError, match="scale"):
        load_model_package(package_path)
    manifest["weight_scales"]["weights1"] = float(
        np.max(np.abs(result.model.weights1)) / 127.0
    )
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

    weights = package_path / "weights.npz"
    weights.write_bytes(weights.read_bytes() + b"corrupt")
    with pytest.raises(ValueError, match="CRC"):
        load_model_package(package_path)
