"""Validate immutable transport-event datasets before model work."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path
from typing import Sequence

import numpy as np
import yaml

from host.transport_recorder.analysis.event_record import EventFormatError, load_event


REQUIRED_SESSION_FIELDS = (
    "session_id",
    "device_id",
    "operator_id",
    "date_local",
    "mounting_id",
    "package_id",
    "action_class",
    "repetition",
    "sample_rate_hz",
    "accel_range",
    "gyro_range",
    "firmware_revision",
    "notes",
)
REQUIRED_EVENT_FIELDS = (
    "session_id",
    "event_id",
    "source_path",
    "source_sha256",
    "label",
    "label_confidence",
    "notes",
)
LABEL_CONFIDENCE_VALUES = frozenset(("high", "medium", "low"))


@dataclass(frozen=True)
class HardwareProfile:
    """Hardware contract used to decide whether session metadata is compatible."""

    board_model: str
    mcu: str
    imu_model: str
    sample_rate_hz: int
    accel_range: str
    gyro_range: str
    firmware_version: str
    firmware_revision: str | None
    evidence: tuple[str, ...]


@dataclass(frozen=True)
class DatasetConfig:
    manifest_path: Path
    allowed_labels: tuple[str, ...]
    expected_sample_rate_hz: int | None
    max_saturation_ratio: float
    reject_data_loss: bool
    hardware_profile: HardwareProfile | None = None


@dataclass(frozen=True)
class ValidationIssue:
    level: str
    code: str
    message: str
    session_id: str | None = None
    event_id: int | None = None

    def to_dict(self) -> dict[str, object]:
        return {
            "level": self.level,
            "code": self.code,
            "message": self.message,
            "session_id": self.session_id,
            "event_id": self.event_id,
        }


@dataclass(frozen=True)
class EventValidation:
    session_id: str
    event_id: int
    source_path: str
    source_sha256: str
    label: str
    eligible_for_training: bool
    format_version: int
    sample_rate_hz: int
    sample_count: int
    lost_sample_count: int
    saturation_ratio: float

    def to_dict(self) -> dict[str, object]:
        return {
            "session_id": self.session_id,
            "event_id": self.event_id,
            "source_path": self.source_path,
            "source_sha256": self.source_sha256,
            "label": self.label,
            "eligible_for_training": self.eligible_for_training,
            "format_version": self.format_version,
            "sample_rate_hz": self.sample_rate_hz,
            "sample_count": self.sample_count,
            "lost_sample_count": self.lost_sample_count,
            "saturation_ratio": self.saturation_ratio,
        }


@dataclass(frozen=True)
class ClassCoverage:
    event_count: int
    session_count: int
    dates: tuple[str, ...]
    mountings: tuple[str, ...]
    operators: tuple[str, ...]

    def to_dict(self) -> dict[str, object]:
        return {
            "event_count": self.event_count,
            "session_count": self.session_count,
            "dates": list(self.dates),
            "mountings": list(self.mountings),
            "operators": list(self.operators),
        }


@dataclass(frozen=True)
class ValidationReport:
    manifest_path: Path
    manifest_sha256: str
    issues: tuple[ValidationIssue, ...]
    events: tuple[EventValidation, ...]
    coverage: dict[str, ClassCoverage] = field(default_factory=dict)

    @property
    def has_errors(self) -> bool:
        return any(issue.level == "error" for issue in self.issues)

    def to_dict(self) -> dict[str, object]:
        return {
            "manifest_path": str(self.manifest_path),
            "manifest_sha256": self.manifest_sha256,
            "valid": not self.has_errors,
            "training_eligible_count": sum(
                event.eligible_for_training for event in self.events
            ),
            "issues": [issue.to_dict() for issue in self.issues],
            "events": [event.to_dict() for event in self.events],
            "coverage": {
                label: coverage.to_dict()
                for label, coverage in sorted(self.coverage.items())
            },
        }


def load_config(path: Path) -> DatasetConfig:
    """Load a versioned YAML config and resolve paths from its directory."""

    path = path.resolve()
    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict) or raw.get("version") != 1:
        raise ValueError("dataset config version must be 1")
    quality = raw.get("quality")
    if not isinstance(quality, dict):
        raise ValueError("dataset config quality must be a mapping")
    manifest_value = raw.get("manifest_path")
    if not isinstance(manifest_value, str) or not manifest_value:
        raise ValueError("dataset config manifest_path must be a nonempty string")
    labels = raw.get("allowed_labels")
    if not isinstance(labels, list) or not labels:
        raise ValueError("dataset config allowed_labels must be a nonempty list")
    manifest_path = (path.parent / manifest_value).resolve()
    expected_rate = quality.get("expected_sample_rate_hz")
    hardware_profile = _load_hardware_profile(raw.get("hardware"))
    config = DatasetConfig(
        manifest_path=manifest_path,
        allowed_labels=tuple(str(value) for value in labels),
        expected_sample_rate_hz=None if expected_rate is None else int(expected_rate),
        max_saturation_ratio=float(quality["max_saturation_ratio"]),
        reject_data_loss=bool(quality["reject_data_loss"]),
        hardware_profile=hardware_profile,
    )
    if config.expected_sample_rate_hz is not None and config.expected_sample_rate_hz <= 0:
        raise ValueError("expected sample rate must be positive or null")
    if (
        hardware_profile is not None
        and config.expected_sample_rate_hz != hardware_profile.sample_rate_hz
    ):
        raise ValueError(
            "quality.expected_sample_rate_hz must equal hardware.sample_rate_hz"
        )
    if not 0.0 <= config.max_saturation_ratio <= 1.0:
        raise ValueError("max saturation ratio must be between zero and one")
    return config


def _load_hardware_profile(raw: object) -> HardwareProfile | None:
    if raw is None:
        return None
    if not isinstance(raw, dict):
        raise ValueError("dataset config hardware must be a mapping")

    def required_text(field: str) -> str:
        value = raw.get(field)
        if not isinstance(value, str) or not value.strip():
            raise ValueError(f"dataset config hardware.{field} must be nonempty")
        return value.strip()

    sample_rate_value = raw.get("sample_rate_hz")
    try:
        sample_rate_hz = int(sample_rate_value)
    except (TypeError, ValueError) as error:
        raise ValueError(
            "dataset config hardware.sample_rate_hz must be a positive integer"
        ) from error
    if sample_rate_hz <= 0:
        raise ValueError("dataset config hardware.sample_rate_hz must be positive")

    firmware_revision_value = raw.get("firmware_revision")
    firmware_revision = (
        None
        if _is_pending(firmware_revision_value)
        else required_text("firmware_revision")
    )
    evidence_value = raw.get("evidence", [])
    if not isinstance(evidence_value, list) or not all(
        isinstance(item, str) and item.strip() for item in evidence_value
    ):
        raise ValueError("dataset config hardware.evidence must be a list of strings")

    return HardwareProfile(
        board_model=required_text("board_model"),
        mcu=required_text("mcu"),
        imu_model=required_text("imu_model"),
        sample_rate_hz=sample_rate_hz,
        accel_range=required_text("accel_range"),
        gyro_range=required_text("gyro_range"),
        firmware_version=required_text("firmware_version"),
        firmware_revision=firmware_revision,
        evidence=tuple(item.strip() for item in evidence_value),
    )


def validate_dataset(config: DatasetConfig) -> ValidationReport:
    """Validate manifest integrity and classify training-quality exclusions."""

    manifest_bytes = config.manifest_path.read_bytes()
    raw = yaml.safe_load(manifest_bytes.decode("utf-8"))
    if not isinstance(raw, dict) or raw.get("version") != 1:
        raise ValueError("dataset manifest version must be 1")
    dataset_root_value = raw.get("dataset_root")
    sessions_raw = raw.get("sessions")
    events_raw = raw.get("events")
    if not isinstance(dataset_root_value, str) or not dataset_root_value:
        raise ValueError("dataset_root must be a nonempty string")
    if not isinstance(sessions_raw, list) or not isinstance(events_raw, list):
        raise ValueError("sessions and events must be lists")

    dataset_root = (config.manifest_path.parent / dataset_root_value).resolve()
    issues: list[ValidationIssue] = []
    sessions: dict[str, dict[str, object]] = {}
    invalid_sessions: set[str] = set()
    allowed_actions = set(config.allowed_labels) - {"unknown"}
    for index, session_raw in enumerate(sessions_raw):
        if not isinstance(session_raw, dict):
            issues.append(_error("invalid_session", f"session {index} must be a mapping"))
            continue
        missing = _missing_fields(session_raw, REQUIRED_SESSION_FIELDS)
        if missing:
            incomplete_session_id = session_raw.get("session_id")
            issues.append(_error(
                "missing_session_fields",
                f"session {index} is missing: {', '.join(missing)}",
                session_id=(
                    None if incomplete_session_id is None else str(incomplete_session_id)
                ),
            ))
            if incomplete_session_id is not None:
                invalid_sessions.add(str(incomplete_session_id))
            continue
        session_id = str(session_raw["session_id"])
        if session_id in sessions:
            issues.append(_error(
                "duplicate_session_id",
                f"session_id {session_id!r} appears more than once",
                session_id=session_id,
            ))
            continue
        action_class = str(session_raw["action_class"])
        if action_class not in allowed_actions:
            issues.append(_error(
                "invalid_action_class",
                f"session action_class {action_class!r} is not allowed",
                session_id=session_id,
            ))
            invalid_sessions.add(session_id)
        sample_rate = session_raw["sample_rate_hz"]
        if not _is_pending(sample_rate):
            try:
                if int(sample_rate) <= 0:
                    raise ValueError
            except (TypeError, ValueError):
                issues.append(_error(
                    "invalid_session_sample_rate",
                    "session sample_rate_hz must be positive or pending",
                    session_id=session_id,
                ))
                invalid_sessions.add(session_id)
        sessions[session_id] = session_raw

    results: list[EventValidation] = []
    seen_hashes: dict[str, int] = {}
    seen_event_ids: set[tuple[str, int]] = set()
    for index, event_raw in enumerate(events_raw):
        if not isinstance(event_raw, dict):
            issues.append(_error("invalid_event", f"event {index} must be a mapping"))
            continue
        missing = _missing_fields(event_raw, REQUIRED_EVENT_FIELDS)
        if missing:
            issues.append(_error(
                "missing_event_fields",
                f"event {index} is missing: {', '.join(missing)}",
            ))
            continue

        session_id = str(event_raw["session_id"])
        event_id = _event_id(event_raw, index, issues, session_id)
        if event_id is None:
            continue
        session = sessions.get(session_id)
        if session is None:
            if session_id in invalid_sessions:
                continue
            issues.append(_error(
                "unknown_session_id",
                f"event references unknown session_id {session_id!r}",
                session_id=session_id,
                event_id=event_id,
            ))
            continue

        label = str(event_raw["label"])
        confidence = str(event_raw["label_confidence"])
        event_has_error = session_id in invalid_sessions
        event_key = (session_id, event_id)
        if event_key in seen_event_ids:
            issues.append(_error(
                "duplicate_event_id",
                f"event_id {event_id} appears more than once in session {session_id!r}",
                session_id,
                event_id,
            ))
            event_has_error = True
        else:
            seen_event_ids.add(event_key)
        if label not in config.allowed_labels:
            issues.append(_error(
                "invalid_label",
                f"label {label!r} is not allowed",
                session_id,
                event_id,
            ))
            event_has_error = True
        elif (
            session_id not in invalid_sessions
            and label != "unknown"
            and label != str(session["action_class"])
        ):
            issues.append(_error(
                "action_label_mismatch",
                f"event label {label!r} does not match session action_class "
                f"{session['action_class']!r}",
                session_id,
                event_id,
            ))
            event_has_error = True
        if confidence not in LABEL_CONFIDENCE_VALUES:
            issues.append(_error(
                "invalid_label_confidence",
                f"label confidence {confidence!r} is not allowed",
                session_id,
                event_id,
            ))
            event_has_error = True

        source_value = str(event_raw["source_path"])
        source_path = (dataset_root / source_value).resolve()
        if not source_path.is_relative_to(dataset_root):
            issues.append(_error(
                "source_outside_root",
                f"source path escapes dataset root: {source_value}",
                session_id,
                event_id,
            ))
            continue
        if not source_path.is_file():
            issues.append(_error(
                "source_missing",
                f"source file does not exist: {source_value}",
                session_id,
                event_id,
            ))
            continue

        expected_hash = str(event_raw["source_sha256"]).lower()
        actual_hash = hashlib.sha256(source_path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            issues.append(_error(
                "source_hash_mismatch",
                f"SHA-256 mismatch for {source_value}",
                session_id,
                event_id,
            ))
            event_has_error = True
        previous_id = seen_hashes.get(actual_hash)
        if previous_id is not None:
            issues.append(_error(
                "duplicate_source_hash",
                f"source content duplicates event {previous_id}",
                session_id,
                event_id,
            ))
            event_has_error = True
        else:
            seen_hashes[actual_hash] = event_id

        try:
            decoded = load_event(source_path)
        except (EventFormatError, OSError, ValueError) as error:
            issues.append(_error(
                "event_parse_error",
                f"cannot validate {source_value}: {error}",
                session_id,
                event_id,
            ))
            continue
        if decoded.metadata.event_id != event_id:
            issues.append(_error(
                "event_id_mismatch",
                f"manifest event_id {event_id} does not match record "
                f"{decoded.metadata.event_id}",
                session_id,
                event_id,
            ))
            event_has_error = True
        if decoded.accel_counts.shape != (decoded.sample_count, 3) or (
            decoded.gyro_counts.shape != (decoded.sample_count, 3)
        ):
            issues.append(_error(
                "channel_shape_invalid",
                "decoded event does not contain exactly six sensor channels",
                session_id,
                event_id,
            ))
            event_has_error = True
        if decoded.timestamps_us.size > 1 and np.any(np.diff(decoded.timestamps_us) <= 0):
            issues.append(_error(
                "timestamp_nonmonotonic",
                "derived sample timestamps are not strictly increasing",
                session_id,
                event_id,
            ))
            event_has_error = True

        combined = np.concatenate((decoded.accel_counts.ravel(), decoded.gyro_counts.ravel()))
        saturated = np.count_nonzero((combined == -32768) | (combined == 32767))
        saturation_ratio = float(saturated / combined.size) if combined.size else 0.0
        eligible = not event_has_error
        if label == "unknown":
            issues.append(_exclusion(
                "label_unknown",
                "unknown labels are preserved but excluded from supervised training",
                session_id,
                event_id,
            ))
            eligible = False
        pending_fields = tuple(
            field for field in REQUIRED_SESSION_FIELDS
            if _is_pending(session.get(field))
        )
        if pending_fields:
            issues.append(_exclusion(
                "pending_metadata",
                f"session metadata is pending: {', '.join(pending_fields)}",
                session_id,
                event_id,
            ))
            eligible = False
        if config.reject_data_loss and decoded.metadata.lost_sample_count:
            issues.append(_exclusion(
                "data_loss",
                f"event reports {decoded.metadata.lost_sample_count} lost samples",
                session_id,
                event_id,
            ))
            eligible = False
        if (
            config.expected_sample_rate_hz is not None
            and decoded.metadata.sample_rate_hz != config.expected_sample_rate_hz
        ):
            issues.append(_exclusion(
                "sample_rate_mismatch",
                f"record rate {decoded.metadata.sample_rate_hz} does not match expected "
                f"{config.expected_sample_rate_hz}",
                session_id,
                event_id,
            ))
            eligible = False
        recorded_rate = session["sample_rate_hz"]
        if (
            session_id not in invalid_sessions
            and not _is_pending(recorded_rate)
            and int(recorded_rate) != decoded.metadata.sample_rate_hz
        ):
            issues.append(_exclusion(
                "session_sample_rate_mismatch",
                f"session rate {recorded_rate} does not match record rate "
                f"{decoded.metadata.sample_rate_hz}",
                session_id,
                event_id,
            ))
            eligible = False
        if config.hardware_profile is not None and session_id not in invalid_sessions:
            profile = config.hardware_profile
            for field in ("accel_range", "gyro_range"):
                actual = session[field]
                expected = getattr(profile, field)
                if not _is_pending(actual) and _normalize_metadata(actual) != _normalize_metadata(expected):
                    issues.append(_exclusion(
                        f"hardware_{field}_mismatch",
                        f"session {field} {actual!r} does not match fixed hardware "
                        f"value {expected!r}",
                        session_id,
                        event_id,
                    ))
                    eligible = False
            if (
                profile.firmware_revision is not None
                and not _is_pending(session["firmware_revision"])
                and _normalize_metadata(session["firmware_revision"])
                != _normalize_metadata(profile.firmware_revision)
            ):
                issues.append(_exclusion(
                    "hardware_firmware_revision_mismatch",
                    "session firmware_revision does not match the fixed hardware "
                    f"profile value {profile.firmware_revision!r}",
                    session_id,
                    event_id,
                ))
                eligible = False
        if saturation_ratio > config.max_saturation_ratio:
            issues.append(_exclusion(
                "saturation_ratio_exceeded",
                f"saturation ratio {saturation_ratio:.6f} exceeds "
                f"{config.max_saturation_ratio:.6f}",
                session_id,
                event_id,
            ))
            eligible = False

        if event_has_error:
            continue
        results.append(EventValidation(
            session_id=session_id,
            event_id=event_id,
            source_path=str(source_path),
            source_sha256=actual_hash,
            label=label,
            eligible_for_training=eligible,
            format_version=decoded.metadata.format_version,
            sample_rate_hz=decoded.metadata.sample_rate_hz,
            sample_count=decoded.sample_count,
            lost_sample_count=decoded.metadata.lost_sample_count,
            saturation_ratio=saturation_ratio,
        ))

    coverage = _build_coverage(results, sessions)
    return ValidationReport(
        manifest_path=config.manifest_path,
        manifest_sha256=hashlib.sha256(manifest_bytes).hexdigest(),
        issues=tuple(issues),
        events=tuple(results),
        coverage=coverage,
    )


def _build_coverage(
    events: Sequence[EventValidation],
    sessions: dict[str, dict[str, object]],
) -> dict[str, ClassCoverage]:
    by_label: dict[str, list[EventValidation]] = {}
    for event in events:
        if event.eligible_for_training:
            by_label.setdefault(event.label, []).append(event)
    coverage: dict[str, ClassCoverage] = {}
    for label, label_events in sorted(by_label.items()):
        session_ids = {event.session_id for event in label_events}
        coverage[label] = ClassCoverage(
            event_count=len(label_events),
            session_count=len(session_ids),
            dates=tuple(sorted({
                str(sessions[item]["date_local"]) for item in session_ids
            })),
            mountings=tuple(sorted({
                str(sessions[item]["mounting_id"]) for item in session_ids
            })),
            operators=tuple(sorted({
                str(sessions[item]["operator_id"]) for item in session_ids
            })),
        )
    return coverage


def _missing_fields(raw: dict[str, object], required: Sequence[str]) -> tuple[str, ...]:
    return tuple(field for field in required if field not in raw)


def _event_id(
    raw: dict[str, object],
    index: int,
    issues: list[ValidationIssue],
    session_id: str,
) -> int | None:
    try:
        event_id = int(raw["event_id"])
    except (TypeError, ValueError):
        issues.append(_error(
            "invalid_event_id",
            f"event {index} has a non-integer event_id",
            session_id=session_id,
        ))
        return None
    if event_id <= 0:
        issues.append(_error(
            "invalid_event_id",
            f"event {index} event_id must be positive",
            session_id=session_id,
        ))
        return None
    return event_id


def _is_pending(value: object) -> bool:
    return value is None or (isinstance(value, str) and value.strip().lower() == "pending")


def _normalize_metadata(value: object) -> str:
    return str(value).strip().lower()


def _error(
    code: str,
    message: str,
    session_id: str | None = None,
    event_id: int | None = None,
) -> ValidationIssue:
    return ValidationIssue("error", code, message, session_id, event_id)


def _exclusion(
    code: str,
    message: str,
    session_id: str,
    event_id: int,
) -> ValidationIssue:
    return ValidationIssue("exclusion", code, message, session_id, event_id)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        report = validate_dataset(load_config(args.config))
    except (OSError, KeyError, TypeError, ValueError, yaml.YAMLError) as error:
        print(json.dumps({"valid": False, "config_error": str(error)}, indent=2))
        return 2
    print(json.dumps(report.to_dict(), indent=2))
    return 1 if report.has_errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
