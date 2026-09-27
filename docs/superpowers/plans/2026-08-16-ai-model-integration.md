# AI model integration: host training and H743 inference

> This plan is for the first end-to-end TinyML slice. It deliberately keeps the
> existing deterministic trigger and TERP export protocol unchanged.

## Goal

Turn the current EV03 event data and `counts_v1` feature baseline into a
reproducible, versioned, int8 feature+MLP package that can be evaluated on the
host and consumed by a low-priority STM32H743 task. The first pilot is binary:
`background` versus `impact`. The existing rule baseline remains the fallback
and remains useful for comparison.

## Frozen v1 contract

- Hardware: STM32H743 + ICM45686, 1600 Hz, 16g accelerometer, 2000 dps gyro.
- Event input: one complete EV03 event, exactly 2400 samples (1.5 s). The
  trigger index remains metadata; it is not an input feature in this route.
- Axis order: `ax, ay, az, gx, gy, gz`, in the signed raw count domain used by
  the host decoder and the firmware driver.
- Feature version: `counts_v1`, six values in this order:
  `accel_peak_magnitude_counts`, `accel_peak_to_peak_counts`,
  `accel_rms_magnitude_counts`, `duration_seconds`, `accel_crest_factor`,
  `gyro_peak_magnitude_counts`.
- Pilot class order: `[background, impact]`. Other labels remain reserved for
  later collection and are not silently treated as training examples.
- Model route: small fully-connected MLP on the six features. The deterministic
  trigger still decides when an event is captured; the model only classifies a
  completed event.
- Quantization: signed int8 weights/activations with an explicit manifest.
  Numeric scales and zero-points are generated from training-only calibration,
  never guessed or calibrated from validation/test data.
- Runtime rule: no feature extraction or inference in the acquisition ISR or
  high-rate producer path; copy/enqueue a completed event to a low-priority AI
  worker. On package/version/CRC/runtime failure, report the failure and use the
  rule baseline/fallback status.

## Work items

### 1. Freeze and validate the dataset contract

1. Add the frozen event-window and model metadata to
   `ai/configs/dataset_v1.yaml` without embedding private raw-data paths.
2. Add a versioned manifest schema for curated event IDs, labels, session IDs,
   source hashes, and quality decisions. Reject missing IDs, data loss, wrong
   sample rate, wrong length, duplicate session leakage, and saturation above
   the configured threshold.
3. Add tests in `ai/tests/test_dataset_contract.py` first. Run the tests to
   observe the expected failures, then implement the smallest validator.
4. Record the pilot selection and the deliberate limitations in
   `evidence/hardware-bringup/2026-08-16/` and `docs/ai/`.

### 2. Implement host feature/model training (test first)

1. Add tests in `ai/tests/test_model.py` for deterministic input ordering,
   train-only normalization/calibration, stable class ordering, deterministic
   inference, and malformed package rejection.
2. Add a dependency-light NumPy trainer in `ai/src/train_model.py` for the
   six-feature pilot: deterministic seed, train/validation/test split by
   session, standardization parameters from train only, a tiny MLP, and metrics
   including confusion matrix and per-class recall.
3. Add `ai/src/model_package.py` to define the serializable model manifest and
   strict schema/version checks. The package must contain model version, dataset
   manifest hash, training revision, feature version/order, class order, input
   shape, quantization parameters, runtime version, model bytes CRC, and eval
   summary hash.
4. Add `ai/src/quantize_model.py` to quantize the trained parameters to int8 and
   export a deterministic binary/header package. Calibration must use only the
   training split and the exported values must round-trip through host
   inference.
5. Add a command-line entry point under `ai/tools/` or the existing project
   script convention that produces `artifacts/ai/<model_version>/` without
   committing raw private data or generated binaries by default.

### 3. Implement firmware feature extraction and runtime (test first)

1. Add native C tests in `firmware/tests/native/test_ai_features.c` for the six
   feature formulas, 2400-sample acceptance, overflow-safe accumulation, and
   parity with host golden vectors.
2. Add `firmware/app/ai_inference/ai_features.{c,h}`. It consumes a completed
   event record/sample blocks, computes the frozen `counts_v1` vector with
   bounded integer/float operations, and never allocates.
3. Add native C tests in `firmware/tests/native/test_ai_runtime.c` for int8
   dequantization/activation, class ordering, package CRC/version rejection,
   and deterministic fallback behavior.
4. Add `firmware/app/ai_inference/ai_runtime.{c,h}` for the tiny MLP and package
   checks. Keep the worker interface independent of the TERP wire protocol.
5. Add the generated model data only after host export and golden-vector
   checks pass; keep model bytes and manifest in a dedicated generated-data
   unit with a compile-time size/CRC assertion.

### 4. Connect the completed-event boundary safely

1. Add a bounded AI queue/worker in `firmware/app/ai_inference/` with a low
   priority and explicit queue-full/error counters.
2. At the existing `EVENT_READY_FOR_EXPORT` boundary, copy the completed event
   (or its six features) before blocks are released. Do not block the event
   export thread on neural inference and do not change UART3/TERP frames.
3. Expose read-only AI status (model version, last class/confidence, queue
   drops, package/runtime errors) through the existing diagnostic/status path,
   with tests for the unconfigured/fallback state.
4. Add an integration test using a synthetic completed event to verify that
   export remains possible while the AI worker runs.

### 5. Golden vectors, build, and acceptance

1. Generate a committed small golden-vector fixture from the host package (no
   private raw event files) containing six input features and expected logits,
   probabilities, class, and confidence.
2. Make host Python and native C tests consume the same fixture and require
   exact/declared-tolerance parity.
3. Run the narrow checks first: `python -m pytest ai/tests -q`, native firmware
   tests, then `scons -C firmware -j4`, and finally `git diff --check`.
4. Do not flash hardware automatically. Flash/HIL validation is a separate
   explicit step after the package, build, and evidence gates pass.

## Acceptance gates

- Contract validator rejects wrong length/rate/order and session leakage.
- Host training is deterministic from a fixed seed and never uses test data to
  set normalization or quantization parameters.
- The exported manifest and binary pass version/shape/CRC checks.
- Host and C golden vectors agree within the documented numeric tolerance.
- Firmware build and native tests pass; the existing event export path remains
  unchanged and no acquisition-path timing regression is introduced.
- Hardware inference is marked unverified until a separately recorded UART3 /
  HIL run confirms model status and fallback behavior.

## Commands

```powershell
python -m pytest ai/tests -q
python -m pytest host/tests -q
scons -C firmware -j4
git diff --check
```
