"""Evidence exports that retain metadata and never overwrite by default."""

from __future__ import annotations

from dataclasses import asdict
from hashlib import sha256
import csv
import json
from pathlib import Path
import shutil

from .event_record import DecodedEvent
from ..protocol.client import AiResult


def export_csv(event: DecodedEvent, target: Path) -> Path:
    """Write count-domain samples with metadata comments and unit-bearing columns."""

    path = _ensure_new_target(target)
    with path.open("x", newline="", encoding="utf-8") as exported:
        exported.write("# transport_recorder_csv_version=1\n")
        exported.write(f"# event_id={event.metadata.event_id}\n")
        exported.write(f"# event_format_version={event.metadata.format_version}\n")
        exported.write(f"# sample_rate_hz={event.metadata.sample_rate_hz}\n")
        exported.write("# acceleration_unit=count\n")
        exported.write("# angular_rate_unit=count\n")
        writer = csv.DictWriter(
            exported,
            fieldnames=(
                "time_us",
                "accel_x_count",
                "accel_y_count",
                "accel_z_count",
                "accel_magnitude_count",
                "gyro_x_count",
                "gyro_y_count",
                "gyro_z_count",
                "sensor_timestamp",
                "temperature_c",
                "fifo_header",
            ),
        )
        writer.writeheader()
        for index in range(event.sample_count):
            writer.writerow(
                {
                    "time_us": int(event.timestamps_us[index]),
                    "accel_x_count": int(event.accel_counts[index, 0]),
                    "accel_y_count": int(event.accel_counts[index, 1]),
                    "accel_z_count": int(event.accel_counts[index, 2]),
                    "accel_magnitude_count": format(
                        float(event.accel_magnitude_counts[index]), ".9g"
                    ),
                    "gyro_x_count": int(event.gyro_counts[index, 0]),
                    "gyro_y_count": int(event.gyro_counts[index, 1]),
                    "gyro_z_count": int(event.gyro_counts[index, 2]),
                    "sensor_timestamp": int(event.sensor_timestamps[index]),
                    "temperature_c": int(event.sample_temperatures_c[index]),
                    "fifo_header": int(event.fifo_headers[index]),
                }
            )
    return path


def export_json_summary(
    event: DecodedEvent,
    target: Path,
    *,
    validation_state: str,
    label: str | None,
    note: str | None,
    ai_result: AiResult | None = None,
) -> Path:
    """Write event metadata, model output, and user annotation without raw samples."""

    path = _ensure_new_target(target)
    payload = {
        "event": asdict(event.metadata),
        "sample_count": event.sample_count,
        "trigger_index": event.trigger_index,
        "validation_state": validation_state,
        "model_result": asdict(ai_result) if ai_result is not None else None,
        "annotation": {"label": label, "note": note},
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path


def export_raw(source: Path, target: Path) -> tuple[Path, Path]:
    """Copy original bytes unchanged and publish their SHA-256 beside them."""

    path = _ensure_new_target(target)
    digest_path = path.with_name(path.name + ".sha256")
    _ensure_new_target(digest_path)
    shutil.copyfile(source, path)
    digest_path.write_text(_file_sha256(path) + "\n", encoding="utf-8")
    return path, digest_path


def _ensure_new_target(target: Path) -> Path:
    path = Path(target)
    if path.exists():
        raise FileExistsError(f"export target already exists: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def _file_sha256(path: Path) -> str:
    digest = sha256()
    with path.open("rb") as exported:
        while chunk := exported.read(64 * 1024):
            digest.update(chunk)
    return digest.hexdigest()
