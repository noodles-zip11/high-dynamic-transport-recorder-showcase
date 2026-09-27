"""Build one hash-linked evaluation bundle for model/baseline predictions."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Sequence

import numpy as np
import yaml

from ai.src.evaluate import (
    _evaluate_payload,
    _require_comparable_inputs,
    compare_reports,
)
from ai.src.model_package import load_model_package
from ai.src.train_baseline import load_rule_baseline


def build_evaluation_bundle(
    model_input: Path,
    baseline_input: Path,
    *,
    model_package: Path,
    baseline_config: Path,
    split_manifest: Path,
    features: Path,
    feature_metadata: Path,
    prediction_code: Path,
) -> dict[str, object]:
    model_input = model_input.resolve()
    baseline_input = baseline_input.resolve()
    model_raw = json.loads(model_input.read_text(encoding="utf-8"))
    baseline_raw = json.loads(baseline_input.read_text(encoding="utf-8"))
    provenance = _evaluation_provenance(
        model_package=model_package,
        baseline_config=baseline_config,
        split_manifest=split_manifest,
        features=features,
        feature_metadata=feature_metadata,
        prediction_code=prediction_code,
    )
    _require_input_provenance(model_raw, provenance)
    _require_input_provenance(baseline_raw, provenance)
    _require_test_partition(
        model_raw,
        baseline_raw,
        split_manifest=split_manifest,
        features=features,
        feature_metadata=feature_metadata,
        baseline_config=baseline_config,
    )
    _require_comparable_inputs(model_raw, baseline_raw)
    model_report = _evaluate_payload(model_raw)
    baseline_report = _evaluate_payload(baseline_raw)
    comparison = compare_reports(
        baseline=baseline_report,
        candidate=model_report,
    )
    return {
        "report_version": 1,
        "provenance": {
            "model_input_sha256": _sha256_file(model_input),
            "baseline_input_sha256": _sha256_file(baseline_input),
            **provenance,
            "evidence_hash": model_report.evidence_hash,
        },
        "model": model_report.to_dict(),
        "baseline": baseline_report.to_dict(),
        "comparison": comparison.to_dict(),
    }


def _sha256_file(path: Path) -> str:
    return hashlib.sha256(path.resolve().read_bytes()).hexdigest()


def _evaluation_provenance(
    *,
    model_package: Path,
    baseline_config: Path,
    split_manifest: Path,
    features: Path,
    feature_metadata: Path,
    prediction_code: Path,
) -> dict[str, str]:
    model_package = model_package.resolve()
    manifest_path = model_package / "manifest.json"
    weights_path = model_package / "weights.npz"
    manifest_sha256 = _sha256_file(manifest_path)
    weights_sha256 = _sha256_file(weights_path)
    try:
        package = load_model_package(model_package)
        load_rule_baseline(baseline_config)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        split = json.loads(split_manifest.resolve().read_text(encoding="utf-8"))
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise ValueError("evaluation model, baseline, or split is invalid") from error
    if not isinstance(manifest, dict) or manifest.get("weights_sha256") != weights_sha256:
        raise ValueError("model manifest weights_sha256 does not match weights.npz")
    if (
        not isinstance(split, dict)
        or not isinstance(split.get("source_manifest_sha256"), str)
        or package.manifest.get("dataset_manifest_sha256")
        != split["source_manifest_sha256"]
    ):
        raise ValueError("model package is not bound to the evaluation split")
    return {
        "model_manifest_sha256": manifest_sha256,
        "model_weights_sha256": weights_sha256,
        "baseline_config_sha256": _sha256_file(baseline_config),
        "split_manifest_sha256": _sha256_file(split_manifest),
        "feature_matrix_sha256": _sha256_file(features),
        "feature_metadata_sha256": _sha256_file(feature_metadata),
        "prediction_code_sha256": _sha256_file(prediction_code),
    }


def _require_input_provenance(
    raw: object,
    expected: dict[str, str],
) -> None:
    if not isinstance(raw, dict) or raw.get("provenance") != expected:
        raise ValueError(
            "prediction input provenance must exactly match the evaluation evidence"
        )


def _require_test_partition(
    model_raw: object,
    baseline_raw: object,
    *,
    split_manifest: Path,
    features: Path,
    feature_metadata: Path,
    baseline_config: Path,
) -> None:
    """Require both prediction inputs to cover exactly the frozen test events."""

    try:
        split_raw = json.loads(split_manifest.resolve().read_text(encoding="utf-8"))
        metadata_raw = json.loads(
            feature_metadata.resolve().read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("evaluation split or feature metadata is invalid") from error
    if not isinstance(split_raw, dict) or split_raw.get("version") != 1:
        raise ValueError("evaluation split manifest version is unsupported")
    if not isinstance(metadata_raw, dict):
        raise ValueError("feature metadata must be a JSON object")
    partitions = split_raw.get("partitions")
    rows = metadata_raw.get("rows")
    test_records = partitions.get("test") if isinstance(partitions, dict) else None
    if not isinstance(test_records, list) or not isinstance(rows, list):
        raise ValueError("evaluation split or feature metadata is missing rows")
    if (
        metadata_raw.get("event_count") != len(rows)
        or metadata_raw.get("features_sha256") != _sha256_file(features)
        or metadata_raw.get("manifest_sha256")
        != split_raw.get("source_manifest_sha256")
    ):
        raise ValueError("feature metadata does not match the feature matrix")
    try:
        feature_matrix = np.asarray(np.load(features.resolve(), allow_pickle=False))
    except (OSError, ValueError) as error:
        raise ValueError("evaluation feature matrix is invalid") from error
    if feature_matrix.ndim != 2 or feature_matrix.shape[0] != len(rows):
        raise ValueError("feature matrix row count does not match feature metadata")

    test_order: list[tuple[str, int]] = []
    test_provenance: dict[tuple[str, int], tuple[str, str]] = {}
    for record in test_records:
        identity, label, source_sha256 = _provenance_identity_and_label(
            record, "split test"
        )
        if identity in test_provenance:
            raise ValueError("evaluation test split contains duplicate events")
        test_order.append(identity)
        test_provenance[identity] = (label, source_sha256)
    metadata_provenance: dict[tuple[str, int], tuple[str, str]] = {}
    for row in rows:
        identity, label, source_sha256 = _provenance_identity_and_label(
            row, "feature metadata"
        )
        if identity in metadata_provenance:
            raise ValueError("feature metadata contains duplicate events")
        metadata_provenance[identity] = (label, source_sha256)
    if any(
        metadata_provenance.get(identity) != provenance
        for identity, provenance in test_provenance.items()
    ):
        raise ValueError("evaluation test events do not match feature metadata")

    for prediction_input in (model_raw, baseline_raw):
        if not isinstance(prediction_input, dict):
            raise ValueError("prediction inputs must be JSON objects")
        session_ids = prediction_input.get("session_ids")
        event_ids = prediction_input.get("event_ids")
        truth = prediction_input.get("truth")
        if not isinstance(session_ids, list) or not isinstance(event_ids, list):
            raise ValueError("evaluation predictions must include session_ids and event_ids")
        if not isinstance(truth, list) or len(session_ids) != len(event_ids):
            raise ValueError("evaluation prediction identity arrays are invalid")
        if len(session_ids) != len(truth) or len(truth) != len(test_provenance):
            raise ValueError("evaluation predictions must cover the full test split")
        observed_order: list[tuple[str, int]] = []
        observed: dict[tuple[str, int], str] = {}
        for session_id, event_id, label in zip(
            session_ids, event_ids, truth, strict=True
        ):
            if (
                not isinstance(session_id, str)
                or not session_id
                or isinstance(event_id, bool)
                or not isinstance(event_id, int)
                or not isinstance(label, str)
            ):
                raise ValueError("evaluation prediction identity or truth is invalid")
            identity = (session_id, event_id)
            if identity in observed:
                raise ValueError("evaluation predictions contain duplicate events")
            observed_order.append(identity)
            observed[identity] = label
        expected_labels = {
            identity: provenance[0]
            for identity, provenance in test_provenance.items()
        }
        if observed_order != test_order or observed != expected_labels:
            raise ValueError(
                "evaluation predictions must match frozen test identities and labels"
            )
        configured_duration = _configured_background_duration(baseline_config)
        if prediction_input.get("background_duration_hours") != configured_duration:
            raise ValueError(
                "evaluation background duration does not match baseline configuration"
            )


def _configured_background_duration(path: Path) -> float:
    try:
        raw = yaml.safe_load(path.resolve().read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as error:
        raise ValueError("baseline configuration is invalid") from error
    evaluation = raw.get("evaluation") if isinstance(raw, dict) else None
    duration = evaluation.get("background_duration_hours") if isinstance(evaluation, dict) else None
    try:
        value = float(duration)
    except (TypeError, ValueError) as error:
        raise ValueError(
            "baseline configuration must declare background_duration_hours"
        ) from error
    if not math.isfinite(value) or value <= 0.0:
        raise ValueError(
            "baseline configuration background_duration_hours must be positive"
        )
    return value


def _provenance_identity_and_label(
    raw: object,
    source: str,
) -> tuple[tuple[str, int], str, str]:
    if not isinstance(raw, dict):
        raise ValueError(f"{source} record is invalid")
    session_id = raw.get("session_id")
    event_id = raw.get("event_id")
    label = raw.get("label")
    source_sha256 = raw.get("source_sha256")
    if (
        not isinstance(session_id, str)
        or not session_id
        or isinstance(event_id, bool)
        or not isinstance(event_id, int)
        or not isinstance(label, str)
        or not label
        or not _is_sha256(source_sha256)
    ):
        raise ValueError(f"{source} record identity or label is invalid")
    return (session_id, event_id), label, str(source_sha256).lower()


def _is_sha256(value: object) -> bool:
    if not isinstance(value, str) or len(value) != 64:
        return False
    return all(character in "0123456789abcdefABCDEF" for character in value)


def _write_json(path: Path, payload: object) -> None:
    serialized = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    path = path.resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(serialized, encoding="utf-8")
    temporary.replace(path)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-input", type=Path, required=True)
    parser.add_argument("--baseline-input", type=Path, required=True)
    parser.add_argument("--model-package", type=Path, required=True)
    parser.add_argument("--baseline-config", type=Path, required=True)
    parser.add_argument("--split-manifest", type=Path, required=True)
    parser.add_argument("--features", type=Path, required=True)
    parser.add_argument("--feature-metadata", type=Path, required=True)
    parser.add_argument("--prediction-code", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        bundle = build_evaluation_bundle(
            args.model_input,
            args.baseline_input,
            model_package=args.model_package,
            baseline_config=args.baseline_config,
            split_manifest=args.split_manifest,
            features=args.features,
            feature_metadata=args.feature_metadata,
            prediction_code=args.prediction_code,
        )
        _write_json(args.output, bundle)
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(json.dumps({"valid": False, "evaluation_error": str(error)}, indent=2))
        return 2
    print(json.dumps({"valid": True, "output_path": str(args.output.resolve())}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
