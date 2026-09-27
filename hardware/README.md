# Hardware evidence

The selected controller is the OpenMV4-style board populated with an
STM32H743VIT6. Board-specific facts are recorded from the user-supplied board
schematic and confirmed MCU marking, while facts that still require electrical
inspection remain explicitly marked as pending.

- Board identity: [`docs/hardware/openmv4_h743_board_identity.md`](../docs/hardware/openmv4_h743_board_identity.md)
- Minimum startup pin map: [`docs/hardware/board_pinmap.md`](../docs/hardware/board_pinmap.md)

Do not add clock, pin, power, memory, or debugger assumptions that cannot be
traced to the physical board, the official schematic, or the STM32H743
reference manuals.

## Current V1 board-validation status (2026-08-25)

The V1 release has board evidence for startup, natural impact/drop admission,
fixed 2400-sample EV03 records, U2 event-log persistence, UART3/TERP download and
CRC, four-class inference, and the Cortex-M7 WFI state chain. The canonical
public summary is [`evidence/releases/v1.0.0/`](../evidence/releases/v1.0.0/README.md).

The measured boundary remains explicit: independent-session AI generalization,
real current/runtime figures, the 100-cycle power-cut matrix, the 72-hour run,
and the complete physical-disconnect matrix are not V1 PASS claims. USB CDC,
RTC/VBAT/LSE, and physical IWDG are outside the V1 core feature declaration.

## Phase 11 OTA-only snapshot (2026-08-12, historical)

> The table below is retained as dated evidence. In particular, its
> `HIL-MODEL-* BLOCKED` row predates the later model A/B OTA implementation and
> V1 board closure; it must not be presented as the latest project status.

Phase 11 validation used the STM32H743VIT6 board, ST-Link
`DEVICE_SERIAL_REDACTED__`, and CH340 on COM5. The validation images did not
start the SPI event-log service; no SPI event Flash was accessed.

| Scope | Status | Board evidence |
| --- | --- | --- |
| QSPI identity, guarded scratch-sector erase/program/read | PASS | `evidence/phase11-ota/2026-08-06/HIL-QSPI-ERASE-001/` records JEDEC `EF4017`, the 40 MHz cap, and 100 cycles in only `0x0000F000..0x0000FFFF`. |
| Bootloader state-page recovery and application-vector rejection | PASS | `evidence/phase11-ota/2026-08-06/HIL-BOOT-STATE-001/` and `HIL-BOOT-VECTOR-001/`. |
| Candidate firmware OTA, invalid-package rejection, and ten-session resume | PASS | `evidence/phase11-ota/2026-08-12/HIL-FW-STAGE-001/`. |
| Recovery-package upload and complete-package validation | PASS | `evidence/phase11-ota/2026-08-12/HIL-RECOVERY-001/`; 147188-byte package passed per-chunk CRC and device-side full CRC/SHA without setting `PENDING_INSTALL`. |
| Recovery-package installation to internal application Flash | PASS | `evidence/phase11-ota/2026-08-12/HIL-RECOVERY-INSTALL-001/`; cold boot copied the image, readback SHA-256 was `122494F1A17E6A6D07924ACCF7E599E01314BCECB3A661AAB814207C987B37FA`, and TERP reconnected. |
| Final production cold boot | PASS | `HIL-RECOVERY-INSTALL-001/final-production-cold-boot.log`: formal Bootloader initial MSP `0x24080000`, `VTOR=0x08020000`, USART3 enabled, state `CONFIRMED`, and COM5 TERP HELLO/health passed. |
| 30-point controlled power-cut installation matrix | NOT DONE | Requires all predefined checkpoints, a recorded 3.3 V power cut, and recovery outcome for each point. |
| Production health-based trial auto-confirmation | NOT DONE | The OTA-only board harness deliberately does not start the SPI event-log/production health reporter, so it cannot validate the automatic confirmation path. |
| Phase 10 model A/B OTA | BLOCKED (at this historical checkpoint) | Requires the Phase 10 runtime and model-manifest contract; later V1 evidence supersedes this status. |

The formal Bootloader keeps its 4 KiB-per-boot resumable copy budget. A
separate full-copy Bootloader was used only to make the recovery-install HIL
test practical under the debugger; it was replaced with the formal image
before the final cold-boot check.
