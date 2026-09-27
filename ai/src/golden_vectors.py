"""Load the checked-in host/MCU AI golden-vector fixture."""

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

import numpy as np

from ai.src.dataset_contract import (
    FEATURE_NAMES,
    LEGACY_CLASS_NAMES,
    validate_class_names,
)


@dataclass(frozen=True)
class GoldenVector:
    name: str
    features: np.ndarray
    logits: np.ndarray
    probabilities: np.ndarray
    class_index: int
    confidence: float


def load_golden_vectors(
    path: Path,
    *,
    class_names: Sequence[str] | None = None,
) -> tuple[GoldenVector, ...]:
    names = validate_class_names(
        LEGACY_CLASS_NAMES if class_names is None else tuple(class_names)
    )
    expected_columns = (
        "name",
        *FEATURE_NAMES,
        *(f"logit_{name}" for name in names),
        *(f"probability_{name}" for name in names),
        "class_index",
        "confidence",
    )
    try:
        with path.resolve().open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if tuple(reader.fieldnames or ()) != expected_columns:
                raise ValueError("golden-vector columns do not match counts_v1")
            vectors: list[GoldenVector] = []
            for row in reader:
                name = str(row["name"])
                if not name:
                    raise ValueError("golden-vector names must be nonempty")
                features = np.asarray([
                    float(row[field]) for field in FEATURE_NAMES
                ], dtype=np.float32)
                logits = np.asarray([
                    float(row[f"logit_{class_name}"])
                    for class_name in names
                ], dtype=np.float32)
                probabilities = np.asarray([
                    float(row[f"probability_{class_name}"])
                    for class_name in names
                ], dtype=np.float32)
                class_index = int(row["class_index"])
                confidence = float(row["confidence"])
                if not np.all(np.isfinite(features)):
                    raise ValueError(f"golden vector {name!r} has invalid features")
                if not np.all(np.isfinite(logits)):
                    raise ValueError(f"golden vector {name!r} has invalid logits")
                if (
                    not np.all(np.isfinite(probabilities))
                    or not 0 <= class_index < len(names)
                ):
                    raise ValueError(f"golden vector {name!r} has invalid prediction")
                vectors.append(GoldenVector(
                    name=name,
                    features=features,
                    logits=logits,
                    probabilities=probabilities,
                    class_index=class_index,
                    confidence=confidence,
                ))
    except (OSError, KeyError, TypeError, ValueError, csv.Error) as error:
        if isinstance(error, ValueError) and str(error).startswith("golden-vector"):
            raise
        raise ValueError("invalid AI golden-vector fixture") from error
    if not vectors:
        raise ValueError("AI golden-vector fixture is empty")
    if len({vector.name for vector in vectors}) != len(vectors):
        raise ValueError("AI golden-vector names must be unique")
    return tuple(vectors)
