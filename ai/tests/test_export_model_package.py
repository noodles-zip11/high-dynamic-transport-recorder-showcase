from __future__ import annotations

import csv
import hashlib
from pathlib import Path
import struct
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np
import pytest

from ai.src.dataset_contract import FEATURE_NAMES
from ai.src.export_model_package import export_model_package
from ai.src.model_package import save_model_package
from ai.src.train_model import MLPModel


ROOT = Path(__file__).resolve().parents[1]
FOUR_CLASS_NAMES = (
    "background",
    "impact",
    "continuous_vibration",
    "drop",
)


def test_export_model_package_contains_contract_and_hashes(tmp_path: Path) -> None:
    output = tmp_path / "pilot.trmd"
    export_model_package(
        ROOT / "artifacts" / "pilot-v1" / "model",
        output,
        ROOT / "tests" / "fixtures" / "ai_golden_vectors.csv",
    )
    package = output.read_bytes()

    assert package[:4] == b"TRMD"
    assert struct.unpack_from("<H", package, 4)[0] == 1
    assert struct.unpack_from("<H", package, 6)[0] == 160
    assert struct.unpack_from("<I", package, 8)[0] == len(package)
    assert struct.unpack_from("<H", package, 14)[0] == 2
    assert struct.unpack_from("<H", package, 16)[0] == 1
    assert package[18:22] == bytes((6, 8, 2, 1))
    assert struct.unpack_from("<H", package, 24)[0] == 6
    assert struct.unpack_from("<H", package, 26)[0] == 4

    model_offset = struct.unpack_from("<I", package, 28)[0]
    model_length = struct.unpack_from("<I", package, 32)[0]
    golden_offset = struct.unpack_from("<I", package, 36)[0]
    golden_length = struct.unpack_from("<I", package, 40)[0]
    assert model_offset == 160
    assert golden_offset == model_offset + model_length
    assert golden_length == 4 * 40
    assert zlib_crc32(package[model_offset:golden_offset]) == struct.unpack_from(
        "<I", package, 44
    )[0]
    assert hashlib.sha256(package[model_offset:golden_offset]).digest() == package[48:80]
    assert package[80:112].hex() == (
        "b63f92aa0eb0d901684c42bd11a7374d0ce8e014b8dab4174bc752215177ae17"
    )
    assert package[112:144].hex() == (
        "e8ded7a990e4f3baad0f9365f90e835ae66c408e2750b7d5082b1d6cda59edcf"
    )
    assert zlib_crc32(package[:144]) == struct.unpack_from("<I", package, 144)[0]


def _write_four_class_model_and_vectors(tmp_path: Path) -> tuple[Path, Path]:
    model = MLPModel(
        class_names=FOUR_CLASS_NAMES,
        feature_names=FEATURE_NAMES,
        feature_mean=np.zeros(len(FEATURE_NAMES), dtype=np.float64),
        feature_scale=np.ones(len(FEATURE_NAMES), dtype=np.float64),
        weights1=np.zeros((len(FEATURE_NAMES), 8), dtype=np.float64),
        bias1=np.zeros(8, dtype=np.float64),
        weights2=np.zeros((8, len(FOUR_CLASS_NAMES)), dtype=np.float64),
        bias2=np.zeros(len(FOUR_CLASS_NAMES), dtype=np.float64),
        input_scale=0.1,
        hidden_scale=0.1,
    )
    package_directory = tmp_path / "four-class-model"
    save_model_package(
        model,
        package_directory,
        dataset_manifest_sha256="b" * 64,
        training_revision="test-four-class",
    )
    vectors_path = tmp_path / "four-class-vectors.csv"
    columns = (
        "name",
        *FEATURE_NAMES,
        *(f"logit_{name}" for name in FOUR_CLASS_NAMES),
        *(f"probability_{name}" for name in FOUR_CLASS_NAMES),
        "class_index",
        "confidence",
    )
    with vectors_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        row = {field: "0.0" for field in columns}
        row.update({
            "name": "drop-vector",
            "logit_background": "1.0",
            "logit_impact": "2.0",
            "logit_continuous_vibration": "3.0",
            "logit_drop": "4.0",
            "probability_background": "0.1",
            "probability_impact": "0.2",
            "probability_continuous_vibration": "0.3",
            "probability_drop": "0.4",
            "class_index": "3",
            "confidence": "0.4",
        })
        writer.writerow(row)
    return package_directory, vectors_path


def test_export_model_package_accepts_four_classes_and_keeps_40b_golden_layout(
    tmp_path: Path,
) -> None:
    package_directory, vectors_path = _write_four_class_model_and_vectors(tmp_path)
    output = tmp_path / "four-class.trmd"

    export_model_package(package_directory, output, vectors_path)
    package = output.read_bytes()

    assert package[18:22] == bytes((6, 8, 4, 1))
    model_offset = struct.unpack_from("<I", package, 28)[0]
    model_length = struct.unpack_from("<I", package, 32)[0]
    golden_offset = struct.unpack_from("<I", package, 36)[0]
    golden_length = struct.unpack_from("<I", package, 40)[0]
    assert model_offset == 160
    assert model_length == (
        10 + 16 + 6 * 8 + 6 * 8 + 4 + 8 * 4 + 8 * 4 + 4 + 4 * 4 + 4
    )
    assert golden_offset == model_offset + model_length
    assert golden_length == 40
    assert len(package) == golden_offset + golden_length
    assert zlib_crc32(package[model_offset:golden_offset]) == struct.unpack_from(
        "<I", package, 44
    )[0]
    assert hashlib.sha256(package[model_offset:golden_offset]).digest() == package[48:80]
    assert struct.unpack_from("<2f", package, golden_offset + 24) == (1.0, 2.0)
    assert package[golden_offset + 32] == 3
    assert struct.unpack_from("<f", package, golden_offset + 36)[0] == pytest.approx(0.4)
    assert zlib_crc32(package[:144]) == struct.unpack_from("<I", package, 144)[0]


@pytest.mark.parametrize("class_count", (1, 5))
def test_export_model_package_rejects_class_count_outside_runtime_bounds(
    tmp_path: Path,
    class_count: int,
) -> None:
    fake_package = SimpleNamespace(
        manifest={
            "class_names": list(FOUR_CLASS_NAMES),
            "feature_names": list(FEATURE_NAMES),
        },
        model=SimpleNamespace(class_names=FOUR_CLASS_NAMES),
        weights1_int8=np.zeros((len(FEATURE_NAMES), 8), dtype=np.int8),
        weights2_int8=np.zeros((8, class_count), dtype=np.int8),
    )
    vectors_path = tmp_path / "unused.csv"

    with patch(
        "ai.src.export_model_package.load_model_package",
        return_value=fake_package,
    ):
        with pytest.raises(ValueError, match="class_count must be between 2 and 4"):
            export_model_package(tmp_path / "model", tmp_path / "out.trmd", vectors_path)


def zlib_crc32(data: bytes) -> int:
    import zlib

    return zlib.crc32(data) & 0xFFFFFFFF
