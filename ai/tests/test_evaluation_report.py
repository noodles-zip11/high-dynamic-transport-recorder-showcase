from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np
import pytest

from ai.src.dataset_contract import FEATURE_NAMES
from ai.src.generate_evaluation_report import build_evaluation_bundle
from ai.src.model_package import save_model_package
from ai.src.train_model import MLPModel


PROJECT_ROOT = Path(__file__).resolve().parents[2]


def test_cli_writes_a_model_baseline_evaluation_bundle(tmp_path: Path) -> None:
    shared = {
        "truth": ["background", "background", "impact", "impact"],
        "session_ids": ["s1", "s2", "s3", "s4"],
        "event_ids": [1, 2, 3, 4],
        "class_names": ["background", "impact"],
        "background_label": "background",
        "background_duration_hours": 2.0,
    }
    model_path = tmp_path / "model-predictions.json"
    baseline_path = tmp_path / "baseline-predictions.json"
    model_package = tmp_path / "model-package"
    save_model_package(
        MLPModel(
            class_names=("background", "impact"),
            feature_names=FEATURE_NAMES,
            feature_mean=np.zeros(6),
            feature_scale=np.ones(6),
            weights1=np.zeros((6, 2)),
            bias1=np.zeros(2),
            weights2=np.zeros((2, 2)),
            bias2=np.zeros(2),
        ),
        model_package,
        dataset_manifest_sha256="a" * 64,
        training_revision="test-revision",
    )
    baseline_config = tmp_path / "baseline.yaml"
    split_manifest = tmp_path / "split.json"
    features = tmp_path / "features.npy"
    feature_metadata = tmp_path / "metadata.json"
    prediction_code = tmp_path / "prediction.py"
    baseline_config.write_text(
        """version: 1
allowed_labels: [background, impact]
baseline:
  feature_version: counts_v1
  default_label: background
  rules: []
evaluation:
  background_duration_hours: 2.0
""",
        encoding="utf-8",
    )
    split_manifest.write_text(json.dumps({
        "version": 1,
        "source_manifest_sha256": "a" * 64,
        "partitions": {
            "train": [],
            "validation": [],
            "test": [
                {
                    "session_id": session_id,
                    "event_id": event_id,
                    "label": label,
                    "source_sha256": "b" * 64,
                }
                for session_id, event_id, label in zip(
                    shared["session_ids"],
                    shared["event_ids"],
                    shared["truth"],
                    strict=True,
                )
            ],
        },
    }), encoding="utf-8")
    np.save(features, np.zeros((4, 6), dtype=np.float64))
    feature_metadata.write_text(json.dumps({
        "event_count": 4,
        "features_sha256": hashlib.sha256(features.read_bytes()).hexdigest(),
        "manifest_sha256": "a" * 64,
        "rows": [
            {
                "session_id": session_id,
                "event_id": event_id,
                "label": label,
                "source_sha256": "b" * 64,
            }
            for session_id, event_id, label in zip(
                shared["session_ids"],
                shared["event_ids"],
                shared["truth"],
                strict=True,
            )
        ],
    }), encoding="utf-8")
    prediction_code.write_text("# predictor\n", encoding="utf-8")
    provenance = {
        "model_manifest_sha256": hashlib.sha256(
            (model_package / "manifest.json").read_bytes()
        ).hexdigest(),
        "model_weights_sha256": hashlib.sha256(
            (model_package / "weights.npz").read_bytes()
        ).hexdigest(),
        "baseline_config_sha256": hashlib.sha256(
            baseline_config.read_bytes()
        ).hexdigest(),
        "split_manifest_sha256": hashlib.sha256(
            split_manifest.read_bytes()
        ).hexdigest(),
        "feature_matrix_sha256": hashlib.sha256(features.read_bytes()).hexdigest(),
        "feature_metadata_sha256": hashlib.sha256(
            feature_metadata.read_bytes()
        ).hexdigest(),
        "prediction_code_sha256": hashlib.sha256(
            prediction_code.read_bytes()
        ).hexdigest(),
    }
    output_path = tmp_path / "evaluation-report.json"
    model_path.write_text(json.dumps({
        **shared,
        "provenance": provenance,
        "predictions": ["background", "background", "impact", "background"],
    }), encoding="utf-8")
    baseline_path.write_text(json.dumps({
        **shared,
        "provenance": provenance,
        "predictions": ["impact", "background", "impact", "background"],
    }), encoding="utf-8")

    completed = subprocess.run(
        [
            sys.executable,
            "-m",
            "ai.src.generate_evaluation_report",
            "--model-input",
            str(model_path),
            "--baseline-input",
            str(baseline_path),
            "--model-package",
            str(model_package),
            "--baseline-config",
            str(baseline_config),
            "--split-manifest",
            str(split_manifest),
            "--features",
            str(features),
            "--feature-metadata",
            str(feature_metadata),
            "--prediction-code",
            str(prediction_code),
            "--output",
            str(output_path),
        ],
        cwd=PROJECT_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )

    assert completed.returncode == 0, completed.stderr
    report = json.loads(output_path.read_text(encoding="utf-8"))
    assert report["model"]["per_class"]["impact"]["recall"] == 0.5
    assert report["baseline"]["false_positives_per_hour"] == 0.5
    assert "comparison" in report
    assert report["provenance"]["model_input_sha256"]
    assert report["provenance"]["prediction_code_sha256"] == provenance[
        "prediction_code_sha256"
    ]

    tampered_model = json.loads(model_path.read_text(encoding="utf-8"))
    tampered_model["event_ids"][0] = 99
    model_path.write_text(json.dumps(tampered_model), encoding="utf-8")
    with pytest.raises(ValueError, match="frozen test identities"):
        build_evaluation_bundle(
            model_path,
            baseline_path,
            model_package=model_package,
            baseline_config=baseline_config,
            split_manifest=split_manifest,
            features=features,
            feature_metadata=feature_metadata,
            prediction_code=prediction_code,
        )
