"""Stable classification metrics for rule/model comparisons."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
from typing import Sequence


@dataclass(frozen=True)
class ClassMetrics:
    support: int
    true_positive: int
    false_positive: int
    false_negative: int
    precision: float
    recall: float
    f1: float


@dataclass(frozen=True)
class EvaluationReport:
    class_names: tuple[str, ...]
    confusion_matrix: tuple[tuple[int, ...], ...]
    per_class: dict[str, ClassMetrics]
    accuracy: float
    macro_f1: float
    event_recall: float
    false_positives_per_hour: float
    sample_count: int
    event_count: int
    background_duration_hours: float
    evidence_hash: str

    def to_dict(self) -> dict[str, object]:
        return {
            "class_names": list(self.class_names),
            "confusion_matrix": [list(row) for row in self.confusion_matrix],
            "per_class": {
                name: asdict(metrics) for name, metrics in self.per_class.items()
            },
            "accuracy": self.accuracy,
            "macro_f1": self.macro_f1,
            "event_recall": self.event_recall,
            "false_positives_per_hour": self.false_positives_per_hour,
            "sample_count": self.sample_count,
            "event_count": self.event_count,
            "background_duration_hours": self.background_duration_hours,
            "evidence_hash": self.evidence_hash,
        }


@dataclass(frozen=True)
class ReportComparison:
    false_positive_reduction_fraction: float | None
    event_recall_delta: float
    macro_f1_delta: float

    def to_dict(self) -> dict[str, float | None]:
        return asdict(self)


def evaluate_predictions(
    truth: Sequence[str],
    predictions: Sequence[str],
    class_names: Sequence[str],
    background_label: str,
    background_duration_hours: float,
    session_ids: Sequence[str] | None = None,
    event_ids: Sequence[str | int] | None = None,
) -> EvaluationReport:
    """Evaluate fixed window predictions without fitting or model selection.

    Confusion-matrix metrics remain window-level. Event recall groups rows by
    the ``session_ids``/``event_ids`` pair and detects an event when any of its
    windows predicts a non-background label. When both are omitted, each row
    is one event.
    """

    if len(truth) == 0 or len(truth) != len(predictions):
        raise ValueError("truth and predictions must have the same nonzero length")
    names = tuple(class_names)
    if not names or len(set(names)) != len(names):
        raise ValueError("class_names must be nonempty and unique")
    if background_label not in names:
        raise ValueError("background label must appear in class_names")
    if background_duration_hours <= 0.0:
        raise ValueError("background duration hours must be positive")
    event_keys = _normalize_event_keys(session_ids, event_ids, len(truth))
    allowed = set(names)
    unknown = sorted((set(truth) | set(predictions)) - allowed)
    if unknown:
        raise ValueError(f"unknown label values: {', '.join(unknown)}")

    indices = {name: index for index, name in enumerate(names)}
    matrix = [[0 for _ in names] for _ in names]
    for expected, actual in zip(truth, predictions, strict=True):
        matrix[indices[expected]][indices[actual]] += 1

    metrics: dict[str, ClassMetrics] = {}
    for name, index in indices.items():
        true_positive = matrix[index][index]
        support = sum(matrix[index])
        predicted = sum(row[index] for row in matrix)
        false_positive = predicted - true_positive
        false_negative = support - true_positive
        precision = _safe_ratio(true_positive, predicted)
        recall = _safe_ratio(true_positive, support)
        f1 = _safe_ratio(2.0 * precision * recall, precision + recall)
        metrics[name] = ClassMetrics(
            support=support,
            true_positive=true_positive,
            false_positive=false_positive,
            false_negative=false_negative,
            precision=precision,
            recall=recall,
            f1=f1,
        )

    correct = sum(matrix[index][index] for index in range(len(names)))
    event_truth: dict[str, str] = {}
    event_detected: dict[str, bool] = {}
    for event_id, expected, actual in zip(
        event_keys,
        truth,
        predictions,
        strict=True,
    ):
        previous_truth = event_truth.setdefault(event_id, expected)
        if previous_truth != expected:
            raise ValueError("all windows for one event must have one truth label")
        event_detected[event_id] = (
            event_detected.get(event_id, False) or actual != background_label
        )
    critical_event_ids = {
        event_id
        for event_id, expected in event_truth.items()
        if expected != background_label
    }
    detected_critical = sum(event_detected[event_id] for event_id in critical_event_ids)
    background_false_positives = sum(
        event_detected[event_id]
        for event_id, expected in event_truth.items()
        if expected == background_label
    )
    return EvaluationReport(
        class_names=names,
        confusion_matrix=tuple(tuple(row) for row in matrix),
        per_class=metrics,
        accuracy=correct / len(truth),
        macro_f1=sum(item.f1 for item in metrics.values()) / len(metrics),
        event_recall=_safe_ratio(detected_critical, len(critical_event_ids)),
        false_positives_per_hour=background_false_positives / background_duration_hours,
        sample_count=len(truth),
        event_count=len(event_truth),
        background_duration_hours=background_duration_hours,
        evidence_hash=_evidence_hash(
            truth=truth,
            event_keys=event_keys,
            class_names=names,
            background_label=background_label,
            background_duration_hours=background_duration_hours,
        ),
    )


def compare_reports(
    *,
    baseline: EvaluationReport,
    candidate: EvaluationReport,
) -> ReportComparison:
    """Expose deltas; project acceptance remains an explicit later decision."""

    if (
        baseline.class_names != candidate.class_names
        or baseline.evidence_hash != candidate.evidence_hash
    ):
        raise ValueError("reports must use the same evaluation evidence")
    reduction = (
        None
        if baseline.false_positives_per_hour == 0.0
        else (
            baseline.false_positives_per_hour - candidate.false_positives_per_hour
        ) / baseline.false_positives_per_hour
    )
    return ReportComparison(
        false_positive_reduction_fraction=reduction,
        event_recall_delta=candidate.event_recall - baseline.event_recall,
        macro_f1_delta=candidate.macro_f1 - baseline.macro_f1,
    )


def _safe_ratio(numerator: float, denominator: float) -> float:
    return numerator / denominator if denominator else 0.0


def _normalize_event_keys(
    session_ids: Sequence[str] | None,
    event_ids: Sequence[str | int] | None,
    row_count: int,
) -> tuple[str, ...]:
    if session_ids is None and event_ids is None:
        return tuple(f"row:{index}" for index in range(row_count))
    if session_ids is None or event_ids is None:
        raise ValueError("session_ids and event_ids must be supplied together")
    if len(session_ids) != row_count or len(event_ids) != row_count:
        raise ValueError(
            "session_ids and event_ids must have the same length as truth"
        )
    normalized: list[str] = []
    for session_id, value in zip(session_ids, event_ids, strict=True):
        if not isinstance(session_id, str) or not session_id:
            raise ValueError("session_ids must contain nonempty strings")
        if isinstance(value, bool) or not isinstance(value, (str, int)):
            raise ValueError("event_ids must contain only strings or integers")
        if isinstance(value, str) and not value:
            raise ValueError("event_ids must not contain empty strings")
        normalized.append(json.dumps(
            (session_id, type(value).__name__, value),
            ensure_ascii=False,
            separators=(",", ":"),
        ))
    return tuple(normalized)


def _evidence_hash(
    *,
    truth: Sequence[str],
    event_keys: Sequence[str],
    class_names: Sequence[str],
    background_label: str,
    background_duration_hours: float,
) -> str:
    serialized = json.dumps(
        {
            "truth": list(truth),
            "event_keys": list(event_keys),
            "class_names": list(class_names),
            "background_label": background_label,
            "background_duration_hours": background_duration_hours,
        },
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(serialized).hexdigest()


def _evaluate_payload(raw: object) -> EvaluationReport:
    if not isinstance(raw, dict):
        raise ValueError("prediction input must be a JSON object")
    raw_event_ids = raw.get("event_ids")
    raw_session_ids = raw.get("session_ids")
    return evaluate_predictions(
        truth=tuple(raw["truth"]),
        predictions=tuple(raw["predictions"]),
        class_names=tuple(raw["class_names"]),
        background_label=str(raw["background_label"]),
        background_duration_hours=float(raw["background_duration_hours"]),
        session_ids=None if raw_session_ids is None else tuple(raw_session_ids),
        event_ids=None if raw_event_ids is None else tuple(raw_event_ids),
    )


def _write_json(path: Path, payload: object) -> None:
    serialized = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(serialized, encoding="utf-8")
    temporary.replace(path)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline-input", type=Path)
    parser.add_argument("--comparison-output", type=Path)
    args = parser.parse_args(argv)
    try:
        if (args.baseline_input is None) != (args.comparison_output is None):
            raise ValueError(
                "--baseline-input and --comparison-output must be supplied together"
            )
        raw = json.loads(args.input.read_text(encoding="utf-8"))
        report = _evaluate_payload(raw)
        _write_json(args.output, report.to_dict())
        if args.baseline_input is not None and args.comparison_output is not None:
            baseline_raw = json.loads(
                args.baseline_input.read_text(encoding="utf-8")
            )
            _require_comparable_inputs(raw, baseline_raw)
            baseline_report = _evaluate_payload(baseline_raw)
            comparison = compare_reports(
                baseline=baseline_report,
                candidate=report,
            )
            _write_json(args.comparison_output, {
                "candidate_report_path": str(args.output),
                "candidate_report": report.to_dict(),
                "baseline_input_path": str(args.baseline_input),
                "baseline_report": baseline_report.to_dict(),
                "comparison": comparison.to_dict(),
            })
    except (OSError, KeyError, TypeError, ValueError) as error:
        print(json.dumps({"valid": False, "evaluation_error": str(error)}, indent=2))
        return 2
    print(json.dumps({"valid": True, "output_path": str(args.output)}, indent=2))
    return 0


def _require_comparable_inputs(candidate: object, baseline: object) -> None:
    if not isinstance(candidate, dict) or not isinstance(baseline, dict):
        raise ValueError("prediction inputs must be JSON objects")
    for key in (
        "truth",
        "session_ids",
        "event_ids",
        "class_names",
        "background_label",
        "background_duration_hours",
    ):
        if candidate.get(key) != baseline.get(key):
            raise ValueError(f"candidate and baseline {key} must match")


if __name__ == "__main__":
    raise SystemExit(main())
