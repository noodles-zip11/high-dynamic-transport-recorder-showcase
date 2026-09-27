# Phase 11 OTA board evidence index

> **HISTORICAL OTA-ONLY INDEX:** this index freezes the 2026-08-06 through
> 2026-08-15 harness state. Rows that mark model A/B as blocked predate its
> implementation and must not be used as current V1 status. Use
> [`../releases/v1.0.0/README.md`](../releases/v1.0.0/README.md) for the current
> release conclusion; keep the rows and raw files below unchanged as dated
> evidence.

This directory only contains evidence collected on the STM32H743VIT6 board
through ST-Link serial `DEVICE_SERIAL_REDACTED__` and the CH340 console
(currently COM8; earlier records retain the historical COM7/COM5 assignments). The
HIL image never starts the SPI event-log service; all external
Flash mutation is guarded to the QSPI scratch sector `0x0000F000..0x0000FFFF`.

| Case | Result | Raw evidence | Scope |
| --- | --- | --- | --- |
| HIL-QSPI-ERASE-001 | PASS | `2026-08-06/HIL-QSPI-ERASE-001/uart-100-cycles.log` | JEDEC, 40 MHz cap, 100 erase/program/read cycles, page crossing, write guards |
| HIL-BOOT-RESET-002 | PASS | `2026-08-06/HIL-BOOT-RESET-002/uart.log` | ten SWD software-reset Bootloader-to-application handoffs |
| HIL-BOOT-STATE-001 | PASS | `2026-08-06/HIL-BOOT-STATE-001/` | blank-plus-corrupt state records enter recovery; backed-up state restored byte-for-byte |
| HIL-BOOT-VECTOR-001 | PASS | `2026-08-06/HIL-BOOT-VECTOR-001/` | non-Thumb application reset vector enters recovery; application restored byte-for-byte |
| HIL-QSPI-COLD-002 | BLOCKED | N/A | requires a physical five-second power removal; NRST and debugger reset are not substitutes |
| HIL-FW-TERP-001 | PASS | `2026-08-12/HIL-OTA-TERP-001/com7-terp-hello-probe.log` | CH340 on COM7 is the exposed UART3/PD8-PD9 TERP link |
| HIL-FW-STAGE-001 | PASS (candidate install) | `2026-08-12/HIL-FW-STAGE-001/` | Candidate upload, full QSPI replay, manifest/CRC/SHA validation, atomic `PENDING_INSTALL`, resumable Bootloader copy, `TRIAL`, application SHA-256 and TERP reconnect |
| HIL-FW-RESUME-010 | PASS | `2026-08-12/HIL-FW-STAGE-001/terp-10-session-resume-replay-cancel.log` | Ten deliberate COM7 session disconnects at 0–90%, exact-progress resume, idempotent replay, conflicting replay rejection, then candidate cancellation |
| HIL-FW-BADPKG-004 | PASS | `2026-08-12/HIL-FW-STAGE-001/terp-four-bad-packages.log`, `terp-three-bad-packages.log` | Product ID, hardware ID, target address, and payload-integrity-invalid candidates were rejected without setting `PENDING_INSTALL`; each candidate slot was then erased |
| HIL-FW-TRIAL-STABILITY-001 | PASS | `2026-08-12/HIL-FW-STAGE-001/com7-trial-stability-60s.log` | Ten TERP reconnect/query samples over 60 seconds after installation; candidate slot remained empty |
| HIL-RECOVERY-001 | PASS (transfer and package validation) | `2026-08-12/HIL-RECOVERY-001/` | Recovery package transfer, per-block CRC and full package CRC/SHA passed without setting `PENDING_INSTALL` |
| HIL-RECOVERY-INSTALL-001 | PASS (install and final cold boot) | `2026-08-12/HIL-RECOVERY-INSTALL-001/` | Recovery image copied to internal application Flash; physical power-cycle evidence, application SHA-256 and final formal Bootloader `CONFIRMED` state recorded |
| HIL-TRIAL-CONFIRM-001 | BLOCKED (OTA-only harness) | `2026-08-12/HIL-TRIAL-CONFIRM-001/` | Forced valid `TRIAL` state reached Bootloader trial count 1/2, but the no-SPI OTA-only harness does not run the production health reporter, so it cannot legitimately self-confirm |
| HIL-UART3-ECHO-001 | VOID (adapter loopback only) | `2026-08-12/HIL-UART3-ECHO-001/com5-echo-four-bytes.log` | `00 41 A5 FF` was returned while CH340 TX/RX remained physically shorted, so this log proves the adapter only, not the STM32 UART3 path |
| HIL-POWER-* | BLOCKED | N/A | requires controlled 3.3 V power cutting at the 30 installation/trial checkpoints |
| HIL-MODEL-* | BLOCKED | N/A | Phase 10 runtime/manifest contract has not yet been integrated |
| HIL-BOOT-RESCUE-001 | BLOCKED | N/A | Standalone Bootloader `RECOVERY` currently waits; no direct UART3 rescue protocol is implemented. `HIL-RECOVERY-001` proves app-side TERP recovery only while an application is still runnable |
| SW-BOOTLOADER-REGRESSION-001 | PASS (software gate) | `2026-08-15/SW-BOOTLOADER-REGRESSION-001/test-bootloader.log` | Native Bootloader tests and image checks passed; not a board acceptance result |
| SW-BOOTLOADER-REGRESSION-002 | PASS (range-guard regression) | `2026-08-15/SW-BOOTLOADER-REGRESSION-002/test-bootloader-range-guard.log` | Rebuilt native/standalone Bootloader after the internal-Flash writable-range upper-bound fix; unsigned wraparound cases, image size and RT-Thread symbol checks passed; not a board acceptance result |
| SW-FULL-REGRESSION-001 | PASS (software gate) | `2026-08-15/SW-FULL-REGRESSION-001/run-tests.log` | Native C, Bootloader, host and Python regression passed; not a board acceptance result |
| SW-FULL-REGRESSION-002 | PASS (post-fix software gate) | `2026-08-15/SW-FULL-REGRESSION-002/run-tests-after-irq-guard.log` | After the internal-Flash IRQ guard, the complete local suite passed again: native C, Bootloader, host `100` tests and AI `1` test; not a board acceptance result |
| SW-FULL-REGRESSION-003 | PASS (pre-merge software gate) | `2026-08-15/SW-FULL-REGRESSION-003/run-tests-pre-merge.log` | Fresh pre-merge run: native C, Bootloader, host `100` tests and AI `1` test passed; not a board acceptance result |
| SW-FW-BUILD-001 | PASS (pre-merge firmware build) | `2026-08-15/SW-FW-BUILD-001/build-pre-merge.log` | Fresh default firmware ELF/BIN/MAP build, application link origin `0x08020000` and memory-map checks passed; no hardware write |
| SW-FULL-REGRESSION-004 | PASS (post-merge software gate) | `2026-08-15/SW-FULL-REGRESSION-004/run-tests-post-merge.log` | Fresh run after resolving the main/OTA merge: native C, Bootloader, host `118` tests and AI `1` test passed; not a board acceptance result |
| SW-FW-BUILD-002 | PASS (post-merge firmware build) | `2026-08-15/SW-FW-BUILD-002/build-post-merge.log` | Fresh default firmware ELF/BIN/MAP build after merge, application link origin `0x08020000` and memory-map checks passed; QSPI HAL/MDMA dependency is explicit; no hardware write |
| SW-MAIN-REGRESSION-001 | PASS (final main verification) | `2026-08-15/SW-MAIN-REGRESSION-001/run-tests-main.log` | Fresh full-suite run on merged `main`: native C, Bootloader/range-guard image checks, host `118` tests and AI `1` test passed; not a board acceptance result |
| SW-MAIN-FW-BUILD-001 | PASS (final main firmware build) | `2026-08-15/SW-MAIN-FW-BUILD-001/build-main.log` | Fresh merged-`main` firmware ELF/BIN/MAP build, application link origin `0x08020000`, QSPI HAL/MDMA and memory-map checks passed; no hardware write |
| SW-OTA-AUDIT-001 | PASS (software gate) | `2026-08-15/SW-OTA-AUDIT-001/software-ota-audit.log` | Host-only environment check, locked Flash/QSPI layout validation, 41 OTA package-tool tests, deterministic package generation, and package inspection passed; no board write or SPI Event Flash access |
| SW-BOOTLOADER-IRQ-GUARD-001 | PASS (software fix gate) | `2026-08-15/SW-BOOTLOADER-IRQ-GUARD-001/build-corrected.log`, `regression-corrected.log` | Minimal internal-Flash erase/program IRQ guard built and passed Bootloader image/native checks; the triggering HIL fault and post-fix replay are recorded under HIL-FW-UPDOWN-001 |
| HIL-BOOT-SNAPSHOT-002 | PASS (read-only snapshot) | `2026-08-15/HIL-BOOT-SNAPSHOT-002/` | ST-Link read-only state/vector snapshot; no reset/write and no SPI Event Flash access |
| HIL-FW-BASELINE-RESTORE-001 | PASS (baseline restore) | `2026-08-15/HIL-FW-BASELINE-RESTORE-001/` | Formal Bootloader and OTA-only app restored to internal Flash; final readback confirmed `VTOR=0x08020000`, USART3 enabled and state `CONFIRMED` |
| HIL-FW-UPDOWN-001 | PASS (upgrade + downgrade after fix) | `2026-08-15/HIL-FW-UPDOWN-001/` | 1.1.2 candidate upgrade and 1.1.1 downgrade completed through COM8/UART3; both application readbacks matched package SHA-256, `INSTALLING→TRIAL` and final `VTOR=0x08020000` were observed. A pre-fix downgrade HardFault is retained as defect evidence; the IRQ-guard Bootloader was then replayed successfully and the OTA-only baseline was restored. Only QSPI candidate and internal application/state pages were touched; no SPI Event Flash access |
| HIL-FW-TERP-002/003 | BLOCKED (host port access) | `2026-08-15/HIL-FW-TERP-002/`, `HIL-FW-TERP-003/` | COM7 enumerates as CH340 but both read-only `info/health` attempts returned Windows `PermissionError(13)`; no UART3 protocol result claimed |
| HIL-FW-TERP-005 | BLOCKED (physical UART path not confirmed) | `2026-08-15/HIL-FW-TERP-005/com8-info-health.log` | COM8 enumerates and opens, but both TERP requests time out; physical connection must be UART3 `PD8/PD9` with crossed TX/RX and common GND |
| HIL-FW-TERP-006 | PASS (UART3 baseline) | `2026-08-15/HIL-FW-TERP-006/com8-info-health-uart3-retry.log` | After PD8/PD9 crossed reconnection, COM8 `info/health` passed; device identity and zero health error counters recorded; OTA-only image intentionally does not exercise SPI Event Flash |
| HIL-FW-TERP-STABILITY-001 | PASS (UART3 stability sub-gate) | `2026-08-15/HIL-FW-TERP-STABILITY-001/com8-terp-stability-60s.log` | Ten read-only HELLO/health samples over 65 seconds on COM8; identity remained stable and health error counters stayed zero; no SPI Event Flash or event operations |
| HIL-FW-TERP-STABILITY-002 | PASS (post-fix final baseline) | `2026-08-15/HIL-FW-TERP-STABILITY-002/com8-terp-stability-post-irq-guard-60s.log` | Ten read-only HELLO/health samples over 60.5 seconds after the IRQ-guard Bootloader and baseline restore; identity stayed stable and all health error counters remained zero |
| PHASE11-EXIT-AUDIT-001 | PARTIAL (exit audit) | `2026-08-15/PHASE11-EXIT-AUDIT-001/exit-audit.md` | Requirement-by-requirement disposition; all current OTA-only/no-SPI checks are closed, while physical power matrix, production Trial confirmation and Phase 10 model A/B remain explicitly blocked |

The original internal-Flash backup is
`2026-08-06/HIL-QSPI-ERASE-001/internal-flash-before.bin` with SHA-256
`0C1CDADF1F93D29DC52C67F5DB0C9AFB409058E35A73968A3B0948CCF28C6302`.

## HIL-FW-STAGE-001 result (2026-08-12)

Board evidence was collected through ST-Link `DEVICE_SERIAL_REDACTED__` and
the CH340 TERP adapter on COM7. The application was built with the
`phase11_ota_only=1` harness, so it intentionally did not start the forbidden
SPI event-log service. Only the QSPI candidate range was changed.

- Package: `candidate-1.1.2-terp-stack-fix.trfw`, 147188 bytes, SHA-256
  `7D6D355DF9F8A08F79F407DEB0A2F78C29955E838D3DF669C1E2D66FF75909F2`.
- Candidate image: SHA-256
  `122494F1A17E6A6D07924ACCF7E599E01314BCECB3A661AAB814207C987B37FA`.
- `terp-finalize-after-stack-fix.log` records the successful sequence
  `BEGIN -> WRITE 147188/147188 -> FINALIZE pending=1 -> QUERY pending=1`.
- `state-after-finalize-pending.log` records the atomic alternate state page
  with generation 1 and `PENDING_INSTALL`.
- `bootloader-install-state-and-image.log` and
  `bootloader-complete-install-state-and-image.log` record resumable
  4 KiB-per-boot copy progress. `bootloader-trial-state-and-image.log` records
  the final `TRIAL` state and the application readback; the matching hashes are
  in `application-after-trial-install.sha256.txt`.
- `com7-after-trial-install.log` records a successful post-install TERP hello
  and health query.

The initial `FINALIZE` failure is retained as a diagnostic record. The
candidate package was first built with the wrong hardware identifier, then the
matching package exposed a `terp_rx` stack overflow during complete package
verification. The test-first stack guard and the minimal increase from 1024 to
2048 bytes are recorded in `build-terp-stack-fix*.log`; the subsequent board
run passed.

This is not the full Phase 11 acceptance exit. The explicit production trial
confirmation, controlled power-cut matrix, Phase 10 model runtime/A-B model
validation, and a standalone Bootloader rescue transport remain BLOCKED. The recovery package has passed its transfer,
validation, installation and final formal-Bootloader cold-boot gates. The
temporary forced-trial diagnostic was restored to `CONFIRMED` before the later
baseline restore.

The deliberate-session test used serial-port close/reopen only; it did not
reset the STM32. `OTA_BEGIN` and `OTA_CANCEL` each erase the 1 MiB candidate
slot and can legitimately exceed the normal 3-second host response timeout;
their completed result was established by reconnecting and querying the
device, not by treating the initial host timeout as a device failure.

## HIL-RECOVERY-001 UART and startup diagnostic (2026-08-12)

After rebinding CH340 driver `3.9.2024.9`, Windows assigned the adapter to
COM5 and it configured successfully at 115200 baud. The four-byte echo log is
retained, but is void as board evidence: the CH340 TX/RX adapter pins were
still physically shorted, so the received bytes were the host transmission.

Once that short was removed, TERP correctly timed out because the processor
was still executing at `VTOR=0x08000000` and USART3 was uninitialised. The
application vector at `0x08020000` and a valid normal OTA state were present,
so the Bootloader image was uploaded and compared with the historically
verified SHA-256. Board readback was `2F03E93A7EF13E47F7661C09E6E47E73CF26713DA64760DB45ADAF681BA317FD`,
not the verified `68751EA9EE77A4209FB4F11730050C1235FAA43A4B7EC503326F91F133E7FA15`:
an unrelated prior program had overwritten the Bootloader.

`restore-verified-bootloader.log` records restoration and programmer verify of
the source-controlled, historically verified Bootloader. After reset,
`post-bootloader-restore-vtor-usart.log` records `VTOR=0x08020000`; and
`com5-post-bootloader-restore-hello.log` records successful TERP HELLO and
health queries. This is the valid UART3 board-path gate for the remaining
recovery transfer.

The recovery transfer itself subsequently passed. The known-good package
`candidate-1.1.2-terp-stack-fix.trfw` (147188 bytes, SHA-256
`7D6D355DF9F8A08F79F407DEB0A2F78C29955E838D3DF669C1E2D66FF75909F2`) was
written to only the recovery slot `0x00110000..0x0020FFFF` with 256-byte TERP
chunks and per-chunk CRC. `com5-recovery-after-first-minute-query.log` records
the resumable `(147188, 73728, 0)` checkpoint; the two resume logs record
completion; and `com5-recovery-resume-finalize-query.log` records matching
`FINALIZE` and `QUERY` results `(147188, 147188, 0)`. The final zero proves it
did not set `PENDING_INSTALL`.

Historical pre-fix diagnostic: during an earlier Bootloader boot, recovery-slot
full validation entered a HardFault before application handoff. Evidence in
`HIL-TRIAL-CONFIRM-001/hardfault-*.log` shows the forced HardFault and the
interrupted PC. The Bootloader was changed to keep validation buffers out of
the startup stack and to place its stack at the top of AXI SRAM, away from the
low SRAM region used by CubeProgrammer's flash loader. Native Bootloader tests
and image checks pass. The later physical-power-cycle and final-production
cold-boot logs supersede this historical diagnostic and provide the recovery
install gate evidence.

## 2026-08-15 continuation

The board was found with an unrelated Bootloader/application image: the
initial MSP was `0x24001600`, the application reset vector was invalid, and
the core was outside the formal Bootloader image. The formal Bootloader and
`phase11_ota_only` application were restored to internal Flash; that
restoration step itself did not touch QSPI or SPI Event Flash. The subsequent
final readback recorded `VTOR=0x08020000`, USART3 `CR1=0x0000002D`, application
PC in the application region, and both state pages `CONFIRMED` (generations
`100/101`).

After the COM7 host-open issue was resolved by re-enumeration as COM8 and a
physical UART3 `PD8/PD9` crossed connection, the 1.1.2 upgrade and 1.1.1
downgrade were executed through TERP. The first downgrade retry exposed a real
Bootloader HardFault during internal-Flash installation; `CFSR=0x00008200`,
`BFAR=0x00010000`, and the stacked PC was the SysTick handler. The minimal
IRQ guard in `bootloader_flash_hal.c` was built, programmed, and the same
downgrade resumed to a verified `TRIAL` image. The board was then restored to
the OTA-only baseline and passed the post-fix 60.5-second TERP stability gate.
The complete local regression was rerun after the fix: native C, Bootloader,
host `100` tests, and AI `1` test all passed.
All upgrade/downgrade QSPI writes were confined to the OTA candidate slot;
the SPI Event Flash service was never started.
