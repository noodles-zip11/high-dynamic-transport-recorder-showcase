"""Build an immutable feature matrix from a validated event manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import yaml

from ai.src.dataset_contract import load_dataset_contract, validate_event_contract
from ai.src.features import extract_features
from ai.src.validate_dataset import load_config, validate_dataset
from host.transport_recorder.analysis.event_record import load_event
from ai.src.build_windows import SensorWindow


def build_feature_dataset(config_path: Path, output_directory: Path) -> dict[str, object]:
    """Validate, extract, and write only eligible v1 class examples."""

    config_path = config_path.resolve()
    output_directory = output_directory.resolve()
    config = load_config(config_path)
    contract = load_dataset_contract(config_path)
    report = validate_dataset(config)
    manifest = yaml.safe_load(config.manifest_path.read_text(encoding="utf-8"))
    dataset_root = (config.manifest_path.parent / manifest["dataset_root"]).resolve()

    rows: list[list[float]] = []
    labels: list[str] = []
    groups: list[str] = []
    event_ids: list[int] = []
    source_hashes: list[str] = []
    for validation in report.events:
        if (
            not validation.eligible_for_training
            or validation.label not in contract.class_names
        ):
            continue
        event = load_event(dataset_root / validation.source_path)
        validate_event_contract(event, contract)
        accel = np.array(event.accel_counts, copy=True)
        gyro = np.array(event.gyro_counts, copy=True)
        accel.setflags(write=False)
        gyro.setflags(write=False)
        window = SensorWindow(
            session_id=validation.session_id,
            event_id=validation.event_id,
            sample_rate_hz=event.metadata.sample_rate_hz,
            start_sample=0,
            end_sample=event.sample_count,
            trigger_offset=event.trigger_index,
            accel_counts=accel,
            gyro_counts=gyro,
        )
        vector = extract_features(window)
        rows.append([
            float(getattr(vector, name))
            for name in contract.feature_names
        ])
        labels.append(validation.label)
        groups.append(validation.session_id)
        event_ids.append(validation.event_id)
        source_hashes.append(validation.source_sha256)

    if not rows:
        raise ValueError(
            "no eligible events were found for configured classes: "
            + ", ".join(contract.class_names)
        )
    output_directory.mkdir(parents=True, exist_ok=True)
    features_path = output_directory / "features.npy"
    labels_path = output_directory / "labels.json"
    groups_path = output_directory / "groups.json"
    event_ids_path = output_directory / "event_ids.json"
    np.save(features_path, np.asarray(rows, dtype=np.float64))
    labels_path.write_text(
        json.dumps(labels, indent=2) + "\n", encoding="utf-8"
    )
    groups_path.write_text(
        json.dumps(groups, indent=2) + "\n", encoding="utf-8"
    )
    event_ids_path.write_text(
        json.dumps(event_ids, indent=2) + "\n", encoding="utf-8"
    )
    metadata = {
        "metadata_version": 1,
        "feature_version": contract.feature_version,
        "feature_names": list(contract.feature_names),
        "sample_rate_hz": contract.sample_rate_hz,
        "event_length_samples": contract.event_length_samples,
        "class_names": list(contract.class_names),
        "manifest_sha256": hashlib.sha256(
            config.manifest_path.read_bytes()
        ).hexdigest(),
        "event_count": len(rows),
        "features_sha256": hashlib.sha256(features_path.read_bytes()).hexdigest(),
        "labels_sha256": hashlib.sha256(labels_path.read_bytes()).hexdigest(),
        "groups_sha256": hashlib.sha256(groups_path.read_bytes()).hexdigest(),
        "event_ids_sha256": hashlib.sha256(event_ids_path.read_bytes()).hexdigest(),
        "rows": [
            {
                "session_id": group,
                "event_id": event_id,
                "label": label,
                "source_sha256": source_hash,
            }
            for group, event_id, label, source_hash in zip(
                groups, event_ids, labels, source_hashes, strict=True
            )
        ],
    }
    (output_directory / "metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return metadata


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build_feature_dataset(args.config, args.output), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
