"""Create deterministic, session-grouped dataset partitions."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
from typing import Mapping, Sequence

import yaml

from ai.src.dataset_contract import validate_class_names
from ai.src.validate_dataset import (
    EventValidation,
    ValidationReport,
    load_config,
    validate_dataset,
)


PARTITIONS = ("train", "validation", "test")


@dataclass(frozen=True)
class SplitRatios:
    train: float
    validation: float
    test: float

    def as_dict(self) -> dict[str, float]:
        return {
            "train": self.train,
            "validation": self.validation,
            "test": self.test,
        }

    def validate(self) -> None:
        values = tuple(self.as_dict().values())
        if any(value <= 0.0 for value in values):
            raise ValueError("all split ratios must be positive")
        if not math.isclose(sum(values), 1.0, abs_tol=1e-9):
            raise ValueError("split ratios must sum to 1")


def build_split_manifest(
    report: ValidationReport,
    *,
    seed: int,
    ratios: SplitRatios,
    grouping: str = "session_id",
    class_names: Sequence[str] | None = None,
) -> dict[str, object]:
    """Build a frozen session split or an explicitly opt-in pilot split."""

    ratios.validate()
    if report.has_errors:
        raise ValueError("dataset validation report contains errors")
    eligible = tuple(event for event in report.events if event.eligible_for_training)
    if not eligible:
        raise ValueError("dataset contains no eligible events")

    if grouping == "session_id":
        return _build_session_split_manifest(
            eligible, report.manifest_sha256, seed, ratios, class_names
        )
    if grouping == "pilot_event_stratified":
        return _build_pilot_split_manifest(
            eligible, report.manifest_sha256, seed, ratios, class_names
        )
    raise ValueError(f"unsupported split grouping: {grouping}")


def _build_session_split_manifest(
    eligible: tuple[EventValidation, ...],
    manifest_sha256: str,
    seed: int,
    ratios: SplitRatios,
    class_names: Sequence[str] | None,
) -> dict[str, object]:
    """Assign complete sessions by class without changing the default gate."""

    configured_names = (
        validate_class_names(class_names) if class_names is not None else None
    )
    if configured_names is not None and any(
        event.label not in configured_names for event in eligible
    ):
        raise ValueError("eligible event label is outside the configured class order")
    by_session: dict[str, list[EventValidation]] = {}
    for event in eligible:
        by_session.setdefault(event.session_id, []).append(event)
    session_labels: dict[str, str] = {}
    for session_id, events in by_session.items():
        labels = {event.label for event in events}
        if len(labels) != 1:
            raise ValueError(f"session {session_id!r} contains multiple labels")
        session_labels[session_id] = next(iter(labels))

    sessions_by_label: dict[str, list[str]] = {}
    for session_id, label in session_labels.items():
        sessions_by_label.setdefault(label, []).append(session_id)

    session_partition: dict[str, str] = {}
    class_session_counts: dict[str, dict[str, int]] = {}
    ratio_map = ratios.as_dict()
    labels_to_split = (
        configured_names
        if configured_names is not None
        else tuple(sorted(sessions_by_label))
    )
    for label in labels_to_split:
        ordered = sorted(
            sessions_by_label.get(label, []),
            key=lambda session_id: _stable_score(seed, label, session_id),
        )
        if len(ordered) < len(PARTITIONS):
            raise ValueError(
                f"label {label!r} needs at least 3 eligible sessions; "
                f"found {len(ordered)}"
            )
        counts = _allocate_counts(len(ordered), ratio_map)
        class_session_counts[label] = dict(counts)
        cursor = 0
        for partition in PARTITIONS:
            end = cursor + counts[partition]
            for session_id in ordered[cursor:end]:
                session_partition[session_id] = partition
            cursor = end

    partition_records: dict[str, list[dict[str, object]]] = {
        partition: [] for partition in PARTITIONS
    }
    for event in sorted(eligible, key=lambda item: (item.session_id, item.event_id)):
        partition_records[session_partition[event.session_id]].append({
            "session_id": event.session_id,
            "event_id": event.event_id,
            "source_sha256": event.source_sha256,
            "label": event.label,
        })

    dataset_hash = _dataset_hash(eligible)
    result: dict[str, object] = {
        "version": 1,
        "dataset_hash": dataset_hash,
        "source_manifest_sha256": manifest_sha256,
        "seed": seed,
        "grouping": "session_id",
        "ratios": ratio_map,
        "class_session_counts": class_session_counts,
        "session_partitions": dict(sorted(session_partition.items())),
        "partitions": partition_records,
    }
    if configured_names is not None:
        result["class_names"] = list(configured_names)
    return result


def _build_pilot_split_manifest(
    eligible: tuple[EventValidation, ...],
    manifest_sha256: str,
    seed: int,
    ratios: SplitRatios,
    class_names: Sequence[str] | None,
) -> dict[str, object]:
    """Stratify individual event identities for a declared one-session pilot."""

    names = (
        validate_class_names(class_names)
        if class_names is not None
        else validate_class_names(tuple(sorted({event.label for event in eligible})))
    )
    events_by_label: dict[str, list[EventValidation]] = {
        label: [] for label in names
    }
    for event in eligible:
        if event.label not in events_by_label:
            raise ValueError("eligible event label is outside the configured class order")
        events_by_label[event.label].append(event)

    event_partition: dict[tuple[str, int], str] = {}
    class_event_counts: dict[str, dict[str, int]] = {}
    ratio_map = ratios.as_dict()
    for label in names:
        ordered = sorted(
            events_by_label[label],
            key=lambda event: _stable_event_score(
                seed, label, event.session_id, event.event_id
            ),
        )
        if len(ordered) < len(PARTITIONS):
            raise ValueError(
                f"label {label!r} needs at least 3 eligible events for "
                "pilot_event_stratified"
            )
        counts = _allocate_counts(len(ordered), ratio_map)
        class_event_counts[label] = dict(counts)
        cursor = 0
        for partition in PARTITIONS:
            end = cursor + counts[partition]
            for event in ordered[cursor:end]:
                identity = (event.session_id, event.event_id)
                if identity in event_partition:
                    raise ValueError("pilot split contains duplicate event identity")
                event_partition[identity] = partition
            cursor = end

    partition_records: dict[str, list[dict[str, object]]] = {
        partition: [] for partition in PARTITIONS
    }
    for event in sorted(eligible, key=lambda item: (item.session_id, item.event_id)):
        partition_records[event_partition[(event.session_id, event.event_id)]].append({
            "session_id": event.session_id,
            "event_id": event.event_id,
            "source_sha256": event.source_sha256,
            "label": event.label,
        })
    class_session_counts = {
        label: {
            partition: len({
                record["session_id"]
                for record in partition_records[partition]
                if record["label"] == label
            })
            for partition in PARTITIONS
        }
        for label in names
    }
    return {
        "version": 1,
        "dataset_hash": _dataset_hash(eligible),
        "source_manifest_sha256": manifest_sha256,
        "seed": seed,
        "grouping": "pilot_event_stratified",
        "within_session_pilot": True,
        "generalization_warning": (
            "within_session_pilot=true; does not represent independent-session "
            "generalization"
        ),
        "class_names": list(names),
        "ratios": ratio_map,
        "class_session_counts": class_session_counts,
        "class_event_counts": class_event_counts,
        "session_partitions": {
            session_id: "within_session_pilot"
            for session_id in sorted({event.session_id for event in eligible})
        },
        "partitions": partition_records,
    }


def write_split_manifest(manifest: Mapping[str, object], path: Path) -> None:
    """Atomically write a split once; refuse changes to a frozen assignment."""

    serialized = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    if path.exists():
        if path.read_text(encoding="utf-8") == serialized:
            return
        raise FileExistsError(f"refusing to overwrite frozen split manifest: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(serialized, encoding="utf-8")
    temporary.replace(path)


def _stable_score(seed: int, label: str, session_id: str) -> str:
    value = f"{seed}\0{label}\0{session_id}".encode("utf-8")
    return hashlib.sha256(value).hexdigest()


def _stable_event_score(
    seed: int,
    label: str,
    session_id: str,
    event_id: int,
) -> str:
    value = f"{seed}\0{label}\0{session_id}\0{event_id}".encode("utf-8")
    return hashlib.sha256(value).hexdigest()


def _allocate_counts(total: int, ratios: Mapping[str, float]) -> dict[str, int]:
    if total < len(PARTITIONS):
        raise ValueError("each split requires at least one session")
    targets = {partition: total * ratios[partition] for partition in PARTITIONS}
    counts = {partition: 1 for partition in PARTITIONS}
    for _ in range(total - len(PARTITIONS)):
        selected = max(
            PARTITIONS,
            key=lambda partition: (
                targets[partition] - counts[partition],
                -PARTITIONS.index(partition),
            ),
        )
        counts[selected] += 1
    return counts


def _dataset_hash(events: tuple[EventValidation, ...]) -> str:
    identity = [
        {
            "event_id": event.event_id,
            "label": event.label,
            "session_id": event.session_id,
            "source_sha256": event.source_sha256,
        }
        for event in sorted(events, key=lambda item: (item.session_id, item.event_id))
    ]
    serialized = json.dumps(identity, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(serialized).hexdigest()


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args(argv)
    config_path = args.config.resolve()
    try:
        raw = yaml.safe_load(config_path.read_text(encoding="utf-8"))
        split_raw = raw["split"]
        ratio_raw = split_raw["ratios"]
        ratios = SplitRatios(
            train=float(ratio_raw["train"]),
            validation=float(ratio_raw["validation"]),
            test=float(ratio_raw["test"]),
        )
        output_path = (config_path.parent / str(split_raw["output_path"])).resolve()
        configured_class_names = None
        if isinstance(raw.get("model"), dict) and raw["model"].get("class_names"):
            from ai.src.dataset_contract import load_dataset_contract

            configured_class_names = load_dataset_contract(config_path).class_names
        report = validate_dataset(load_config(config_path))
        if report.has_errors:
            print(json.dumps(report.to_dict(), indent=2))
            return 1
        manifest = build_split_manifest(
            report,
            seed=int(split_raw["seed"]),
            ratios=ratios,
            grouping=str(split_raw.get("grouping", "session_id")),
            class_names=configured_class_names,
        )
        write_split_manifest(manifest, output_path)
    except (OSError, KeyError, TypeError, ValueError, yaml.YAMLError) as error:
        print(json.dumps({"valid": False, "split_error": str(error)}, indent=2))
        return 2
    print(json.dumps({
        "valid": True,
        "dataset_hash": manifest["dataset_hash"],
        "output_path": str(output_path),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
