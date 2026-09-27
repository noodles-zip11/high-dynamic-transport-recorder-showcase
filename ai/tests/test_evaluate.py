from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys

import pytest

from ai.src.evaluate import compare_reports, evaluate_predictions


PROJECT_ROOT = Path(__file__).resolve().parents[2]


def test_evaluation_reports_per_class_and_operational_metrics() -> None:
    report = evaluate_predictions(
        truth=("background", "background", "impact", "impact", "drop", "drop"),
        predictions=("background", "impact", "impact", "background", "drop", "impact"),
        class_names=("background", "impact", "drop"),
        background_label="background",
        background_duration_hours=2.0,
    )

    assert report.confusion_matrix == (
        (1, 1, 0),
        (1, 1, 0),
        (0, 1, 1),
    )
    assert report.accuracy == 0.5
    assert report.per_class["background"].precision == 0.5
    assert report.per_class["impact"].precision == pytest.approx(1 / 3)
    assert report.per_class["drop"].recall == 0.5
    assert report.macro_f1 == pytest.approx((0.5 + 0.4 + 2 / 3) / 3)
    assert report.event_recall == 0.75
    assert report.false_positives_per_hour == 0.5


def test_evaluation_reports_a_four_by_four_confusion_matrix() -> None:
    class_names = (
        "background",
        "impact",
        "continuous_vibration",
        "drop",
    )
    report = evaluate_predictions(
        truth=class_names,
        predictions=(
            "background",
            "continuous_vibration",
            "continuous_vibration",
            "drop",
        ),
        class_names=class_names,
        background_label="background",
        background_duration_hours=1.0,
    )

    assert len(report.confusion_matrix) == 4
    assert all(len(row) == 4 for row in report.confusion_matrix)
    assert set(report.per_class) == set(class_names)
    assert report.accuracy == 0.75


def test_event_recall_groups_multiple_windows_from_the_same_event() -> None:
    report = evaluate_predictions(
        truth=("impact", "impact", "drop", "drop"),
        predictions=("background", "background", "background", "drop"),
        session_ids=("session-1", "session-1", "session-1", "session-1"),
        event_ids=("event-1", "event-1", "event-2", "event-2"),
        class_names=("background", "impact", "drop"),
        background_label="background",
        background_duration_hours=1.0,
    )

    assert report.event_count == 2
    assert report.event_recall == 0.5


def test_event_recall_rejects_conflicting_truth_for_one_event() -> None:
    with pytest.raises(ValueError, match="one truth label"):
        evaluate_predictions(
            truth=("impact", "drop"),
            predictions=("impact", "drop"),
            session_ids=("session-1", "session-1"),
            event_ids=("event-1", "event-1"),
            class_names=("background", "impact", "drop"),
            background_label="background",
            background_duration_hours=1.0,
        )


def test_event_identity_combines_session_and_event_id() -> None:
    report = evaluate_predictions(
        truth=("impact", "impact"),
        predictions=("background", "impact"),
        session_ids=("session-1", "session-2"),
        event_ids=(1, 1),
        class_names=("background", "impact"),
        background_label="background",
        background_duration_hours=1.0,
    )

    assert report.event_count == 2
    assert report.event_recall == 0.5


def test_event_ids_require_matching_session_ids() -> None:
    with pytest.raises(ValueError, match="supplied together"):
        evaluate_predictions(
            truth=("impact",),
            predictions=("impact",),
            event_ids=(1,),
            class_names=("background", "impact"),
            background_label="background",
            background_duration_hours=1.0,
        )


def test_evaluation_validates_labels_lengths_and_exposure() -> None:
    with pytest.raises(ValueError, match="same nonzero length"):
        evaluate_predictions((), (), ("background",), "background", 1.0)
    with pytest.raises(ValueError, match="unknown label"):
        evaluate_predictions(
            ("impact",),
            ("other",),
            ("background", "impact"),
            "background",
            1.0,
        )
    with pytest.raises(ValueError, match="positive"):
        evaluate_predictions(
            ("background",),
            ("background",),
            ("background",),
            "background",
            0.0,
        )


def test_comparison_exposes_baseline_deltas_without_deciding_acceptance() -> None:
    baseline = evaluate_predictions(
        truth=("background", "impact"),
        predictions=("impact", "impact"),
        class_names=("background", "impact"),
        background_label="background",
        background_duration_hours=0.5,
    )
    model = evaluate_predictions(
        truth=("background", "impact"),
        predictions=("background", "impact"),
        class_names=("background", "impact"),
        background_label="background",
        background_duration_hours=0.5,
    )

    comparison = compare_reports(baseline=baseline, candidate=model)

    assert comparison.false_positive_reduction_fraction == 1.0
    assert comparison.event_recall_delta == 0.0
    assert comparison.macro_f1_delta > 0.0


def test_comparison_rejects_reports_from_different_evidence() -> None:
    baseline = evaluate_predictions(
        truth=("background", "impact"),
        predictions=("background", "impact"),
        class_names=("background", "impact"),
        background_label="background",
        background_duration_hours=1.0,
    )
    candidate = evaluate_predictions(
        truth=("background", "background"),
        predictions=("background", "background"),
        class_names=("background", "impact"),
        background_label="background",
        background_duration_hours=1.0,
    )

    with pytest.raises(ValueError, match="same evaluation evidence"):
        compare_reports(baseline=baseline, candidate=candidate)


def test_cli_writes_machine_readable_metrics(tmp_path: Path) -> None:
    input_path = tmp_path / "predictions.json"
    output_path = tmp_path / "report.json"
    input_path.write_text(json.dumps({
        "truth": ["background", "impact"],
        "predictions": ["background", "impact"],
        "class_names": ["background", "impact"],
        "background_label": "background",
        "background_duration_hours": 1.0,
    }), encoding="utf-8")

    completed = subprocess.run(
        [
            sys.executable,
            "-m",
            "ai.src.evaluate",
            "--input",
            str(input_path),
            "--output",
            str(output_path),
        ],
        cwd=PROJECT_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )

    assert completed.returncode == 0
    report = json.loads(output_path.read_text(encoding="utf-8"))
    assert report["macro_f1"] == 1.0
    assert report["false_positives_per_hour"] == 0.0


def test_cli_can_write_candidate_vs_baseline_comparison(tmp_path: Path) -> None:
    candidate_path = tmp_path / "candidate.json"
    baseline_path = tmp_path / "baseline.json"
    output_path = tmp_path / "candidate-report.json"
    comparison_path = tmp_path / "comparison.json"
    shared = {
        "truth": ["background", "impact", "impact"],
        "session_ids": ["session-1", "session-2", "session-2"],
        "event_ids": ["background-1", "impact-1", "impact-1"],
        "class_names": ["background", "impact"],
        "background_label": "background",
        "background_duration_hours": 1.0,
    }
    candidate_path.write_text(json.dumps({
        **shared,
        "predictions": ["background", "background", "impact"],
    }), encoding="utf-8")
    baseline_path.write_text(json.dumps({
        **shared,
        "predictions": ["impact", "background", "background"],
    }), encoding="utf-8")

    completed = subprocess.run(
        [
            sys.executable,
            "-m",
            "ai.src.evaluate",
            "--input",
            str(candidate_path),
            "--output",
            str(output_path),
            "--baseline-input",
            str(baseline_path),
            "--comparison-output",
            str(comparison_path),
        ],
        cwd=PROJECT_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )

    assert completed.returncode == 0
    comparison = json.loads(comparison_path.read_text(encoding="utf-8"))
    assert comparison["candidate_report_path"] == str(output_path)
    assert comparison["baseline_report"]["event_recall"] == 0.0
    assert comparison["comparison"]["event_recall_delta"] == 1.0
