from __future__ import annotations

from pathlib import Path

import pytest

from ai.src.dataset_contract import (
    FEATURE_NAMES,
    DatasetContract,
    load_dataset_contract,
    validate_event_contract,
)
from host.transport_recorder.analysis.event_record import load_event


PROJECT_ROOT = Path(__file__).resolve().parents[2]
FIXTURE = PROJECT_ROOT / "host" / "tests" / "fixtures" / "events" / "valid-42.terp-event"


def _write_config(
    tmp_path: Path,
    *,
    length: int = 4,
    class_names: tuple[str, ...] = ("background", "impact"),
) -> Path:
    path = tmp_path / "dataset.yaml"
    path.write_text(
        f"""\
version: 1
manifest_path: manifest.yaml
allowed_labels: [background, impact]
hardware:
  sample_rate_hz: 1600
quality:
  expected_sample_rate_hz: 1600
  max_saturation_ratio: 0.01
  reject_data_loss: true
windows:
  event:
    length_samples: {length}
features:
  version: counts_v1
  axis_order: [ax, ay, az, gx, gy, gz]
  names: [{', '.join(FEATURE_NAMES)}]
model:
  class_names: [{', '.join(class_names)}]
  quantization: int8
""",
        encoding="utf-8",
    )
    return path


def test_contract_loads_frozen_window_axes_features_and_classes(tmp_path: Path) -> None:
    contract = load_dataset_contract(_write_config(tmp_path))

    assert contract.event_length_samples == 4
    assert contract.sample_rate_hz == 1600
    assert contract.axis_order == ("ax", "ay", "az", "gx", "gy", "gz")
    assert contract.feature_names == FEATURE_NAMES
    assert contract.class_names == ("background", "impact")
    assert contract.quantization == "int8"


def test_contract_rejects_partial_or_wrong_feature_order(tmp_path: Path) -> None:
    path = _write_config(tmp_path)
    text = path.read_text(encoding="utf-8").replace(
        "names: [" + ", ".join(FEATURE_NAMES) + "]",
        "names: [duration_seconds, " + ", ".join(FEATURE_NAMES[:-1]) + "]",
    )
    path.write_text(text, encoding="utf-8")

    with pytest.raises(ValueError, match="feature names"):
        load_dataset_contract(path)


def test_contract_accepts_the_four_class_order(tmp_path: Path) -> None:
    contract = load_dataset_contract(
        _write_config(
            tmp_path,
            class_names=(
                "background",
                "impact",
                "continuous_vibration",
                "drop",
            ),
        )
    )

    assert contract.class_names == (
        "background",
        "impact",
        "continuous_vibration",
        "drop",
    )


def test_contract_rejects_class_order_outside_the_fixed_prefix(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="class names"):
        load_dataset_contract(
            _write_config(
                tmp_path,
                class_names=("background", "drop", "impact"),
            )
        )


def test_event_contract_rejects_wrong_rate_or_length(tmp_path: Path) -> None:
    contract = load_dataset_contract(_write_config(tmp_path))
    event = load_event(FIXTURE)

    validate_event_contract(event, contract)

    bad_rate = contract.__class__(
        **{**contract.__dict__, "sample_rate_hz": 800}
    )
    with pytest.raises(ValueError, match="sample rate"):
        validate_event_contract(event, bad_rate)

    bad_length = contract.__class__(
        **{**contract.__dict__, "event_length_samples": 5}
    )
    with pytest.raises(ValueError, match="exactly"):
        validate_event_contract(event, bad_length)
