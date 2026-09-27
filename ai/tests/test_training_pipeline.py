from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np
import pytest

from ai.src.dataset_contract import FEATURE_NAMES
from ai.src.train_model import (
    CLASS_NAMES,
    _is_sha256,
    MLPTrainingConfig,
    load_frozen_split,
    train_mlp,
)


PROJECT_ROOT = Path(__file__).resolve().parents[2]

FOUR_CLASS_NAMES = (
    "background",
    "impact",
    "continuous_vibration",
    "drop",
)


@pytest.mark.parametrize(
    "value", ("-" + "a" * 63, "+" + "a" * 63, " " + "a" * 63)
)
def test_sha256_validation_rejects_non_hex_prefixes(value: str) -> None:
    assert not _is_sha256(value)


def _dataset() -> tuple[np.ndarray, tuple[str, ...], tuple[str, ...], tuple[int, ...]]:
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
    labels = ("background",) * 6 + ("impact",) * 6
    groups = (
        "bg-1", "bg-1", "bg-2", "bg-2", "bg-3", "bg-3",
        "impact-1", "impact-1", "impact-2", "impact-2", "impact-3", "impact-3",
    )
    event_ids = tuple(range(100, 112))
    return features, labels, groups, event_ids


def _write_split_manifest(path: Path) -> Path:
    _, labels, groups, event_ids = _dataset()
    assignment = {
        "bg-1": "train",
        "impact-1": "train",
        "bg-2": "validation",
        "impact-2": "validation",
        "bg-3": "test",
        "impact-3": "test",
    }
    partitions: dict[str, list[dict[str, object]]] = {
        "train": [],
        "validation": [],
        "test": [],
    }
    raw_directory = path.parent / "raw"
    raw_directory.mkdir(exist_ok=True)
    source_events: list[dict[str, object]] = []
    for event_id, label, group in zip(event_ids, labels, groups):
        source_path = raw_directory / f"event-{event_id}.bin"
        source_path.write_bytes(f"source-{event_id}".encode("utf-8"))
        source_sha256 = hashlib.sha256(source_path.read_bytes()).hexdigest()
        partitions[assignment[group]].append({
            "session_id": group,
            "event_id": event_id,
            "source_sha256": source_sha256,
            "label": label,
        })
        source_events.append({
            "session_id": group,
            "event_id": event_id,
            "source_path": f"event-{event_id}.bin",
            "source_sha256": source_sha256,
            "label": label,
        })
    source_manifest_path = path.parent / "dataset-manifest.yaml"
    source_manifest_path.write_text(
        json.dumps({
            "version": 1,
            "dataset_root": "raw",
            "sessions": [],
            "events": source_events,
        }, indent=2) + "\n",
        encoding="utf-8",
    )
    records = [record for partition in partitions.values() for record in partition]
    dataset_hash = hashlib.sha256(json.dumps(
        sorted(records, key=lambda record: (record["session_id"], record["event_id"])),
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")).hexdigest()
    manifest = {
        "version": 1,
        "dataset_hash": dataset_hash,
        "source_manifest_sha256": hashlib.sha256(
            source_manifest_path.read_bytes()
        ).hexdigest(),
        "seed": 20260811,
        "grouping": "session_id",
        "ratios": {"train": 1 / 3, "validation": 1 / 3, "test": 1 / 3},
        "class_session_counts": {},
        "session_partitions": assignment,
        "partitions": partitions,
    }
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return source_manifest_path


def _write_feature_metadata(
    path: Path,
    *,
    features_path: Path,
    labels_path: Path,
    groups_path: Path,
    event_ids_path: Path,
    source_manifest_path: Path,
) -> None:
    _, labels, groups, event_ids = _dataset()
    metadata = {
        "metadata_version": 1,
        "feature_version": "counts_v1",
        "feature_names": list(FEATURE_NAMES),
        "class_names": list(CLASS_NAMES),
        "manifest_sha256": hashlib.sha256(source_manifest_path.read_bytes()).hexdigest(),
        "event_count": len(event_ids),
        "features_sha256": hashlib.sha256(features_path.read_bytes()).hexdigest(),
        "labels_sha256": hashlib.sha256(labels_path.read_bytes()).hexdigest(),
        "groups_sha256": hashlib.sha256(groups_path.read_bytes()).hexdigest(),
        "event_ids_sha256": hashlib.sha256(event_ids_path.read_bytes()).hexdigest(),
        "rows": [
            {
                "session_id": group,
                "event_id": event_id,
                "label": label,
                "source_sha256": hashlib.sha256(
                    f"source-{event_id}".encode("utf-8")
                ).hexdigest(),
            }
            for event_id, label, group in zip(event_ids, labels, groups, strict=True)
        ],
    }
    path.write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def test_training_consumes_the_explicit_frozen_split(tmp_path: Path) -> None:
    features, labels, groups, event_ids = _dataset()
    split_path = tmp_path / "split_manifest.json"
    source_manifest_path = _write_split_manifest(split_path)

    frozen = load_frozen_split(
        split_path,
        source_manifest_path=source_manifest_path,
        source_hashes=tuple(
            hashlib.sha256(f"source-{event_id}".encode("utf-8")).hexdigest()
            for event_id in event_ids
        ),
        event_ids=event_ids,
        labels=labels,
        groups=groups,
    )
    result = train_mlp(
        features,
        labels,
        groups,
        split=frozen,
        config=MLPTrainingConfig(seed=20260816, epochs=2, hidden_units=2),
    )

    assert result.split == {
        "train": (0, 1, 6, 7),
        "validation": (2, 3, 8, 9),
        "test": (4, 5, 10, 11),
    }
    np.testing.assert_allclose(
        result.model.feature_mean,
        np.mean(features[list(result.split["train"])], axis=0),
    )


def test_training_accepts_four_configured_classes() -> None:
    features = np.arange(12 * len(FEATURE_NAMES), dtype=np.float64).reshape(
        12, len(FEATURE_NAMES)
    )
    labels = tuple(
        label
        for label in FOUR_CLASS_NAMES
        for _ in range(3)
    )
    groups = tuple(f"event-{index}" for index in range(len(labels)))
    split = {
        "train": (0, 3, 6, 9),
        "validation": (1, 4, 7, 10),
        "test": (2, 5, 8, 11),
    }

    result = train_mlp(
        features,
        labels,
        groups,
        class_names=FOUR_CLASS_NAMES,
        split=split,
        config=MLPTrainingConfig(seed=11, epochs=2, hidden_units=2),
    )

    assert result.model.class_names == FOUR_CLASS_NAMES
    assert result.model.weights2.shape == (2, 4)
    assert result.model.predict_logits(features[:1]).shape == (1, 4)


def test_frozen_split_accepts_explicit_pilot_grouping(tmp_path: Path) -> None:
    _, labels, groups, event_ids = _dataset()
    split_path = tmp_path / "split_manifest.json"
    source_manifest_path = _write_split_manifest(split_path)
    raw = json.loads(split_path.read_text(encoding="utf-8"))
    raw["grouping"] = "pilot_event_stratified"
    raw["within_session_pilot"] = True
    raw["generalization_warning"] = (
        "within_session_pilot=true; does not represent independent-session generalization"
    )
    split_path.write_text(json.dumps(raw, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    frozen = load_frozen_split(
        split_path,
        source_manifest_path=source_manifest_path,
        source_hashes=tuple(
            hashlib.sha256(f"source-{event_id}".encode("utf-8")).hexdigest()
            for event_id in event_ids
        ),
        event_ids=event_ids,
        labels=labels,
        groups=groups,
        class_names=CLASS_NAMES,
    )

    assert set(frozen) == {"train", "validation", "test"}


def test_training_cli_writes_a_hash_linked_artifact_report(tmp_path: Path) -> None:
    features, labels, groups, event_ids = _dataset()
    features_path = tmp_path / "features.npy"
    labels_path = tmp_path / "labels.json"
    groups_path = tmp_path / "groups.json"
    event_ids_path = tmp_path / "event_ids.json"
    feature_metadata_path = tmp_path / "metadata.json"
    split_path = tmp_path / "split_manifest.json"
    output_path = tmp_path / "model"
    report_path = tmp_path / "training-report.json"
    header_path = tmp_path / "ai_model_data.h"
    source_path = tmp_path / "ai_model_data.c"
    np.save(features_path, features)
    labels_path.write_text(json.dumps(labels), encoding="utf-8")
    groups_path.write_text(json.dumps(groups), encoding="utf-8")
    event_ids_path.write_text(json.dumps(event_ids), encoding="utf-8")
    source_manifest_path = _write_split_manifest(split_path)
    _write_feature_metadata(
        feature_metadata_path,
        features_path=features_path,
        labels_path=labels_path,
        groups_path=groups_path,
        event_ids_path=event_ids_path,
        source_manifest_path=source_manifest_path,
    )

    completed = subprocess.run(
        [
            sys.executable,
            "-m",
            "ai.src.train_model",
            "--features",
            str(features_path),
            "--labels",
            str(labels_path),
            "--groups",
            str(groups_path),
            "--event-ids",
            str(event_ids_path),
            "--feature-metadata",
            str(feature_metadata_path),
            "--split-manifest",
            str(split_path),
            "--dataset-manifest",
            str(source_manifest_path),
            "--output",
            str(output_path),
            "--report",
            str(report_path),
            "--generated-header",
            str(header_path),
            "--generated-source",
            str(source_path),
            "--training-revision",
            "test-revision",
            "--epochs",
            "2",
            "--hidden-units",
            "2",
        ],
        cwd=PROJECT_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )

    assert completed.returncode == 0, completed.stderr
    report = json.loads(report_path.read_text(encoding="utf-8"))
    assert report["provenance"]["split_manifest_sha256"] == hashlib.sha256(
        split_path.read_bytes()
    ).hexdigest()
    assert report["provenance"]["source_manifest_sha256"] == hashlib.sha256(
        source_manifest_path.read_bytes()
    ).hexdigest()
    assert report["provenance"]["feature_matrix_sha256"] == hashlib.sha256(
        features_path.read_bytes()
    ).hexdigest()
    assert report["provenance"]["feature_metadata_sha256"] == hashlib.sha256(
        feature_metadata_path.read_bytes()
    ).hexdigest()
    assert report["artifacts"]["model_manifest_sha256"] == hashlib.sha256(
        (output_path / "manifest.json").read_bytes()
    ).hexdigest()
    assert report["artifacts"]["weights_sha256"] == hashlib.sha256(
        (output_path / "weights.npz").read_bytes()
    ).hexdigest()
    assert report["artifacts"]["generated_c_sha256"] == hashlib.sha256(
        source_path.read_bytes()
    ).hexdigest()
    assert report["training_revision"] == "test-revision"
    assert report["split"]["groups"] == {
        "train": ["bg-1", "impact-1"],
        "validation": ["bg-2", "impact-2"],
        "test": ["bg-3", "impact-3"],
    }


def test_frozen_split_rejects_source_or_record_hash_tampering(tmp_path: Path) -> None:
    _, labels, groups, event_ids = _dataset()
    split_path = tmp_path / "split_manifest.json"
    source_manifest_path = _write_split_manifest(split_path)

    source_manifest_path.write_text("tampered\n", encoding="utf-8")
    with pytest.raises(ValueError, match="source manifest hash"):
        load_frozen_split(
            split_path,
            source_manifest_path=source_manifest_path,
            source_hashes=tuple(
                hashlib.sha256(f"source-{event_id}".encode("utf-8")).hexdigest()
                for event_id in event_ids
            ),
            event_ids=event_ids,
            labels=labels,
            groups=groups,
        )

    source_manifest_path = _write_split_manifest(tmp_path / "split_manifest_2.json")
    tampered_path = tmp_path / "split_manifest_tampered.json"
    tampered = json.loads((tmp_path / "split_manifest_2.json").read_text(encoding="utf-8"))
    tampered["partitions"]["train"][0]["source_sha256"] = "b" * 64
    tampered_path.write_text(json.dumps(tampered), encoding="utf-8")
    with pytest.raises(ValueError, match="provenance"):
        load_frozen_split(
            tampered_path,
            source_manifest_path=source_manifest_path,
            source_hashes=("b" * 64,) + tuple(
                hashlib.sha256(f"source-{event_id}".encode("utf-8")).hexdigest()
                for event_id in event_ids[1:]
            ),
            event_ids=event_ids,
            labels=labels,
            groups=groups,
        )
