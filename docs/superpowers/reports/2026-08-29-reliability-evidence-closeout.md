# Reliability Evidence final closeout

Date: 2026-08-29

## Disposition

The reliability-evidence implementation and its software safeguards are complete on
`feature/reliability-evidence`. It is not approved as a reliability-enabled Release
candidate because the frozen Phase 4 policy requires H0-H5 to be physical-board
PASS on one sealed source revision. The enabled Release path therefore remains
locked. The released V1 remains the product and board baseline.

This is an evidence limitation, not a claim that the implemented software failed.
Native, host, AI, bootloader, firmware build, default-off Release, and V1
compatibility checks pass. The unavailable instrumented and destructive physical
sub-gates are retained as explicit open risks rather than being inferred from
software tests.

## Final gate status

| Gate | Final status | Evidence-backed conclusion |
|---|---|---|
| Software S0-S4 | Current code PASS; current package not sealed | The complete local release gate passes with reliability disabled. The 11-gate evidence package is historical and bound to revision `0603436`, not the final branch HEAD. |
| H0 | Historical candidate PASS; current HEAD not rebound | Candidate identity, backup/program/verify/readback, and verified-V1 restore evidence are present for earlier revisions. They are not promoted to a final-HEAD RC gate. |
| H1 | PARTIAL / not formal PASS | Processor-fault and CrashRecord functional cases pass. Instrumented reset, reconnect, and watchdog timing are unavailable. |
| H2 | PARTIAL / not formal PASS | Queue-pressure behavior passes. Real IMU/FIFO/DMA loss and instrumented timing are unavailable. |
| H3 | NOT_EXECUTED | Destructive U2 media/power-loss coverage was not completed. |
| H4 | PARTIAL / not formal PASS | Immutable V1 client compatibility, an operator sequence represented by 11 PASS reconnect records plus a separate 9/9 PASS completion batch, and a 10/10 cold-boot matrix are useful supplemental evidence. They are not bound to the final HEAD, and the full legal DEGRADED/INVALID display matrix is not complete. |
| H5 | NOT_EXECUTED | QSPI/OTA/model physical integrity and timing coverage was not completed. |
| Reliability-enabled Release | LOCKED / REJECTED | H0-H5 are not all physical-board PASS. |
| Released V1 baseline | RESTORED / PASS | V1 was programmed once, exactly read back, resumed without another Flash write, and passed runtime acceptance. |

## Evidence anchors

- Phase 4 package: `C:/transport-recorder-evidence/2026-08-28/reliability-evidence-rc-013`
- Physical reconnect evidence: `reconnect/rc013-physical-matrix-20260828/matrix-final/round-01.json` through `round-11.json`, followed chronologically by `matrix-final-rounds-12-20/results.json` (a separate 9/9 PASS batch). The second collector uses local round numbers 1-9; the closeout index records the operator-intended 12-20 mapping but does not treat that mapping as a formal same-revision release gate.
- 10-round cold-boot result: `C:/transport-recorder-evidence/2026-08-29/reliability-evidence-rc-013/cold-boot-matrix-final-resume-04/results.json`.
- Cold-boot provenance: `C:/transport-recorder-evidence/2026-08-29/reliability-evidence-rc-013/cold-boot-matrix-final-resume-04/resume-provenance.json`; failed source round 5 is explicitly excluded and preserved.
- Original V1 restore result: `C:/transport-recorder-evidence/2026-08-29/reliability-evidence-rc-013/restore-v1-final/restore-v1.json`; its runtime failure is preserved because the exact NORMAL readback left the MCU halted.
- V1 exact readback: `C:/transport-recorder-evidence/2026-08-29/reliability-evidence-rc-013/restore-v1-final/v1-readback.bin`, 219,120 bytes, SHA-256 `83584FD8B3BBA44619D315D75E3140AEBC706BA8B406F3E5E04CDFD1A17F9112`.
- V1 no-write recovery verdict: `C:/transport-recorder-evidence/2026-08-29/reliability-evidence-rc-013/restore-v1-final/runtime-recovery-01/recovery-verdict.json`.
- V1 runtime acceptance: `C:/transport-recorder-evidence/2026-08-29/reliability-evidence-rc-013/restore-v1-final/runtime-recovery-01/acceptance/runtime-acceptance-recovery-01.json`.
- Final cross-root hashes and this disposition are sealed under `C:/transport-recorder-evidence/2026-08-29/reliability-evidence-rc-013/closeout/`.

All failed attempts and diagnostic-only records remain in place. They are useful
provenance and are not counted as formal physical passes. The 2026-08-28 package
and 2026-08-29 add-on evidence bind earlier implementation revisions; neither is
silently rebound to the final branch HEAD.

## Release and rollback rule

Do not build, flash, or label a reliability-enabled Release until H0-H5 all carry
physical-board PASS evidence for one sealed source revision under the frozen
manifest policy. The default-off
Release build may be used only as a non-regression guard. If a future physical gate
finds a regression, keep the feature disabled and retain released V1 unchanged.

## Current-main default-off gate snapshot

This snapshot records the local, default-off Release gate at exact revision `a454b4d4fa80ef23333212cc9a8201f10f663cdb`.
It is not a sealed V1 artifact and is not a Reliability-enabled Release.
These figures belong to the current-main gate snapshot only; published V1 artifact
figures and unchanged V1 firmware/protocol/model bytes remain traceable in the
[V1 evidence package](../../../evidence/releases/v1.0.0/README.md) and [V1 release notes](../../v1.0.0-release-notes.md).
This local snapshot is unsealed, so it does not publish artifact hashes and must not be compared with or rebound to the older sealed candidate manifest.

| Check | Result |
|---|---|
| scripts | `135 passed` |
| Host | `178 passed`, `1 skipped` |
| AI | `78 passed` |
| native C / Bootloader / ARM Release | `PASS` |
| Release image fault-injection entry-point isolation (FaultInjection absence) | `PASS` |
| Release ROM | `136,528 B / 1,664 KiB (8.01%)` |
| primary RAM | `271,396 B / 512 KiB (51.76%)` |
| D2 SRAM1 | `2,144 B / 128 KiB (1.64%)` |
