"""Reference implementation of the Phase 05 integer acceleration trigger."""

from collections.abc import Iterable, Mapping

TRIGGER_AXIS_X = 0x01
TRIGGER_AXIS_Y = 0x02
TRIGGER_AXIS_Z = 0x04


def _axis_mask(accel: tuple[int, int, int]) -> int:
    mask = 0
    if accel[0] != 0:
        mask |= TRIGGER_AXIS_X
    if accel[1] != 0:
        mask |= TRIGGER_AXIS_Y
    if accel[2] != 0:
        mask |= TRIGGER_AXIS_Z
    return mask


def detect_trigger_facts(
    samples: Iterable[Mapping[str, object]],
    *,
    threshold_magnitude_sq: int,
    consecutive_samples: int,
) -> list[dict[str, int]]:
    """Return the trigger facts produced by the C detector for ``samples``."""
    if consecutive_samples <= 0:
        raise ValueError("consecutive_samples must be positive")

    consecutive_count = 0
    facts: list[dict[str, int]] = []
    for sample in samples:
        sequence = int(sample["sequence"])
        accel = tuple(int(value) for value in sample["accel"])
        if len(accel) != 3:
            raise ValueError("each sample must contain exactly three acceleration axes")

        magnitude_sq = accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]
        if magnitude_sq < threshold_magnitude_sq:
            consecutive_count = 0
            continue

        if consecutive_count < consecutive_samples:
            consecutive_count += 1
        if consecutive_count < consecutive_samples:
            continue

        facts.append(
            {
                "sample_sequence": sequence,
                "magnitude_sq": magnitude_sq,
                "threshold_magnitude_sq": threshold_magnitude_sq,
                "axis_mask": _axis_mask(accel),
            }
        )

    return facts
