from __future__ import annotations

import csv
from pathlib import Path

import numpy as np

from ai.src.dataset_contract import FEATURE_NAMES
from ai.src.golden_vectors import load_golden_vectors
from ai.src.model_package import load_model_package, predict_quantized


PROJECT_ROOT = Path(__file__).resolve().parents[2]
FIXTURE = PROJECT_ROOT / "ai/tests/fixtures/ai_golden_vectors.csv"
MODEL_PACKAGE = PROJECT_ROOT / "ai/artifacts/pilot-v1/model"
FOUR_CLASS_NAMES = (
    "background",
    "impact",
    "continuous_vibration",
    "drop",
)


def test_host_quantized_reference_matches_the_shared_golden_vectors() -> None:
    package = load_model_package(MODEL_PACKAGE)

    for vector in load_golden_vectors(FIXTURE):
        logits, probabilities, class_index, confidence = predict_quantized(
            package,
            vector.features,
        )
        np.testing.assert_allclose(logits, vector.logits, rtol=0.0, atol=1e-4)
        np.testing.assert_allclose(
            probabilities,
            vector.probabilities,
            rtol=0.0,
            atol=1e-4,
        )
        assert class_index == vector.class_index
        np.testing.assert_allclose(
            confidence,
            vector.confidence,
            rtol=0.0,
            atol=1e-4,
        )


def test_golden_vector_columns_follow_the_configured_class_order(
    tmp_path: Path,
) -> None:
    path = tmp_path / "vectors.csv"
    columns = (
        "name",
        *FEATURE_NAMES,
        *(f"logit_{name}" for name in FOUR_CLASS_NAMES),
        *(f"probability_{name}" for name in FOUR_CLASS_NAMES),
        "class_index",
        "confidence",
    )
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        row = {field: "0.0" for field in columns}
        row.update({"name": "four-class", "class_index": "3", "confidence": "1.0"})
        writer.writerow(row)

    vectors = load_golden_vectors(path, class_names=FOUR_CLASS_NAMES)

    assert vectors[0].logits.shape == (4,)
    assert vectors[0].probabilities.shape == (4,)
    assert vectors[0].class_index == 3
