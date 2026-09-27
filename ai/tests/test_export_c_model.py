from __future__ import annotations

from pathlib import Path

import numpy as np

from ai.src.export_c_model import _float_literal, export_c_model
from ai.src.model_package import save_model_package
from ai.src.train_model import MLPTrainingConfig, train_mlp


def test_c_model_export_uses_firmware_hidden_major_layout(tmp_path: Path) -> None:
    features = np.asarray([
        [0, 0, 0, 1, 0, 0],
        [1, 0, 0, 1, 0, 0],
        [2, 0, 0, 1, 0, 0],
        [10, 1, 0, 1, 2, 0],
        [11, 1, 0, 1, 2, 0],
        [12, 1, 0, 1, 2, 0],
    ], dtype=np.float64)
    labels = ("background",) * 3 + ("impact",) * 3
    groups = ("bg1", "bg2", "bg3", "i1", "i2", "i3")
    result = train_mlp(features, labels, groups,
                       config=MLPTrainingConfig(seed=1, epochs=2, hidden_units=2))
    package = tmp_path / "package"
    save_model_package(result.model, package,
                       dataset_manifest_sha256="a" * 64,
                       training_revision="test")
    header = tmp_path / "ai_model_data.h"
    source = tmp_path / "ai_model_data.c"

    export_c_model(package, header, source)

    text = source.read_text(encoding="utf-8")
    assert "const ai_model_t transport_ai_model_v1" in text
    assert "weights1[AI_FEATURE_COUNT * 2]" in text
    assert "model_crc32" in text
    assert "tensor_bytes" not in text
    assert header.read_text(encoding="utf-8").startswith("#ifndef")


def test_c_float_literal_keeps_decimal_for_integer_valued_float() -> None:
    assert _float_literal(1.0) == "1.0F"
