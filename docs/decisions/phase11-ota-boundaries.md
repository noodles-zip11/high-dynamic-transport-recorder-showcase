# Phase 11 OTA integration boundary

> **HISTORICAL DECISION (2026-08-15):** This file records the firmware-OTA
> merge boundary at that date. Its statements that model OTA was unavailable
> were superseded by the later model A/B lifecycle and V1 board evidence.
> Current release status: [`evidence/releases/v1.0.0/`](../../evidence/releases/v1.0.0/README.md).
> The power-cut and standalone Bootloader-rescue limitations below remain
> historical requirements unless a newer acceptance record closes them.

Date: 2026-08-15
Branch: `feature/ota`

## Decision

This integration merges the Bootloader and firmware-OTA software slice into
`main`. It does **not** declare the complete Phase 11 acceptance or a
production-release approval.

The merged slice contains the locked H743/W25Q64 layout, package validation,
TERP staging, resumable Bootloader installation, trial/rollback state handling,
application handoff, and the associated native, host, firmware-build, and HIL
evidence.

## Evidence status at merge

Passed software and board sub-gates are indexed in
`evidence/phase11-ota/README.md` and the shared
`evidence/hardware-validation.md`, including the fresh pre-merge and
post-merge records:

- `SW-FULL-REGRESSION-003` and the post-merge rerun `SW-FULL-REGRESSION-004`;
- `SW-FW-BUILD-001` and the post-merge rerun `SW-FW-BUILD-002`;
- `SW-BOOTLOADER-REGRESSION-002`, including the writable-range
  unsigned-underflow regression cases;
- normal 1.1.2 upgrade and 1.1.1 downgrade;
- post-fix final baseline and COM8/UART3 stability.

The following remain explicit follow-up gates and are not hidden by this
merge:

1. the 30-point physical installation power-cut matrix;
2. production-image health-based Trial auto-confirmation;
3. Phase 10 model runtime/manifest integration and model A/B HIL validation.
4. A direct Bootloader rescue transport and its dedicated HIL evidence. The
   current recovery evidence uses the app-side TERP service while the
   application is runnable; a Bootloader that falls into `RECOVERY` currently
   waits and does not expose a standalone UART3 rescue protocol.

Until those gates pass, do not label Phase 11 fully accepted, do not claim
model OTA support, and do not use this merge as a production-release sign-off.
The direct rescue transport is also required before claiming recovery from a
non-bootable application.

## Scope boundary

The OTA-only HIL image intentionally does not start the SPI Event Flash
service. QSPI candidate/recovery operations are covered by the existing HIL
records; SPI Event Flash is outside this task and remains untouched.

Model A/B OTA should be implemented later in a new worktree from the updated
`main`, after the Phase 10 manifest/runtime contract is stable.
