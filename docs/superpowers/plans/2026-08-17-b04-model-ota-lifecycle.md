# B04 Model OTA Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a validated model-package format, resumable inactive-slot upload, atomic A/B activation metadata, runtime loading/golden-vector verification, and a TERP/host path while preserving the previously active model across incomplete or invalid updates.

**Architecture:** The model package is a fixed little-endian header followed by the canonical `ai_runtime` model payload and bounded golden vectors. Firmware validates the complete package before writing a second copy of the model-state record; the old state record and old model slot remain untouched until that commit succeeds. The existing ordered `ota_download_service` is extended with an optional package validator, and the model lifecycle adapts it to the inactive QSPI slot. TERP model messages reuse the existing chunk/progress framing and do not share the firmware candidate state machine.

**Tech Stack:** C11/RT-Thread, existing QSPI HAL callbacks, existing CRC32/SHA-256 components, native GCC tests, Python 3.12 host package exporter/client, generated TERP message IDs.

---

### Task 1: Freeze and test the model package contract

**Files:**
- Create: `firmware/app/ota/ota_model_package.h`
- Create: `firmware/app/ota/ota_model_package.c`
- Create: `firmware/tests/native/test_ota_model_package.c`
- Create: `ai/src/export_model_package.py`
- Create: `ai/tests/test_export_model_package.py`
- Modify: `firmware/tests/native/SConstruct`
- Modify: `firmware/app/SConscript` only if the existing application glob does not include the new module

- [x] **Step 1: Write failing native tests** for a valid package, missing/zero contract fields, header CRC failure, model CRC/SHA failure, input/output golden hash failure, and a golden inference mismatch.
- [x] **Step 2: Write failing host tests** that export the checked-in pilot model plus `ai/tests/fixtures/ai_golden_vectors.csv`, then assert the fixed header fields, little-endian payload, model hash, and both golden hashes.
- [x] **Step 3: Implement the fixed 160-byte `TRMD` header and bounded reader-based validator.** Required checks are exact package/header lengths, `runtime_version == 2`, `feature_version == counts_v1`, input shape `[6]`, int8 quantization, nonzero dimensions, model length/CRC/SHA, and nonzero golden count/input-output hashes. Decode the canonical payload into an `ai_model_t`, run `ai_runtime_validate_model`, hash the input/output portions of every golden record, and compare inference logits/class/confidence with a fixed tolerance.
- [x] **Step 4: Implement the host exporter** using the same canonical model byte order as `export_c_model.py`; pack each golden record as six input `f32`, two expected logits `f32`, class `u8`, three reserved bytes, and confidence `f32`.
- [x] **Step 5: Run the targeted native executable and Python tests.** Expected: `ota model package: PASS` and all new host tests pass.

### Task 2: Add model A/B lifecycle and runtime selection

**Files:**
- Create: `firmware/app/ota/ota_model_lifecycle.h`
- Create: `firmware/app/ota/ota_model_lifecycle.c`
- Create: `firmware/tests/native/test_ota_model_lifecycle.c`
- Modify: `firmware/app/ota/ota_download_service.h`
- Modify: `firmware/app/ota/ota_download_service.c`
- Modify: `firmware/app/ota/ota_qspi_candidate.h`
- Modify: `firmware/app/ota/ota_qspi_candidate.c`
- Modify: `firmware/app/ai_inference/ai_runtime.h`
- Modify: `firmware/app/ai_inference/ai_runtime.c`
- Modify: `firmware/tests/native/SConstruct`

- [x] **Step 1: Write failing lifecycle tests** for mount with blank metadata, valid upload to inactive A/B, interrupted upload retaining the old active slot, invalid feature/CRC/SHA/golden package retaining the old slot, atomic metadata-write failure retaining the old slot, reboot remount, and both-invalid fallback.
- [x] **Step 2: Extend `ota_download_storage_t` with an optional `validate` callback** so existing firmware OTA behavior remains unchanged while model finalization can validate its own package before invoking the existing pending/activation callback.
- [x] **Step 3: Implement model state records** in the reserved model-state QSPI sector with generation, active slot, package length/model version/CRC/SHA, golden hashes, CRC, and commit marker. Mount chooses the newest valid record whose referenced package validates; activation writes the alternate metadata record and verifies readback before changing the in-memory active slot.
- [x] **Step 4: Implement model lifecycle adapters** that select the inactive slot, erase/write/read it through the existing QSPI bus lock, expose begin/write/query/finalize/cancel, and return the embedded static model as the rule/fallback path when neither slot is valid.
- [x] **Step 5: Add `ai_runtime_decode_model`** using caller-owned bounded buffers so an active package can become an `ai_model_t` without dynamic allocation; infer only after CRC and golden validation.
- [x] **Step 6: Run native lifecycle, package, AI runtime, and existing OTA tests.** Expected: all targeted executables pass and existing firmware OTA tests remain unchanged.

### Task 3: Expose model upload over TERP and the host client

**Files:**
- Modify: `protocol/terp_messages.yaml`
- Regenerate: `firmware/components/protocol/terp_messages_generated.h`
- Regenerate: `host/transport_recorder/protocol/messages_generated.py`
- Modify: `firmware/components/protocol/terp_device.h`
- Modify: `firmware/components/protocol/terp_device.c`
- Modify: `firmware/app/transport/terp_service.h`
- Modify: `firmware/app/transport/terp_service.c`
- Modify: `host/transport_recorder/protocol/client.py`
- Modify: `firmware/tests/native/test_terp_device.c`
- Modify: `firmware/tests/native/test_terp_service.c`
- Modify: `host/tests/test_terp_client.py`

- [x] **Step 1: Add generated IDs** `MODEL_OTA_BEGIN`, `MODEL_OTA_WRITE_CHUNK`, `MODEL_OTA_QUERY`, `MODEL_OTA_FINALIZE`, and `MODEL_OTA_CANCEL` in the unused `0x0110..0x0114` range, preserving the existing firmware/recovery IDs.
- [x] **Step 2: Add device operation callbacks** matching the existing OTA progress/chunk signatures and route the five model messages through the common request/error encoder, including handshake and malformed-payload checks.
- [x] **Step 3: Wire `terp_service` to `ota_model_lifecycle`** using model A/B QSPI callbacks and advertise a new model-OTA capability bit only when model storage initialization succeeds.
- [x] **Step 4: Add host client methods** for begin/write/query/finalize/cancel with CRC-protected ordered chunks and strict progress parsing; do not make `COM8/UART1` a model transport assumption because TERP remains UART3.
- [x] **Step 5: Run generated-file checks, native TERP tests, and targeted host protocol tests.** Expected: generator `--check` passes and no existing OTA/AI-result protocol behavior changes.

### Task 4: Documentation, review, and hardware handoff

**Files:**
- Modify: `docs/reviews/project-issue-ledger.md`
- Modify: `docs/superpowers/plans/2026-08-06-phase11-board-validation.md` only if the implemented contract needs a cross-reference

- [x] **Step 1: Record exact software evidence** (package format version, state-record commit ordering, fallback semantics, native/host commands, and the fact that QSPI/TERP hardware was not yet exercised).
- [x] **Step 2: Run `git diff --check`, memory-map/protocol/golden checks, targeted native tests, available host tests, and the full SCons firmware build; record the remaining hardware gates without claiming them.**
- [x] **Step 3: Send the B04 slice to the reviewer thread for a short review, read the response, and fix any P1/P2 findings in this same worktree before finalizing the ledger.**
- [x] **Step 4: Leave only manual COM8/UART1 FinSH checks, QSPI model upload/rollback, power-cycle interruption, and long-run stress in the final handoff.**

**Coverage gap to preserve:** This software slice cannot close physical QSPI timing, real interrupted writes, watchdog/power-loss behavior, or final model generalization; those remain hardware/C-11 evidence gates even if all native tests pass.
