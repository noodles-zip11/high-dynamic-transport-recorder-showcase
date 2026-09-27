"""Generate a small, reviewable C model-data unit for the H743 runtime."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import zlib

import numpy as np

from ai.src.dataset_contract import FEATURE_NAMES
from ai.src.model_package import RUNTIME_MODEL_VERSION, load_model_package


def export_c_model(
    package_directory: Path,
    header_path: Path,
    source_path: Path,
    *,
    symbol: str = "transport_ai_model_v1",
) -> None:
    package = load_model_package(package_directory)
    model = package.model
    weights1 = np.asarray(package.weights1_int8, dtype=np.int8).T
    weights2 = np.asarray(package.weights2_int8, dtype=np.int8)
    weight_scales = package.manifest["weight_scales"]
    weights1_scale = float(weight_scales["weights1"])
    weights2_scale = float(weight_scales["weights2"])
    model_crc32 = zlib.crc32(
        _canonical_model_bytes(
            model,
            weights1,
            weights2,
            weights1_scale,
            weights2_scale,
        )
    ) & 0xFFFFFFFF
    header_path = header_path.resolve()
    source_path = source_path.resolve()
    header_path.parent.mkdir(parents=True, exist_ok=True)
    source_path.parent.mkdir(parents=True, exist_ok=True)

    header_path.write_text(
        f"""#ifndef TRANSPORT_RECORDER_AI_MODEL_DATA_H
#define TRANSPORT_RECORDER_AI_MODEL_DATA_H

#include \"ai_runtime.h\"

extern const ai_model_t {symbol};

#endif
""",
        encoding="utf-8",
    )
    source_path.write_text(
        _render_source(
            symbol,
            model,
            weights1,
            weights2,
            weights1_scale,
            weights2_scale,
            model_crc32,
        ),
        encoding="utf-8",
    )


def _render_source(
    symbol: str,
    model,
    weights1: np.ndarray,
    weights2: np.ndarray,
    weights1_scale: float,
    weights2_scale: float,
    model_crc32: int,
) -> str:
    hidden_units = weights1.shape[0]
    class_count = weights2.shape[1]
    if hidden_units > 64 or class_count > 4:
        raise ValueError("model is larger than the firmware runtime limits")
    lines = [
        '#include "ai_model_data.h"',
        "",
        f"static const float {symbol}_feature_mean[AI_FEATURE_COUNT] = "
        f"{{{_float_list(model.feature_mean)}}};",
        f"static const float {symbol}_feature_scale[AI_FEATURE_COUNT] = "
        f"{{{_float_list(model.feature_scale)}}};",
        f"static const int8_t {symbol}_weights1[AI_FEATURE_COUNT * {hidden_units}] = "
        f"{{{_int_list(weights1)}}};",
        f"static const float {symbol}_bias1[{hidden_units}] = "
        f"{{{_float_list(model.bias1)}}};",
        f"static const int8_t {symbol}_weights2[{hidden_units} * {class_count}] = "
        f"{{{_int_list(weights2)}}};",
        f"static const float {symbol}_bias2[{class_count}] = "
        f"{{{_float_list(model.bias2)}}};",
        f"const ai_model_t {symbol} = {{",
        "    .magic = AI_MODEL_MAGIC,",
        "    .version = AI_MODEL_VERSION,",
        "    .feature_count = AI_FEATURE_COUNT,",
        f"    .hidden_units = {hidden_units}U,",
        f"    .class_count = {class_count}U,",
        f"    .input_scale = {_float_literal(model.input_scale)},",
        "    .input_zero_point = 0.0F,",
        f"    .hidden_scale = {_float_literal(model.hidden_scale)},",
        "    .hidden_zero_point = 0.0F,",
        f"    .feature_mean = {symbol}_feature_mean,",
        f"    .feature_scale = {symbol}_feature_scale,",
        f"    .weights1 = {symbol}_weights1,",
        f"    .weights1_scale = {_float_literal(weights1_scale)},",
        f"    .bias1 = {symbol}_bias1,",
        f"    .weights2 = {symbol}_weights2,",
        f"    .weights2_scale = {_float_literal(weights2_scale)},",
        f"    .bias2 = {symbol}_bias2,",
        f"    .model_crc32 = UINT32_C(0x{model_crc32:08X}),",
        "};",
        "",
    ]
    return "\n".join(lines)


def _float_literal(value: float) -> str:
    formatted = f"{float(np.float32(value)):.9g}"
    if "." not in formatted and "e" not in formatted.lower():
        formatted += ".0"
    return f"{formatted}F"


def _float_list(values: np.ndarray) -> str:
    return ", ".join(_float_literal(value) for value in np.asarray(values).ravel())


def _int_list(values: np.ndarray) -> str:
    return ", ".join(str(int(value)) for value in np.asarray(values).ravel())


def _canonical_model_bytes(
    model,
    weights1: np.ndarray,
    weights2: np.ndarray,
    weights1_scale: float,
    weights2_scale: float,
) -> bytes:
    """Serialize the fields used by ai_runtime_model_crc32 in fixed order."""

    chunks = [struct.pack(
        "<I H 4B",
        0x314D4941,
        RUNTIME_MODEL_VERSION,
        len(FEATURE_NAMES),
        weights1.shape[0],
        weights2.shape[1],
        0,
    )]
    chunks.extend((
        _pack_float(model.input_scale),
        _pack_float(0.0),
        _pack_float(model.hidden_scale),
        _pack_float(0.0),
    ))
    for mean, scale in zip(model.feature_mean, model.feature_scale):
        chunks.extend((_pack_float(mean), _pack_float(scale)))
    chunks.append(np.asarray(weights1, dtype=np.int8).tobytes(order="C"))
    chunks.append(_pack_float(weights1_scale))
    chunks.append(np.asarray(model.bias1, dtype="<f4").tobytes(order="C"))
    chunks.append(np.asarray(weights2, dtype=np.int8).tobytes(order="C"))
    chunks.append(_pack_float(weights2_scale))
    chunks.append(np.asarray(model.bias2, dtype="<f4").tobytes(order="C"))
    return b"".join(chunks)


def _pack_float(value: float) -> bytes:
    return struct.pack("<f", float(np.float32(value)))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    export_c_model(args.package, args.header, args.source)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
