"""Export the canonical binary model package consumed by the firmware OTA path."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import zlib

import numpy as np

from ai.src.dataset_contract import (
    FEATURE_NAMES,
    MAX_CLASS_COUNT,
    validate_class_names,
)
from ai.src.export_c_model import _canonical_model_bytes
from ai.src.golden_vectors import load_golden_vectors
from ai.src.model_package import RUNTIME_MODEL_VERSION, load_model_package


MODEL_PACKAGE_MAGIC = b"TRMD"
MODEL_PACKAGE_FORMAT_VERSION = 1
MODEL_PACKAGE_HEADER_BYTES = 160
MODEL_PACKAGE_QUANTIZATION_INT8 = 1
MODEL_PACKAGE_GOLDEN_RECORD_BYTES = 40
MODEL_PACKAGE_MAX_GOLDEN_VECTORS = 8
MODEL_PACKAGE_FEATURE_VERSION = 1


def export_model_package(
    package_directory: Path,
    output_path: Path,
    golden_vectors_path: Path,
    *,
    model_version: int | None = None,
) -> Path:
    """Export a model package with hashes and MCU-checkable golden vectors."""

    package = load_model_package(package_directory)
    class_names = validate_class_names(
        package.manifest.get("class_names"), "model manifest class names"
    )
    class_count = int(package.weights2_int8.shape[1])
    if class_count < 2 or class_count > MAX_CLASS_COUNT:
        raise ValueError(
            f"class_count must be between 2 and {MAX_CLASS_COUNT}"
        )
    if class_count != len(class_names):
        raise ValueError("model class_count does not match manifest class_names")
    if tuple(package.model.class_names) != class_names:
        raise ValueError("model class_names do not match manifest class_names")
    vectors = load_golden_vectors(golden_vectors_path, class_names=class_names)
    if len(vectors) > MODEL_PACKAGE_MAX_GOLDEN_VECTORS:
        raise ValueError("too many golden vectors for the firmware package")
    if len(package.manifest["feature_names"]) != len(FEATURE_NAMES):
        raise ValueError("model feature count does not match counts_v1")

    weights1 = np.asarray(package.weights1_int8, dtype=np.int8).T
    weights2 = np.asarray(package.weights2_int8, dtype=np.int8)
    scales = package.manifest["weight_scales"]
    canonical = _canonical_model_bytes(
        package.model,
        weights1,
        weights2,
        float(scales["weights1"]),
        float(scales["weights2"]),
    )
    runtime_crc32 = zlib.crc32(canonical) & 0xFFFFFFFF
    model_bytes = canonical + struct.pack("<I", runtime_crc32)
    golden_bytes = b"".join(
        struct.pack(
            "<6f2fB3xf",
            *np.asarray(vector.features, dtype=np.float32),
            *np.asarray(vector.logits[:2], dtype=np.float32),
            vector.class_index,
            np.float32(vector.confidence),
        )
        for vector in vectors
    )
    input_hash = hashlib.sha256(
        b"".join(
            golden_bytes[index:index + 24]
            for index in range(0, len(golden_bytes), MODEL_PACKAGE_GOLDEN_RECORD_BYTES)
        )
    ).digest()
    output_hash = hashlib.sha256(
        b"".join(
            golden_bytes[index + 24:index + MODEL_PACKAGE_GOLDEN_RECORD_BYTES]
            for index in range(0, len(golden_bytes), MODEL_PACKAGE_GOLDEN_RECORD_BYTES)
        )
    ).digest()

    selected_model_version = (
        int(package.manifest["model_version"])
        if model_version is None
        else int(model_version)
    )
    package_length = MODEL_PACKAGE_HEADER_BYTES + len(model_bytes) + len(golden_bytes)
    header = bytearray(MODEL_PACKAGE_HEADER_BYTES)
    header[0:4] = MODEL_PACKAGE_MAGIC
    struct.pack_into("<H", header, 4, MODEL_PACKAGE_FORMAT_VERSION)
    struct.pack_into("<H", header, 6, MODEL_PACKAGE_HEADER_BYTES)
    struct.pack_into("<I", header, 8, package_length)
    struct.pack_into("<H", header, 12, selected_model_version)
    struct.pack_into("<H", header, 14, RUNTIME_MODEL_VERSION)
    struct.pack_into("<H", header, 16, MODEL_PACKAGE_FEATURE_VERSION)
    header[18:22] = bytes((
        len(FEATURE_NAMES),
        int(weights1.shape[0]),
        class_count,
        MODEL_PACKAGE_QUANTIZATION_INT8,
    ))
    header[22] = 1
    struct.pack_into("<H", header, 24, len(FEATURE_NAMES))
    struct.pack_into("<H", header, 26, len(vectors))
    struct.pack_into("<I", header, 28, MODEL_PACKAGE_HEADER_BYTES)
    struct.pack_into("<I", header, 32, len(model_bytes))
    struct.pack_into("<I", header, 36, MODEL_PACKAGE_HEADER_BYTES + len(model_bytes))
    struct.pack_into("<I", header, 40, len(golden_bytes))
    struct.pack_into("<I", header, 44, zlib.crc32(model_bytes) & 0xFFFFFFFF)
    header[48:80] = hashlib.sha256(model_bytes).digest()
    header[80:112] = input_hash
    header[112:144] = output_hash
    struct.pack_into("<I", header, 144, zlib.crc32(header[:144]) & 0xFFFFFFFF)

    output_path = output_path.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(bytes(header) + model_bytes + golden_bytes)
    return output_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--golden-vectors", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--model-version", type=int)
    args = parser.parse_args()
    export_model_package(
        args.package,
        args.output,
        args.golden_vectors,
        model_version=args.model_version,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
