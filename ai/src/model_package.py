"""Versioned host package and int8 tensor export for the firmware runtime."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path

import numpy as np

from ai.src.dataset_contract import FEATURE_NAMES, validate_class_names
from ai.src.train_model import MLPModel


MODEL_PACKAGE_VERSION = 2
RUNTIME_MODEL_VERSION = 2
RUNTIME_VERSION = "feature_mlp_runtime_v2"


@dataclass(frozen=True)
class ModelPackage:
    model: MLPModel
    manifest: dict[str, object]
    weights1_int8: np.ndarray
    weights2_int8: np.ndarray


def save_model_package(
    model: MLPModel,
    directory: Path,
    *,
    dataset_manifest_sha256: str,
    training_revision: str,
) -> None:
    """Write a reproducible package containing float reference and int8 tensors."""

    _validate_model(model)
    if len(dataset_manifest_sha256) != 64:
        raise ValueError("dataset manifest hash must be a SHA-256 hex string")
    directory = directory.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    weights_path = directory / "weights.npz"
    quantized = _quantize_model(model)
    np.savez(
        weights_path,
        feature_mean=model.feature_mean,
        feature_scale=model.feature_scale,
        weights1=model.weights1,
        bias1=model.bias1,
        weights2=model.weights2,
        bias2=model.bias2,
        input_scale=np.asarray(model.input_scale, dtype=np.float64),
        hidden_scale=np.asarray(model.hidden_scale, dtype=np.float64),
        weights1_int8=quantized["weights1_int8"],
        weights2_int8=quantized["weights2_int8"],
    )
    weights_sha256 = hashlib.sha256(weights_path.read_bytes()).hexdigest()
    manifest = {
        "package_version": MODEL_PACKAGE_VERSION,
        "model_version": RUNTIME_MODEL_VERSION,
        "runtime_version": RUNTIME_VERSION,
        "dataset_manifest_sha256": dataset_manifest_sha256,
        "training_revision": training_revision,
        "feature_version": "counts_v1",
        "feature_names": list(FEATURE_NAMES),
        "class_names": list(model.class_names),
        "input_shape": [len(FEATURE_NAMES)],
        "hidden_units": int(model.weights1.shape[1]),
        "quantization": "int8",
        "input_scale": model.input_scale,
        "input_zero_point": 0,
        "hidden_scale": model.hidden_scale,
        "hidden_zero_point": 0,
        "weight_scales": {
            "weights1": quantized["weights1_scale"],
            "weights2": quantized["weights2_scale"],
        },
        "weights_sha256": weights_sha256,
    }
    (directory / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def load_model_package(directory: Path) -> ModelPackage:
    directory = directory.resolve()
    try:
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("invalid model manifest") from error
    if manifest.get("package_version") != MODEL_PACKAGE_VERSION:
        raise ValueError("unsupported model package version")
    if manifest.get("model_version") != RUNTIME_MODEL_VERSION:
        raise ValueError("unsupported model runtime version")
    weights_path = directory / "weights.npz"
    actual_hash = hashlib.sha256(weights_path.read_bytes()).hexdigest()
    if actual_hash != manifest.get("weights_sha256"):
        raise ValueError("model weights CRC/hash check failed")
    try:
        with np.load(weights_path, allow_pickle=False) as values:
            model = MLPModel(
                class_names=tuple(manifest["class_names"]),
                feature_names=tuple(manifest["feature_names"]),
                feature_mean=np.asarray(values["feature_mean"], dtype=np.float64),
                feature_scale=np.asarray(values["feature_scale"], dtype=np.float64),
                weights1=np.asarray(values["weights1"], dtype=np.float64),
                bias1=np.asarray(values["bias1"], dtype=np.float64),
                weights2=np.asarray(values["weights2"], dtype=np.float64),
                bias2=np.asarray(values["bias2"], dtype=np.float64),
                input_scale=float(values["input_scale"]),
                hidden_scale=float(values["hidden_scale"]),
            )
            weights1_int8 = np.asarray(values["weights1_int8"], dtype=np.int8).copy()
            weights2_int8 = np.asarray(values["weights2_int8"], dtype=np.int8).copy()
    except (KeyError, OSError, ValueError) as error:
        raise ValueError("invalid model weights") from error
    _validate_model(model)
    if (
        weights1_int8.shape != model.weights1.shape
        or weights2_int8.shape != model.weights2.shape
    ):
        raise ValueError("quantized model tensor shapes are invalid")
    _validate_package_manifest(manifest, model)
    _validate_quantized_tensors(model, manifest, weights1_int8, weights2_int8)
    return ModelPackage(
        model=model,
        manifest=manifest,
        weights1_int8=weights1_int8,
        weights2_int8=weights2_int8,
    )


def predict_quantized(
    package: ModelPackage,
    features: np.ndarray,
) -> tuple[np.ndarray, np.ndarray, int, float]:
    """Run the float32/int8 arithmetic used by ``ai_runtime.c``."""

    values = np.asarray(features, dtype=np.float32)
    if values.ndim == 1:
        values = values.reshape(1, -1)
    if values.ndim != 2 or values.shape[1] != len(FEATURE_NAMES):
        raise ValueError(f"features must have shape (n, {len(FEATURE_NAMES)})")
    if not np.all(np.isfinite(values)):
        raise ValueError("features must contain only finite values")

    model = package.model
    feature_mean = np.asarray(model.feature_mean, dtype=np.float32)
    feature_scale = np.asarray(model.feature_scale, dtype=np.float32)
    input_scale = np.float32(model.input_scale)
    hidden_scale = np.float32(model.hidden_scale)
    weight_scales = package.manifest["weight_scales"]
    weights1_scale = np.float32(weight_scales["weights1"])
    weights2_scale = np.float32(weight_scales["weights2"])
    hidden_units = package.weights1_int8.shape[1]
    class_count = package.weights2_int8.shape[1]
    logits = np.zeros((len(values), class_count), dtype=np.float32)
    for row_index, row in enumerate(values):
        hidden = np.zeros(hidden_units, dtype=np.float32)
        for hidden_index in range(hidden_units):
            total = np.float32(model.bias1[hidden_index])
            for feature_index in range(len(FEATURE_NAMES)):
                normalized = np.float32(
                    (row[feature_index] - feature_mean[feature_index])
                    / feature_scale[feature_index]
                )
                input_q = _quantize_scalar(normalized, input_scale)
                input_value = np.float32(np.float32(input_q) * input_scale)
                weight = np.float32(
                    np.float32(package.weights1_int8[feature_index, hidden_index])
                    * weights1_scale
                )
                total = np.float32(total + np.float32(input_value * weight))
            activated = max(total, np.float32(0.0))
            hidden[hidden_index] = np.float32(
                np.float32(_quantize_scalar(activated, hidden_scale))
                * hidden_scale
            )
        for class_index in range(class_count):
            total = np.float32(model.bias2[class_index])
            for hidden_index in range(hidden_units):
                weight = np.float32(
                    np.float32(package.weights2_int8[hidden_index, class_index])
                    * weights2_scale
                )
                total = np.float32(total + np.float32(hidden[hidden_index] * weight))
            logits[row_index, class_index] = total

    maximum = np.max(logits, axis=1, keepdims=True)
    probabilities = np.exp(logits - maximum).astype(np.float32)
    probabilities /= np.sum(probabilities, axis=1, keepdims=True)
    class_index = int(np.argmax(logits[0]))
    return (
        logits[0].copy(),
        probabilities[0].copy(),
        class_index,
        float(probabilities[0, class_index]),
    )


def _quantize_scalar(value: np.float32, scale: np.float32) -> np.int8:
    quantized = np.float32(value / scale)
    if quantized > np.float32(127.0):
        return np.int8(127)
    if quantized < np.float32(-128.0):
        return np.int8(-128)
    rounded = (
        np.floor(quantized + np.float32(0.5))
        if quantized >= np.float32(0.0)
        else np.ceil(quantized - np.float32(0.5))
    )
    return np.int8(rounded)


def _validate_model(model: MLPModel) -> None:
    if model.feature_names != FEATURE_NAMES:
        raise ValueError("model feature order does not match counts_v1")
    try:
        validate_class_names(model.class_names, "model class names")
    except ValueError as error:
        raise ValueError(str(error)) from error
    if model.weights1.shape != (len(FEATURE_NAMES), model.bias1.shape[0]):
        raise ValueError("model first layer shape is invalid")
    if model.weights2.shape != (model.bias1.shape[0], len(model.class_names)):
        raise ValueError("model second layer shape is invalid")
    if model.bias2.shape != (len(model.class_names),):
        raise ValueError("model output bias shape is invalid")
    if model.feature_mean.shape != (len(FEATURE_NAMES),) or model.feature_scale.shape != (len(FEATURE_NAMES),):
        raise ValueError("model normalization shape is invalid")
    if not all(np.all(np.isfinite(values)) for values in (
        model.feature_mean,
        model.feature_scale,
        model.weights1,
        model.bias1,
        model.weights2,
        model.bias2,
    )):
        raise ValueError("model tensors must be finite")
    if np.any(model.feature_scale <= 0.0):
        raise ValueError("model feature scales must be positive")
    if not np.isfinite(model.input_scale) or model.input_scale <= 0.0:
        raise ValueError("model input scale must be positive")
    if not np.isfinite(model.hidden_scale) or model.hidden_scale <= 0.0:
        raise ValueError("model hidden scale must be positive")


def _validate_package_manifest(
    manifest: dict[str, object],
    model: MLPModel,
) -> None:
    expected_hidden_units = int(model.weights1.shape[1])
    if (
        manifest.get("runtime_version") != RUNTIME_VERSION
        or manifest.get("feature_version") != "counts_v1"
        or manifest.get("feature_names") != list(FEATURE_NAMES)
        or manifest.get("class_names") != list(model.class_names)
        or manifest.get("input_shape") != [len(FEATURE_NAMES)]
        or manifest.get("hidden_units") != expected_hidden_units
        or manifest.get("quantization") != "int8"
    ):
        raise ValueError("model manifest does not match the runtime contract")
    if not _same_float32(manifest.get("input_scale"), model.input_scale):
        raise ValueError("model manifest input scale does not match model weights")
    if not _same_float32(manifest.get("hidden_scale"), model.hidden_scale):
        raise ValueError("model manifest hidden scale does not match model weights")
    if manifest.get("input_zero_point") != 0 or manifest.get("hidden_zero_point") != 0:
        raise ValueError("model manifest zero points do not match the runtime contract")


def _validate_quantized_tensors(
    model: MLPModel,
    manifest: dict[str, object],
    weights1_int8: np.ndarray,
    weights2_int8: np.ndarray,
) -> None:
    expected = _quantize_model(model)
    if not np.array_equal(weights1_int8, expected["weights1_int8"]):
        raise ValueError("first-layer int8 tensor does not match float weights")
    if not np.array_equal(weights2_int8, expected["weights2_int8"]):
        raise ValueError("second-layer int8 tensor does not match float weights")
    weight_scales = manifest.get("weight_scales")
    if not isinstance(weight_scales, dict):
        raise ValueError("model manifest weight scales are missing")
    for name in ("weights1", "weights2"):
        if not _same_float32(weight_scales.get(name), expected[f"{name}_scale"]):
            raise ValueError(f"model manifest {name} scale does not match tensors")


def _same_float32(left: object, right: float) -> bool:
    try:
        return np.float32(left).tobytes() == np.float32(right).tobytes()
    except (TypeError, ValueError):
        return False


def _quantize_model(model: MLPModel) -> dict[str, object]:
    weights1_int8, weights1_scale = _symmetric_int8(model.weights1)
    weights2_int8, weights2_scale = _symmetric_int8(model.weights2)
    return {
        "weights1_int8": weights1_int8,
        "weights2_int8": weights2_int8,
        "weights1_scale": weights1_scale,
        "weights2_scale": weights2_scale,
    }


def _symmetric_int8(values: np.ndarray) -> tuple[np.ndarray, float]:
    maximum = float(np.max(np.abs(values)))
    scale = maximum / 127.0 if maximum > 0.0 else 1.0
    quantized = np.clip(np.rint(values / scale), -127, 127).astype(np.int8)
    return quantized, scale
