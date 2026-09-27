import csv
from pathlib import Path

from host.tools.trigger_reference import TRIGGER_AXIS_X, detect_trigger_facts


def test_matches_the_integer_trigger_golden_vector():
    fixture = Path(__file__).parent / "data" / "trigger_reference_golden.csv"
    with fixture.open(newline="", encoding="utf-8") as source:
        samples = [
            {
                "sequence": int(row["sequence"]),
                "accel": (int(row["ax"]), int(row["ay"]), int(row["az"])),
                "expected_trigger": bool(int(row["expected_trigger"])),
            }
            for row in csv.DictReader(source)
        ]

    facts = detect_trigger_facts(
        samples,
        threshold_magnitude_sq=100,
        consecutive_samples=2,
    )

    expected_sequences = [sample["sequence"] for sample in samples
                          if sample["expected_trigger"]]
    assert [fact["sample_sequence"] for fact in facts] == expected_sequences
    assert facts[-1]["magnitude_sq"] == 1073741824
    assert facts[-1]["axis_mask"] == TRIGGER_AXIS_X
