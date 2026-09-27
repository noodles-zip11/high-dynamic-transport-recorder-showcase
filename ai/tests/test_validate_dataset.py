from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib

import pytest
import yaml

from ai.src.validate_dataset import load_config, validate_dataset


PROJECT_ROOT = Path(__file__).resolve().parents[2]
EVENT_FIXTURES = PROJECT_ROOT / "host" / "tests" / "fixtures" / "events"


def _write_dataset(
    tmp_path: Path,
    *,
    fixture_name: str = "valid-42.terp-event",
    label: str = "impact",
    expected_rate: int | None = 1600,
    session_overrides: dict[str, object] | None = None,
    event_overrides: dict[str, object] | None = None,
) -> Path:
    dataset_root = tmp_path / "raw"
    dataset_root.mkdir(parents=True)
    source = dataset_root / "event.terp-event"
    shutil.copyfile(EVENT_FIXTURES / fixture_name, source)
    session: dict[str, object] = {
        "session_id": "20260811_01",
        "device_id": "device_01",
        "operator_id": "operator_01",
        "date_local": "2026-08-11",
        "mounting_id": "box_bottom_x_right",
        "package_id": "carton_a_foam_01",
        "action_class": "impact",
        "repetition": "01",
        "sample_rate_hz": 1600,
        "accel_range": "16g",
        "gyro_range": "2000dps",
        "firmware_revision": "598e536",
        "notes": "test fixture",
    }
    session.update(session_overrides or {})
    event: dict[str, object] = {
        "session_id": session["session_id"],
        "event_id": 42,
        "source_path": source.name,
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "label": label,
        "label_confidence": "high",
        "notes": "observed action",
    }
    event.update(event_overrides or {})
    manifest_path = tmp_path / "manifest.yaml"
    manifest_path.write_text(
        yaml.safe_dump(
            {
                "version": 1,
                "dataset_root": "raw",
                "sessions": [session],
                "events": [event],
            },
            sort_keys=False,
        ),
        encoding="utf-8",
    )
    config_path = tmp_path / "dataset.yaml"
    config_path.write_text(
        yaml.safe_dump(
            {
                "version": 1,
                "manifest_path": "manifest.yaml",
                "allowed_labels": [
                    "background",
                    "impact",
                    "continuous_vibration",
                    "drop",
                    "unknown",
                ],
                "quality": {
                    "expected_sample_rate_hz": expected_rate,
                    "max_saturation_ratio": 0.01,
                    "reject_data_loss": True,
                },
            },
            sort_keys=False,
        ),
        encoding="utf-8",
    )
    return config_path


def _rewrite_manifest(config_path: Path, transform) -> None:
    manifest_path = config_path.parent / "manifest.yaml"
    raw = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
    transform(raw)
    manifest_path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")


def _rewrite_event(config_path: Path, transform) -> None:
    source_path = config_path.parent / "raw" / "event.terp-event"
    raw = bytearray(source_path.read_bytes())
    transform(raw)
    source_path.write_bytes(raw)
    digest = hashlib.sha256(raw).hexdigest()
    _rewrite_manifest(config_path, lambda manifest: manifest["events"][0].update(
        {"source_sha256": digest}
    ))


def _add_hardware_profile(config_path: Path, **overrides: object) -> None:
    raw = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    profile: dict[str, object] = {
        "board_model": "openmv4_h743",
        "mcu": "STM32H743VIT6",
        "imu_model": "ICM45686",
        "sample_rate_hz": 1600,
        "accel_range": "16g",
        "gyro_range": "2000dps",
        "firmware_version": "phase08-terp-uart3",
        "firmware_revision": "pending",
        "evidence": ["evidence/hardware-validation.md"],
    }
    profile.update(overrides)
    raw["hardware"] = profile
    config_path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")


def test_project_config_freezes_verified_hardware_profile() -> None:
    config = load_config(PROJECT_ROOT / "ai" / "configs" / "dataset_v1.yaml")

    assert config.expected_sample_rate_hz == 1600
    assert config.hardware_profile is not None
    assert config.hardware_profile.board_model == "openmv4_h743"
    assert config.hardware_profile.mcu == "STM32H743VIT6"
    assert config.hardware_profile.imu_model == "ICM45686"
    assert config.hardware_profile.accel_range == "16g"
    assert config.hardware_profile.gyro_range == "2000dps"
    assert config.hardware_profile.firmware_version == "phase08-terp-uart3"
    assert config.hardware_profile.firmware_revision == (
        "203bfaa55ab905375c640216e78b8d438e25a89c"
    )


def test_hardware_profile_metadata_mismatch_is_quality_exclusion(tmp_path: Path) -> None:
    config_path = _write_dataset(tmp_path)
    _add_hardware_profile(config_path, firmware_revision="598e536")
    _rewrite_manifest(
        config_path,
        lambda manifest: manifest["sessions"][0].update({
            "accel_range": "8g",
            "gyro_range": "1000dps",
            "firmware_revision": "old-image",
        }),
    )

    report = validate_dataset(load_config(config_path))

    assert report.has_errors is False
    assert report.events[0].eligible_for_training is False
    assert {issue.code for issue in report.issues} == {
        "hardware_accel_range_mismatch",
        "hardware_gyro_range_mismatch",
        "hardware_firmware_revision_mismatch",
    }


def test_hardware_profile_rate_must_match_quality_rate(tmp_path: Path) -> None:
    config_path = _write_dataset(tmp_path)
    _add_hardware_profile(config_path, sample_rate_hz=800)

    with pytest.raises(ValueError, match="expected_sample_rate_hz"):
        load_config(config_path)


def test_load_config_resolves_manifest_relative_to_config(tmp_path: Path) -> None:
    config_path = tmp_path / "configs" / "dataset.yaml"
    config_path.parent.mkdir()
    config_path.write_text(
        yaml.safe_dump(
            {
                "version": 1,
                "manifest_path": "../manifests/events.yaml",
                "allowed_labels": [
                    "background",
                    "impact",
                    "continuous_vibration",
                    "drop",
                    "unknown",
                ],
                "quality": {
                    "expected_sample_rate_hz": None,
                    "max_saturation_ratio": 0.01,
                    "reject_data_loss": True,
                },
            },
            sort_keys=False,
        ),
        encoding="utf-8",
    )

    config = load_config(config_path)

    assert config.manifest_path == (tmp_path / "manifests" / "events.yaml").resolve()
    assert config.expected_sample_rate_hz is None
    assert config.max_saturation_ratio == 0.01
    assert config.reject_data_loss is True


def test_valid_ev02_event_is_training_eligible(tmp_path: Path) -> None:
    config = load_config(_write_dataset(tmp_path))

    report = validate_dataset(config)

    assert report.has_errors is False
    assert report.manifest_sha256 == hashlib.sha256(
        config.manifest_path.read_bytes()
    ).hexdigest()
    assert report.issues == ()
    assert len(report.events) == 1
    assert report.events[0].eligible_for_training is True
    assert report.events[0].format_version == 2
    assert report.events[0].sample_count == 4
    assert report.coverage["impact"].event_count == 1
    assert report.coverage["impact"].session_count == 1
    assert report.coverage["impact"].dates == ("2026-08-11",)
    assert report.coverage["impact"].mountings == ("box_bottom_x_right",)
    assert report.coverage["impact"].operators == ("operator_01",)


def test_unknown_and_pending_metadata_are_preserved_but_excluded(tmp_path: Path) -> None:
    config = load_config(_write_dataset(
        tmp_path,
        label="unknown",
        session_overrides={"device_id": "pending"},
    ))

    report = validate_dataset(config)

    assert report.has_errors is False
    assert report.events[0].eligible_for_training is False
    assert {issue.code for issue in report.issues} == {
        "label_unknown",
        "pending_metadata",
    }


def test_ev03_data_loss_is_preserved_but_excluded(tmp_path: Path) -> None:
    config = load_config(_write_dataset(
        tmp_path,
        fixture_name="valid-v3-loss-43.terp-event",
        event_overrides={"event_id": 43},
    ))

    report = validate_dataset(config)

    assert report.has_errors is False
    assert report.events[0].eligible_for_training is False
    assert report.events[0].format_version == 3
    assert report.events[0].lost_sample_count == 6
    assert [issue.code for issue in report.issues] == ["data_loss"]


def test_saturation_and_sample_rate_mismatch_are_quality_exclusions(tmp_path: Path) -> None:
    config_path = _write_dataset(tmp_path, expected_rate=800)

    def saturate_one_axis(raw: bytearray) -> None:
        header_bytes = struct.unpack_from("<H", raw, 6)[0]
        struct.pack_into("<h", raw, header_bytes, 32767)
        payload = raw[header_bytes:]
        struct.pack_into("<I", raw, 64, zlib.crc32(payload) & 0xFFFFFFFF)

    _rewrite_event(config_path, saturate_one_axis)

    report = validate_dataset(load_config(config_path))

    assert report.has_errors is False
    assert report.events[0].eligible_for_training is False
    assert {issue.code for issue in report.issues} == {
        "sample_rate_mismatch",
        "saturation_ratio_exceeded",
    }


def test_hash_mismatch_and_event_id_mismatch_are_errors(tmp_path: Path) -> None:
    config_path = _write_dataset(
        tmp_path,
        event_overrides={"source_sha256": "0" * 64, "event_id": 99},
    )

    report = validate_dataset(load_config(config_path))

    assert report.has_errors is True
    assert report.events == ()
    assert {issue.code for issue in report.issues} == {
        "event_id_mismatch",
        "source_hash_mismatch",
    }


def test_parser_error_and_source_escape_are_errors(tmp_path: Path) -> None:
    parser_config = load_config(_write_dataset(
        tmp_path / "parser",
        fixture_name="crc-invalid-42.terp-event",
    ))
    escape_config_path = _write_dataset(tmp_path / "escape")
    outside = tmp_path / "outside.terp-event"
    shutil.copyfile(EVENT_FIXTURES / "valid-42.terp-event", outside)
    _rewrite_manifest(
        escape_config_path,
        lambda manifest: manifest["events"][0].update({
            "source_path": "../../outside.terp-event",
            "source_sha256": hashlib.sha256(outside.read_bytes()).hexdigest(),
        }),
    )

    parser_report = validate_dataset(parser_config)
    escape_report = validate_dataset(load_config(escape_config_path))

    assert [issue.code for issue in parser_report.issues] == ["event_parse_error"]
    assert [issue.code for issue in escape_report.issues] == ["source_outside_root"]
    assert parser_report.has_errors is True
    assert escape_report.has_errors is True


def test_duplicate_sessions_and_source_hashes_are_errors(tmp_path: Path) -> None:
    config_path = _write_dataset(tmp_path)

    def duplicate_records(manifest) -> None:
        manifest["sessions"].append(dict(manifest["sessions"][0]))
        second_event = dict(manifest["events"][0])
        manifest["events"].append(second_event)

    _rewrite_manifest(config_path, duplicate_records)

    report = validate_dataset(load_config(config_path))

    assert report.has_errors is True
    assert {issue.code for issue in report.issues} >= {
        "duplicate_event_id",
        "duplicate_session_id",
        "duplicate_source_hash",
    }


def test_session_contract_and_recorded_sample_rate_are_checked(tmp_path: Path) -> None:
    rate_config = load_config(_write_dataset(
        tmp_path / "rate",
        expected_rate=None,
        session_overrides={"sample_rate_hz": 800},
    ))
    action_config = load_config(_write_dataset(
        tmp_path / "action",
        session_overrides={"action_class": "tilt"},
    ))
    missing_config_path = _write_dataset(tmp_path / "missing")
    _rewrite_manifest(
        missing_config_path,
        lambda manifest: manifest["sessions"][0].pop("repetition"),
    )

    rate_report = validate_dataset(rate_config)
    action_report = validate_dataset(action_config)
    missing_report = validate_dataset(load_config(missing_config_path))

    assert [issue.code for issue in rate_report.issues] == [
        "session_sample_rate_mismatch"
    ]
    assert rate_report.events[0].eligible_for_training is False
    assert [issue.code for issue in action_report.issues] == ["invalid_action_class"]
    assert action_report.has_errors is True
    assert [issue.code for issue in missing_report.issues] == ["missing_session_fields"]
    assert missing_report.has_errors is True


def test_invalid_session_rate_returns_structured_error_instead_of_crashing(
    tmp_path: Path,
) -> None:
    config = load_config(_write_dataset(
        tmp_path,
        expected_rate=None,
        session_overrides={"sample_rate_hz": "not-a-rate"},
    ))

    report = validate_dataset(config)

    assert report.has_errors is True
    assert [issue.code for issue in report.issues] == [
        "invalid_session_sample_rate"
    ]
    assert report.events == ()


def test_event_label_must_match_session_action_or_be_unknown(tmp_path: Path) -> None:
    config = load_config(_write_dataset(
        tmp_path,
        label="drop",
        session_overrides={"action_class": "impact"},
    ))

    report = validate_dataset(config)

    assert report.has_errors is True
    assert [issue.code for issue in report.issues] == ["action_label_mismatch"]
    assert report.events == ()


def test_cli_emits_json_and_fails_only_for_integrity_errors(tmp_path: Path) -> None:
    valid_config = _write_dataset(tmp_path / "valid", label="unknown")
    invalid_config = _write_dataset(
        tmp_path / "invalid",
        event_overrides={"source_sha256": "0" * 64},
    )

    valid = subprocess.run(
        [sys.executable, "-m", "ai.src.validate_dataset", "--config", str(valid_config)],
        cwd=PROJECT_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    invalid = subprocess.run(
        [sys.executable, "-m", "ai.src.validate_dataset", "--config", str(invalid_config)],
        cwd=PROJECT_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )

    assert valid.returncode == 0
    assert json.loads(valid.stdout)["training_eligible_count"] == 0
    assert invalid.returncode == 1
    assert json.loads(invalid.stdout)["valid"] is False
