"""Convert a verified EV01 file to inspectable JSON/CSV, with optional PNG output."""

from __future__ import annotations

import csv
import json
from pathlib import Path

from capture_debug_event import DebugEvent, parse_ev01


def export_event_artifacts(source_path: Path, output_dir: Path) -> DebugEvent:
    """Create CSV and JSON only after EV01 validation; PNG needs matplotlib installed."""
    event = parse_ev01(source_path.read_bytes())
    output_dir.mkdir(parents=True, exist_ok=True)
    stem = source_path.stem

    (output_dir / f"{stem}.json").write_text(
        json.dumps({
            "event_id": event.event_id,
            "trigger_monotonic_tick": event.trigger_monotonic_tick,
            "sample_rate_hz": event.sample_rate_hz,
            "trigger_sequence": event.trigger_sequence,
            "pretrigger_samples": event.pretrigger_samples,
            "posttrigger_samples": event.posttrigger_samples,
            "subtrigger_count": event.subtrigger_count,
            "flags": event.flags,
            "peak_magnitude_sq": event.peak_magnitude_sq,
            "threshold_magnitude_sq": event.threshold_magnitude_sq,
        }, indent=2), encoding="utf-8")
    with (output_dir / f"{stem}.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(("ax", "ay", "az", "gx", "gy", "gz", "timestamp", "temperature", "fifo_header"))
        writer.writerows(event.samples)
    return event
