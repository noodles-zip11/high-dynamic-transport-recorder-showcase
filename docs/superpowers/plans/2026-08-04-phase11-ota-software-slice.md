# Phase 11 OTA Software Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a board-profiled, offline-verifiable firmware and model OTA software slice that cannot overwrite the running application before a complete package is validated.

**Architecture:** The normal RT-Thread application accepts TERP update chunks only into a QSPI staging abstraction. A minimal standalone Bootloader owns package verification, resumable internal-Flash copying, trial accounting, and rollback selection. Package parsing, state encoding, and QSPI layout calculations remain pure C so native tests can inject every write/read/power-loss result.

**Tech Stack:** C11, Python 3.12 standard library, SCons, STM32H743VIT6 internal Flash, QUADSPI NOR, CRC32, SHA-256, TERP.

---

## Locked boundaries

- Target board profile is the user-provided STM32H743VIT6 25 MHz demo-board material. The schematic identifies U3 as W25Q64: `PB10` is nCS, `PB2` is CLK, and `PD11`/`PD12`/`PE2`/`PD13` are IO0..IO3. It is an 8 MiB, 24-bit-address device with 256-byte pages and 4 KiB erase sectors. The v1 Bootloader uses conservative 1-1-1 commands only (`0x03` read, `0x02` program, `0x20` erase), `FlashSize=22`, a maximum 40 MHz QSPI clock, and refuses erase/program until JEDEC `0xEF4017` is read. This profile is isolated in one board header; no event-log SPI NOR, IMU, clock, DMA, or existing read-event TERP behavior changes.
- Internal Flash reserves `0x08000000..0x0801FFFF` for Bootloader, `0x08020000..0x081BFFFF` for the application, `0x081C0000..0x081DFFFF` for the primary state sector, and `0x081E0000..0x081FFFFF` for the secondary state sector. Each state sector is a distinct 128 KiB H743 erase unit; the checker rejects overlap and any image larger than the application region.
- Firmware packages use a fixed 196-byte little-endian binary manifest: magic, format version, header bytes, product ID, hardware ID, firmware version, minimum Bootloader version, target address, image length, CRC32, SHA-256, and reserved zero bytes. V1 has integrity verification but deliberately has no signature/authentication claim.
- The 8 MiB QSPI map is erase-aligned: metadata `0x000000..0x00FFFF`; firmware candidate `0x010000..0x10FFFF`; firmware recovery `0x110000..0x20FFFF`; model A `0x210000..0x507FFF`; model B `0x508000..0x7FFFFF`. Each firmware package slot is 1 MiB, so package creation rejects payloads larger than the slot minus its 196-byte manifest (`0x000FFF3C`). Each model package slot is `0x2F8000` bytes.
- A Bootloader state record is valid only when magic/version/header bytes/CRC/commit marker pass. Two copies select the largest valid generation. State progression is `NORMAL -> PENDING_INSTALL -> INSTALLING -> TRIAL -> CONFIRMED` or `ROLLBACK_REQUIRED -> INSTALLING`; invalid state records enter `RECOVERY`.
- Normal update transport is app-side TERP over the existing UART3 byte transport. It supports begin, write chunk, query progress, finalize, cancel, and result. A repeated verified chunk at the same offset is accepted only when bytes and CRC match.
- Model packages use the same integrity envelope and inactive A/B slot selection, but runtime activation remains an explicit Phase 10 interface: this slice validates/stores/chooses a slot and never claims an AI inference engine is deployed.

## File structure

- Create `firmware/config/memory_layout.h`: the C constants mirrored by the layout file.
- Create `config/memory_layout.yaml`: human-readable regions and package limits.
- Create `tools/check_memory_map.py`: stdlib-only parser/checker for the restricted `key: value` layout file.
- Create `tools/package_firmware.py` and `tools/inspect_package.py`: deterministic package creation and inspection.
- Create `firmware/components/ota/ota_crc32.{c,h}`, `ota_sha256.{c,h}`, `ota_package.{c,h}`, `ota_state.{c,h}`, and `ota_slots.{c,h}`: pure, allocation-free package/state/slot logic.
- Create `firmware/tests/native/test_ota_package.c`, `test_ota_state.c`, and `test_ota_slots.c`; extend `firmware/tests/native/SConstruct` to build and execute them.
- Create `bootloader/SConstruct`, `bootloader/link.lds`, and `bootloader/src/*`: standalone reset path, image verifier, Flash-copy state machine, QSPI board adapter, and application jump module. It must not import RT-Thread.
- Modify `protocol/terp_messages.yaml`, `firmware/components/protocol/terp_messages_generated.h`, and host protocol code only to add OTA message IDs and serialisation. Existing IDs/frames remain byte-for-byte compatible.
- Create `firmware/app/ota/ota_download_service.{c,h}` and its native test. It receives validated TERP chunks and depends on a QSPI writer interface, not a HAL handle.
- Create `docs/decisions/phase11-ota-boundaries.md` and `evidence/phase11-ota/README.md`: record exact software proof and all required board tests.

## Tasks

### Task 1: Freeze map and deterministic firmware package

**Files:** create the memory-layout/config/tool files, package tools, package native tests.

- [ ] Write tests that reject overlapping regions, firmware packages above the 1 MiB QSPI slot, empty product/version fields, altered payload CRC, altered payload SHA-256, and non-zero reserved manifest bytes.
- [ ] Run those tests and observe missing-module failures.
- [ ] Implement the restricted layout parser and package envelope without third-party dependencies; emit the same bytes for the same inputs.
- [ ] Run targeted package/layout tests, then `pwsh scripts/run_tests.ps1`.

### Task 2: OTA core and fault-recoverable state records

**Files:** create `ota_*` core modules and native tests; extend native SCons.

- [ ] Write failing tests for double-record generation selection, torn newest record fallback, illegal transition rejection, trial counter overflow, candidate/backup non-overlap, and model inactive-slot selection.
- [ ] Implement CRC32/SHA-256 streaming interfaces, fixed manifest decoding, committed state records, and slot-bound validation with no heap allocation.
- [ ] Run each new native executable and the complete native suite.

### Task 3: Standalone Bootloader build and application handoff

**Files:** create all `bootloader/` sources, SConstruct, and linker script; add bootloader build test/check script.

- [ ] Write native tests for application vector validity: aligned RAM MSP, Thumb reset handler inside the application range, invalid manifest rejection, and state-dependent boot action selection.
- [ ] Implement the Bootloader around injectable Flash/QSPI operations; only the board adapter owns STM32 HAL/register calls.
- [ ] Build the standalone ELF; assert Bootloader size is at most 128 KiB and application link origin is `0x08020000`.

### Task 4: App-side TERP staging and model slots

**Files:** modify TERP schema/generated declarations/host client; create `ota_download_service`; tests for service and Python client.

- [ ] Write failing tests for begin/write/query/finalize/cancel, chunk replay, out-of-order rejection, final whole-package verification, and model-slot activation only after package validity.
- [ ] Add the six new OTA messages in an unused message-ID range; preserve all legacy framing and existing message IDs.
- [ ] Implement only staging writes and status responses in the application. Installation remains Bootloader-owned after reboot.
- [ ] Run targeted C/Python tests, regenerate golden frames if schema changes, then run the full project suite.

### Task 5: Evidence and complete software verification

**Files:** create decision/evidence documents; update package/version documentation if required.

- [ ] Record the exact QSPI profile adopted from vendor material, plus the conflict-resolving rationale and required JEDEC proof.
- [ ] Record the hardware matrix: JEDEC, read/write/erase, normal update, corrupted package, 10 disconnect/resume cases, 30 install power cuts, trial reset, model golden-input failure, and recovery communication.
- [ ] Run HostOnly selfcheck, native tests, Python tests, Bootloader build, application firmware build, and `git diff --check`.
- [ ] Do not commit, merge, push, or claim board acceptance without explicit user authorization and real evidence.
