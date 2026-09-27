"""Deterministic, dependency-light trainer for the first TinyML MLP."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
from typing import Mapping, Sequence

import numpy as np
import yaml

from ai.src.dataset_contract import (
    FEATURE_NAMES,
    LEGACY_CLASS_NAMES,
    validate_class_names,
)


# Kept as the historical default so existing 2-class artifacts and imports
# remain readable; new four-class runs pass the configured names explicitly.
CLASS_NAMES = LEGACY_CLASS_NAMES
PARTITIONS = ("train", "validation", "test")


@dataclass(frozen=True)
class MLPTrainingConfig:
    seed: int = 20260816
    hidden_units: int = 8
    epochs: int = 400
    learning_rate: float = 0.03
    l2: float = 1e-4

    def validate(self) -> None:
        if self.hidden_units <= 0 or self.epochs <= 0:
            raise ValueError("MLP hidden_units and epochs must be positive")
        if self.learning_rate <= 0.0 or self.l2 < 0.0:
            raise ValueError("MLP learning_rate must be positive and l2 nonnegative")


@dataclass(frozen=True)
class MLPModel:
    """Float reference model; package export adds quantized tensors."""

    class_names: tuple[str, ...]
    feature_names: tuple[str, ...]
    feature_mean: np.ndarray
    feature_scale: np.ndarray
    weights1: np.ndarray
    bias1: np.ndarray
    weights2: np.ndarray
    bias2: np.ndarray
    input_scale: float = 1.0 / 127.0
    hidden_scale: float = 1.0 / 127.0

    def predict_logits(self, features: np.ndarray) -> np.ndarray:
        values = _validate_features(features)
        normalized = (values - self.feature_mean) / self.feature_scale
        hidden = np.maximum(0.0, normalized @ self.weights1 + self.bias1)
        return hidden @ self.weights2 + self.bias2

    def predict_proba(self, features: np.ndarray) -> np.ndarray:
        logits = self.predict_logits(features)
        shifted = logits - np.max(logits, axis=1, keepdims=True)
        exponent = np.exp(shifted)
        return exponent / np.sum(exponent, axis=1, keepdims=True)

    def predict(self, features: np.ndarray) -> tuple[str, ...]:
        indices = np.argmax(self.predict_logits(features), axis=1)
        return tuple(self.class_names[int(index)] for index in indices)


@dataclass(frozen=True)
class TrainingResult:
    model: MLPModel
    split: dict[str, tuple[int, ...]]
    metrics: dict[str, float]


def train_mlp(
    features: np.ndarray,
    labels: Sequence[str],
    groups: Sequence[str],
    *,
    config: MLPTrainingConfig | None = None,
    split: Mapping[str, Sequence[int]] | None = None,
    class_names: Sequence[str] | None = None,
) -> TrainingResult:
    """Train a tiny ReLU MLP using an optional frozen index split."""

    config = config or MLPTrainingConfig()
    config.validate()
    values = _validate_features(features)
    names = validate_class_names(
        CLASS_NAMES if class_names is None else tuple(class_names)
    )
    labels_tuple = tuple(str(label) for label in labels)
    groups_tuple = tuple(str(group) for group in groups)
    if len(labels_tuple) != len(values) or len(groups_tuple) != len(values):
        raise ValueError("features, labels and groups must have equal lengths")
    if not values.shape[0]:
        raise ValueError("training data must be nonempty")
    unknown = sorted(set(labels_tuple) - set(names))
    if unknown:
        raise ValueError(f"unknown class label(s): {', '.join(unknown)}")
    if split is None:
        split = _split_by_group(
            labels_tuple, groups_tuple, seed=config.seed, class_names=names
        )
    else:
        split = _validate_split_indices(split, len(values))
    train_indices = np.asarray(split["train"], dtype=np.int64)
    train_values = values[train_indices]
    train_labels = np.asarray(
        [names.index(labels_tuple[index]) for index in train_indices],
        dtype=np.int64,
    )
    feature_mean = np.mean(train_values, axis=0, dtype=np.float64)
    feature_scale = np.std(train_values, axis=0, dtype=np.float64)
    feature_scale = np.where(feature_scale < 1e-12, 1.0, feature_scale)
    normalized = (train_values - feature_mean) / feature_scale

    rng = np.random.default_rng(config.seed)
    hidden_units = config.hidden_units
    weights1 = rng.normal(0.0, 0.15, size=(len(FEATURE_NAMES), hidden_units))
    bias1 = np.zeros(hidden_units, dtype=np.float64)
    weights2 = rng.normal(0.0, 0.15, size=(hidden_units, len(names)))
    bias2 = np.zeros(len(names), dtype=np.float64)

    for _ in range(config.epochs):
        hidden_pre = normalized @ weights1 + bias1
        hidden = np.maximum(0.0, hidden_pre)
        logits = hidden @ weights2 + bias2
        probabilities = _softmax(logits)
        errors = probabilities
        errors[np.arange(len(train_labels)), train_labels] -= 1.0
        errors /= len(train_labels)

        grad_weights2 = hidden.T @ errors + config.l2 * weights2
        grad_bias2 = np.sum(errors, axis=0)
        hidden_gradient = (errors @ weights2.T) * (hidden_pre > 0.0)
        grad_weights1 = normalized.T @ hidden_gradient + config.l2 * weights1
        grad_bias1 = np.sum(hidden_gradient, axis=0)

        weights1 -= config.learning_rate * grad_weights1
        bias1 -= config.learning_rate * grad_bias1
        weights2 -= config.learning_rate * grad_weights2
        bias2 -= config.learning_rate * grad_bias2

    model = MLPModel(
        class_names=names,
        feature_names=FEATURE_NAMES,
        feature_mean=feature_mean,
        feature_scale=feature_scale,
        weights1=weights1,
        bias1=bias1,
        weights2=weights2,
        bias2=bias2,
        input_scale=_activation_scale(normalized),
        hidden_scale=_activation_scale(
            np.maximum(0.0, normalized @ weights1 + bias1)
        ),
    )
    metrics = {
        f"{partition}_accuracy": _accuracy(
            model,
            values[np.asarray(indices, dtype=np.int64)],
            tuple(labels_tuple[index] for index in indices),
        )
        for partition, indices in split.items()
    }
    return TrainingResult(model=model, split=split, metrics=metrics)


def load_frozen_split(
    path: Path,
    *,
    source_manifest_path: Path,
    source_hashes: Sequence[str],
    event_ids: Sequence[int],
    labels: Sequence[str],
    groups: Sequence[str],
    class_names: Sequence[str] | None = None,
) -> dict[str, tuple[int, ...]]:
    """Resolve a versioned split manifest to feature-row indices.

    The manifest is authoritative.  Every feature row must match exactly one
    ``(session_id, event_id)`` record, including its label and session group.
    """

    try:
        raw = json.loads(path.resolve().read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("invalid frozen split manifest") from error
    if not isinstance(raw, dict) or raw.get("version") != 1:
        raise ValueError("unsupported frozen split manifest version")
    source_manifest_sha256 = raw.get("source_manifest_sha256")
    dataset_hash = raw.get("dataset_hash")
    if not _is_sha256(source_manifest_sha256) or not _is_sha256(dataset_hash):
        raise ValueError("frozen split manifest hashes must be SHA-256 values")
    try:
        actual_source_manifest_sha256 = _sha256_file(source_manifest_path)
    except OSError as error:
        raise ValueError("source dataset manifest cannot be read") from error
    if actual_source_manifest_sha256 != str(source_manifest_sha256).lower():
        raise ValueError("source manifest hash does not match frozen split")
    source_records = _load_source_manifest_records(source_manifest_path)
    grouping = raw.get("grouping", "session_id")
    if not isinstance(raw.get("seed"), int) or grouping not in {
        "session_id",
        "pilot_event_stratified",
    }:
        raise ValueError("frozen split manifest metadata is invalid")
    within_session_pilot = raw.get("within_session_pilot", False)
    if grouping == "pilot_event_stratified":
        if within_session_pilot is not True:
            raise ValueError(
                "pilot_event_stratified splits must declare "
                "within_session_pilot=true"
            )
        warning = raw.get("generalization_warning")
        if not isinstance(warning, str) or (
            "does not represent independent-session generalization" not in warning
        ):
            raise ValueError(
                "pilot_event_stratified splits must declare their "
                "generalization warning"
            )
    elif within_session_pilot not in (False, None):
        raise ValueError("session_id splits cannot declare within_session_pilot")

    event_ids_tuple = tuple(int(event_id) for event_id in event_ids)
    labels_tuple = tuple(str(label) for label in labels)
    groups_tuple = tuple(str(group) for group in groups)
    expected_class_names = (
        validate_class_names(class_names)
        if class_names is not None
        else (
            validate_class_names(raw["class_names"])
            if raw.get("class_names") is not None
            else tuple(sorted(set(labels_tuple)))
        )
    )
    if raw.get("class_names") is not None and list(expected_class_names) != raw.get(
        "class_names"
    ):
        raise ValueError("frozen split class order does not match the model contract")
    source_hashes_tuple = tuple(str(source_hash).lower() for source_hash in source_hashes)
    row_count = len(event_ids_tuple)
    if (
        len(labels_tuple) != row_count
        or len(groups_tuple) != row_count
        or len(source_hashes_tuple) != row_count
    ):
        raise ValueError("event_ids, labels, groups and source hashes must have equal lengths")
    if len(set((group, event_id) for group, event_id in zip(groups_tuple, event_ids_tuple))) != row_count:
        raise ValueError("feature rows must have unique (session_id, event_id) identities")

    row_by_identity = {
        (group, event_id): index
        for index, (group, event_id) in enumerate(zip(groups_tuple, event_ids_tuple))
    }
    for identity, index in row_by_identity.items():
        source_record = source_records.get(identity)
        if source_record is None:
            raise ValueError(
                "feature row is missing from the source dataset manifest"
            )
        source_label, source_sha256 = source_record
        if (
            source_label != labels_tuple[index]
            or source_sha256 != source_hashes_tuple[index]
        ):
            raise ValueError(
                "feature row provenance does not match the source dataset manifest"
            )
    partitions_raw = raw.get("partitions")
    if not isinstance(partitions_raw, dict):
        raise ValueError("frozen split manifest partitions are missing")
    resolved: dict[str, tuple[int, ...]] = {}
    seen_indices: set[int] = set()
    dataset_records: list[dict[str, object]] = []
    for partition in PARTITIONS:
        records = partitions_raw.get(partition)
        if not isinstance(records, list):
            raise ValueError(f"frozen split partition {partition!r} is invalid")
        indices: list[int] = []
        for record in records:
            if not isinstance(record, dict):
                raise ValueError("frozen split record is invalid")
            try:
                identity = (str(record["session_id"]), int(record["event_id"]))
                index = row_by_identity[identity]
            except (KeyError, TypeError, ValueError) as error:
                raise ValueError(
                    "frozen split record does not match feature rows"
                ) from error
            if index in seen_indices:
                raise ValueError("frozen split assigns a feature row more than once")
            if str(record.get("label")) != labels_tuple[index]:
                raise ValueError("frozen split label does not match feature rows")
            source_sha256 = record.get("source_sha256")
            if not _is_sha256(source_sha256):
                raise ValueError("frozen split record has an invalid source_sha256")
            if str(source_sha256).lower() != source_hashes_tuple[index]:
                raise ValueError("frozen split source hash does not match feature metadata")
            seen_indices.add(index)
            indices.append(index)
            dataset_records.append({
                "event_id": identity[1],
                "label": labels_tuple[index],
                "session_id": identity[0],
                "source_sha256": str(source_sha256).lower(),
            })
        resolved[partition] = tuple(sorted(indices))

    if seen_indices != set(range(row_count)):
        raise ValueError("frozen split does not cover every feature row exactly once")
    serialized_records = json.dumps(
        sorted(dataset_records, key=lambda record: (
            str(record["session_id"]), int(record["event_id"]),
        )),
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    actual_dataset_hash = hashlib.sha256(serialized_records).hexdigest()
    if actual_dataset_hash != str(dataset_hash).lower():
        raise ValueError("frozen split dataset_hash does not match its records")
    for label in expected_class_names:
        if label not in labels_tuple:
            raise ValueError(f"frozen split is missing class {label!r}")
        if any(
            not any(labels_tuple[index] == label for index in indices)
            for indices in resolved.values()
        ):
            raise ValueError(
                f"frozen split must contain class {label!r} in every partition"
            )
    session_partitions = raw.get("session_partitions")
    if grouping == "session_id":
        if not isinstance(session_partitions, dict):
            raise ValueError("frozen split session_partitions are missing")
        if set(session_partitions) != set(groups_tuple):
            raise ValueError("frozen split session_partitions do not match feature rows")
        for partition, indices in resolved.items():
            for index in indices:
                if session_partitions.get(groups_tuple[index]) != partition:
                    raise ValueError("frozen split session assignment is inconsistent")
    return resolved


def load_feature_metadata(
    path: Path,
    *,
    features_path: Path,
    labels_path: Path,
    groups_path: Path,
    event_ids_path: Path,
    source_manifest_sha256: str,
    event_ids: Sequence[int],
    labels: Sequence[str],
    groups: Sequence[str],
    class_names: Sequence[str] | None = None,
) -> tuple[str, ...]:
    """Validate the immutable row metadata emitted by ``build_feature_dataset``."""

    try:
        raw = json.loads(path.resolve().read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("invalid feature metadata") from error
    if not isinstance(raw, dict) or raw.get("metadata_version") != 1:
        raise ValueError("unsupported feature metadata version")
    if raw.get("manifest_sha256") != str(source_manifest_sha256).lower():
        raise ValueError("feature metadata manifest hash does not match frozen split")
    if raw.get("feature_names") != list(FEATURE_NAMES):
        raise ValueError("feature metadata feature order does not match counts_v1")
    names = validate_class_names(
        CLASS_NAMES if class_names is None else tuple(class_names)
    )
    if raw.get("class_names") != list(names):
        raise ValueError("feature metadata class order does not match model contract")

    for field, file_path in (
        ("features_sha256", features_path),
        ("labels_sha256", labels_path),
        ("groups_sha256", groups_path),
        ("event_ids_sha256", event_ids_path),
    ):
        expected = raw.get(field)
        if not _is_sha256(expected) or _sha256_file(file_path) != str(expected).lower():
            raise ValueError(f"feature metadata {field} does not match input file")

    event_ids_tuple = tuple(int(event_id) for event_id in event_ids)
    labels_tuple = tuple(str(label) for label in labels)
    groups_tuple = tuple(str(group) for group in groups)
    row_count = len(event_ids_tuple)
    if (
        len(labels_tuple) != row_count
        or len(groups_tuple) != row_count
        or raw.get("event_count") != row_count
    ):
        raise ValueError("feature metadata row count does not match input files")
    rows = raw.get("rows")
    if not isinstance(rows, list) or len(rows) != row_count:
        raise ValueError("feature metadata rows are missing or incomplete")
    source_hashes: list[str] = []
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError("feature metadata row is invalid")
        source_hash = row.get("source_sha256")
        if (
            str(row.get("session_id")) != groups_tuple[index]
            or int(row.get("event_id")) != event_ids_tuple[index]
            or str(row.get("label")) != labels_tuple[index]
            or not _is_sha256(source_hash)
        ):
            raise ValueError("feature metadata row does not match input files")
        source_hashes.append(str(source_hash).lower())
    return tuple(source_hashes)


def _validate_split_indices(
    split: Mapping[str, Sequence[int]],
    row_count: int,
) -> dict[str, tuple[int, ...]]:
    if set(split) != set(PARTITIONS):
        raise ValueError("split must contain train, validation and test partitions")
    resolved: dict[str, tuple[int, ...]] = {}
    seen: set[int] = set()
    for partition in PARTITIONS:
        indices = tuple(sorted(int(index) for index in split[partition]))
        if any(index < 0 or index >= row_count for index in indices):
            raise ValueError("split contains an out-of-range feature index")
        if seen.intersection(indices):
            raise ValueError("split partitions overlap")
        seen.update(indices)
        resolved[partition] = indices
    if seen != set(range(row_count)):
        raise ValueError("split does not cover every feature row exactly once")
    if any(not resolved[partition] for partition in PARTITIONS):
        raise ValueError("split partitions must not be empty")
    return resolved


def _is_sha256(value: object) -> bool:
    if not isinstance(value, str) or len(value) != 64:
        return False
    return all(character in "0123456789abcdefABCDEF" for character in value)


def _load_source_manifest_records(
    path: Path,
) -> dict[tuple[str, int], tuple[str, str]]:
    """Load and verify source event identities and hashes from the manifest."""

    path = path.resolve()
    try:
        raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as error:
        raise ValueError("invalid source dataset manifest") from error
    if not isinstance(raw, dict) or raw.get("version") != 1:
        raise ValueError("unsupported source dataset manifest version")
    dataset_root_value = raw.get("dataset_root")
    events = raw.get("events")
    if not isinstance(dataset_root_value, str) or not isinstance(events, list):
        raise ValueError("source dataset manifest is missing dataset_root or events")
    dataset_root = (path.parent / dataset_root_value).resolve()
    records: dict[tuple[str, int], tuple[str, str]] = {}
    for event in events:
        if not isinstance(event, dict):
            raise ValueError("source dataset manifest event is invalid")
        try:
            identity = (str(event["session_id"]), int(event["event_id"]))
            source_value = str(event["source_path"])
            source_sha256 = str(event["source_sha256"]).lower()
            label = str(event["label"])
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError("source dataset manifest event is incomplete") from error
        if not source_value or not label or not _is_sha256(source_sha256):
            raise ValueError("source dataset manifest event has invalid provenance")
        source_path = (dataset_root / source_value).resolve()
        if not source_path.is_relative_to(dataset_root) or not source_path.is_file():
            raise ValueError(
                "source dataset manifest event points to a missing or unsafe file"
            )
        if _sha256_file(source_path) != source_sha256:
            raise ValueError(
                "source dataset manifest event hash does not match its file"
            )
        if identity in records:
            raise ValueError("source dataset manifest contains duplicate event identity")
        records[identity] = (label, source_sha256)
    return records


def _split_by_group(
    labels: tuple[str, ...],
    groups: tuple[str, ...],
    *,
    seed: int,
    class_names: Sequence[str] = CLASS_NAMES,
) -> dict[str, tuple[int, ...]]:
    group_labels: dict[str, str] = {}
    group_indices: dict[str, list[int]] = {}
    for index, (label, group) in enumerate(zip(labels, groups)):
        if not group:
            raise ValueError("group identifiers must be nonempty")
        previous = group_labels.setdefault(group, label)
        if previous != label:
            raise ValueError(f"group {group!r} contains multiple labels")
        group_indices.setdefault(group, []).append(index)

    rng = np.random.default_rng(seed)
    partitions: dict[str, list[int]] = {"train": [], "validation": [], "test": []}
    for label in class_names:
        label_groups = sorted(
            group for group, group_label in group_labels.items() if group_label == label
        )
        if len(label_groups) < 3:
            raise ValueError("need at least 3 groups per class")
        permutation = rng.permutation(len(label_groups))
        shuffled = [label_groups[int(index)] for index in permutation]
        test_count = max(1, round(len(shuffled) * 0.15))
        validation_count = max(1, round(len(shuffled) * 0.15))
        if test_count + validation_count >= len(shuffled):
            test_count = validation_count = 1
        selected = {
            "test": shuffled[:test_count],
            "validation": shuffled[test_count:test_count + validation_count],
            "train": shuffled[test_count + validation_count:],
        }
        for partition, selected_groups in selected.items():
            for group in selected_groups:
                partitions[partition].extend(group_indices[group])

    return {
        partition: tuple(sorted(indices))
        for partition, indices in partitions.items()
    }


def _validate_features(features: np.ndarray) -> np.ndarray:
    values = np.asarray(features, dtype=np.float64)
    if values.ndim != 2 or values.shape[1] != len(FEATURE_NAMES):
        raise ValueError(f"features must have shape (n, {len(FEATURE_NAMES)})")
    if not np.all(np.isfinite(values)):
        raise ValueError("features must contain only finite values")
    return values


def _softmax(logits: np.ndarray) -> np.ndarray:
    shifted = logits - np.max(logits, axis=1, keepdims=True)
    exponent = np.exp(shifted)
    return exponent / np.sum(exponent, axis=1, keepdims=True)


def _accuracy(model: MLPModel, features: np.ndarray, labels: tuple[str, ...]) -> float:
    if not labels:
        return 0.0
    predictions = model.predict(features)
    return float(sum(prediction == label for prediction, label in zip(predictions, labels)) / len(labels))


def _activation_scale(values: np.ndarray) -> float:
    maximum = float(np.max(np.abs(values)))
    return maximum / 127.0 if maximum > 0.0 else 1.0 / 127.0


def _sha256_file(path: Path) -> str:
    return hashlib.sha256(path.resolve().read_bytes()).hexdigest()


def _build_training_report(
    *,
    result: TrainingResult,
    config: MLPTrainingConfig,
    training_revision: str,
    features_path: Path,
    labels_path: Path,
    groups_path: Path,
    event_ids_path: Path,
    feature_metadata_path: Path,
    split_path: Path,
    split_manifest: Mapping[str, object],
    model_directory: Path,
    generated_header: Path,
    generated_source: Path,
) -> dict[str, object]:
    model_directory = model_directory.resolve()
    model_manifest_path = model_directory / "manifest.json"
    weights_path = model_directory / "weights.npz"
    code_directory = Path(__file__).resolve().parent
    code_paths = {
        "ai/src/export_c_model.py": code_directory / "export_c_model.py",
        "ai/src/model_package.py": code_directory / "model_package.py",
        "ai/src/split_dataset.py": code_directory / "split_dataset.py",
        "ai/src/train_model.py": code_directory / "train_model.py",
    }
    groups = tuple(json.loads(groups_path.resolve().read_text(encoding="utf-8")))
    groups_by_partition = {
        partition: sorted({groups[index] for index in indices})
        for partition, indices in result.split.items()
    }
    return {
        "report_version": 1,
        "class_names": list(result.model.class_names),
        "training_revision": training_revision,
        "training_config": asdict(config),
        "metrics": result.metrics,
        "split": {
            "seed": split_manifest["seed"],
            "grouping": split_manifest.get("grouping", "session_id"),
            "within_session_pilot": bool(
                split_manifest.get("within_session_pilot", False)
            ),
            "generalization_warning": split_manifest.get(
                "generalization_warning"
            ),
            "dataset_hash": split_manifest["dataset_hash"],
            "groups": groups_by_partition,
            "row_counts": {
                partition: len(indices)
                for partition, indices in result.split.items()
            },
        },
        "provenance": {
            "source_manifest_sha256": split_manifest["source_manifest_sha256"],
            "dataset_hash": split_manifest["dataset_hash"],
            "split_manifest_sha256": _sha256_file(split_path),
            "feature_matrix_sha256": _sha256_file(features_path),
            "labels_sha256": _sha256_file(labels_path),
            "groups_sha256": _sha256_file(groups_path),
            "event_ids_sha256": _sha256_file(event_ids_path),
            "feature_metadata_sha256": _sha256_file(feature_metadata_path),
        },
        "training_code": {
            path: _sha256_file(file_path)
            for path, file_path in code_paths.items()
        },
        "artifacts": {
            "model_manifest_sha256": _sha256_file(model_manifest_path),
            "weights_sha256": _sha256_file(weights_path),
            "generated_header_sha256": _sha256_file(generated_header),
            "generated_c_sha256": _sha256_file(generated_source),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--features", type=Path, required=True)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--groups", type=Path, required=True)
    parser.add_argument("--event-ids", type=Path, required=True)
    parser.add_argument("--feature-metadata", type=Path, required=True)
    parser.add_argument("--split-manifest", type=Path, required=True)
    parser.add_argument("--dataset-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--generated-header", type=Path, required=True)
    parser.add_argument("--generated-source", type=Path, required=True)
    parser.add_argument("--dataset-manifest-sha256")
    parser.add_argument("--training-revision", required=True)
    parser.add_argument("--seed", type=int, default=20260816)
    parser.add_argument("--hidden-units", type=int, default=8)
    parser.add_argument("--epochs", type=int, default=400)
    args = parser.parse_args()

    features = np.load(args.features, allow_pickle=False)
    labels = tuple(json.loads(args.labels.read_text(encoding="utf-8")))
    groups = tuple(json.loads(args.groups.read_text(encoding="utf-8")))
    event_ids = tuple(json.loads(args.event_ids.read_text(encoding="utf-8")))
    split_manifest = json.loads(
        args.split_manifest.resolve().read_text(encoding="utf-8")
    )
    feature_metadata = json.loads(
        args.feature_metadata.resolve().read_text(encoding="utf-8")
    )
    class_names = validate_class_names(
        feature_metadata.get("class_names", list(CLASS_NAMES))
    )
    dataset_manifest_sha256 = split_manifest["source_manifest_sha256"]
    if (
        args.dataset_manifest_sha256 is not None
        and args.dataset_manifest_sha256 != dataset_manifest_sha256
    ):
        raise ValueError(
            "dataset manifest hash does not match the frozen split manifest"
        )
    source_hashes = load_feature_metadata(
        args.feature_metadata,
        features_path=args.features,
        labels_path=args.labels,
        groups_path=args.groups,
        event_ids_path=args.event_ids,
        source_manifest_sha256=dataset_manifest_sha256,
        event_ids=event_ids,
        labels=labels,
        groups=groups,
        class_names=class_names,
    )
    frozen_split = load_frozen_split(
        args.split_manifest,
        source_manifest_path=args.dataset_manifest,
        source_hashes=source_hashes,
        event_ids=event_ids,
        labels=labels,
        groups=groups,
        class_names=class_names,
    )
    config = MLPTrainingConfig(
        seed=args.seed,
        hidden_units=args.hidden_units,
        epochs=args.epochs,
    )
    result = train_mlp(
        features,
        labels,
        groups,
        config=config,
        split=frozen_split,
        class_names=class_names,
    )
    from ai.src.model_package import save_model_package
    from ai.src.export_c_model import export_c_model

    save_model_package(
        result.model,
        args.output,
        dataset_manifest_sha256=dataset_manifest_sha256,
        training_revision=args.training_revision,
    )
    export_c_model(
        args.output,
        args.generated_header,
        args.generated_source,
    )
    report = _build_training_report(
        result=result,
        config=config,
        training_revision=args.training_revision,
        features_path=args.features,
        labels_path=args.labels,
        groups_path=args.groups,
        event_ids_path=args.event_ids,
        feature_metadata_path=args.feature_metadata,
        split_path=args.split_manifest,
        split_manifest=split_manifest,
        model_directory=args.output,
        generated_header=args.generated_header,
        generated_source=args.generated_source,
    )
    args.report.resolve().parent.mkdir(parents=True, exist_ok=True)
    args.report.resolve().write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({"report": str(args.report.resolve()), **report}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
