# Phase 2 real four-class AI controlled pilot

## Scope

This evidence covers the production-relevant AI delta only: the four-class
dataset contract and pilot split, feature/model training pipeline, int8 export,
firmware model data, four-class TRMD compatibility, and the board model-loading
gate. Timed button collection, LED campaign control, quota handling, board
button mappings, and low-power/power-loss work are intentionally outside the
AI integration commit.

## Frozen data and evaluation

- Class order: `background`, `impact`, `continuous_vibration`, `drop`.
- Eligible events: 91 total (25 / 24 / 20 / 22 respectively).
- Dataset manifest SHA-256:
  `C9F72B9F06A79EA7AB74CDBDD963AB450A817EDB27F0F6C6F099F112BE8E1C08`.
- Explicit controlled-pilot split: 64 train, 14 validation, 13 test;
  `within_session_pilot=true`.
- Release int8 test accuracy: `0.7692308` (10/13).
- Release int8 test macro F1: `0.755952`.
- Float/int8 test prediction parity: `1.0`.
- Test recall: background `0.75`, impact `1.0`, continuous vibration
  `0.3333`, drop `1.0`.
- Model CRC32: `0xAD10980E`.
- Model weights SHA-256:
  `F4EF4C310F808F0DC76E59E4F2296FC97D5A90118C73506E34C995E47E53FB78`.

Hyperparameters were selected from train/validation only. The test partition
was not used for model selection. Because each class currently has one
controlled session, this is a real-data controlled pilot and not an
independent-session production-generalization result.

## Software gate

Final command:

```powershell
pwsh -NoProfile -File .\scripts\run_tests.ps1 -WithFirmware `
  -FirmwareProfile Debug `
  -FirmwareRevision real-ai-integration-local `
  -FirmwareSerial pending
```

This command was rerun from the clean `feature/real-ai-integration-20260823`
worktree based directly on `main`; no timed-collection or low-power source was
present. Result: exit `0`; native 38 executables, Host 131 tests, AI 78 tests,
and firmware build/vector/memory-map/ICM alignment checks all passed.
`git diff --check` exited `0`.

Firmware artifacts:

| Artifact | Size | SHA-256 |
|---|---:|---|
| ELF | 1,201,136 B | `8EBFF70F5BEE335EE4D4DAC4B85BE5D8E5516EAC2C2E52850DFD1815E6165F1C` |
| BIN | 208,144 B | `1EE4B6240559710EAC81566CB33C8F0C00E109BC7124D8AAFC67EECFDC2289A6` |
| MAP | 985,797 B | `0E6C46752F3F50A0A53480AFB8DFC1EF2AF847E45A01CECE89540AA86D1DAC7E` |

## Board gate

The BIN was programmed and verified at `0x08020000` through ST-Link
`DEVICE_SERIAL_REDACTED__`. External QSPI was not formatted. A 534-byte
four-class TRMD package (SHA-256
`1DAF7B54E2F0F84940D82D5B06094D68C9D992EEF77C5910AAAAE82047FD39DD`)
was uploaded through TERP. MCU finalization accepted four golden records with
expected class indices 0, 1, 2, and 3. The active model changed from slot B
(`1`) to slot A (`0`).

The final AI-only BIN from the clean integration worktree was then programmed
and verified at the same application address. After reset, read-only TERP
verification reported `model_valid=true`, `pending_install=false`, active slot
A, storage/export error counts zero, and all 179 historical events still
present. The authoritative final-board record is
[`ai-only-post-flash-verify.json`](./ai-only-post-flash-verify.json)
(`pass=true`; SHA-256
`8492B981C9966760C229B34C70BD7C5E26608BCD30CD9C5BF897898E2FB4B133`); the
earlier model-install record is retained as
[`model-ota-post-reset-verify.json`](./model-ota-post-reset-verify.json)
(`pass=true`; SHA-256
`C98104CE2D110D366A2ACE271E10478C291B41E88ADDAD7FFBD3F24504DE359C`).
These machine-readable records are independent evidence from the isolated
`feature/real-ai-integration-20260823` worktree. Their repository file history
does not establish a stronger shared `main` acceptance provenance, so these
PASS results are not written as `main`'s common release state.

## Remaining limits

- Continuous-vibration test recall is weak (`0.3333`).
- Background false-positive rate is based on only five minutes and is unstable.
- A production gate needs at least three truly independent sessions per class
  and a fresh untouched test session.
- QSPI event-log free space is zero; do not format it as part of AI integration.
- OTA rollback and power-loss fault injection were not repeated for this pilot.
