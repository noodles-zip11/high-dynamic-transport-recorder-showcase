# Phase 11 OTA exit audit

Date: 2026-08-15
Board: STM32H743VIT6, ST-Link `DEVICE_SERIAL_REDACTED__`
Console: CH340 COM8, UART3 `PD8/PD9`, 115200 8N1
Scope rule: the OTA-only image does not start the SPI Event Flash service. The
only external-Flash mutations in this audit are the QSPI candidate/recovery
operations already listed in the HIL evidence.

## Requirement disposition

| Requirement | Disposition | Authoritative evidence | Boundary |
|---|---|---|---|
| H743 Flash/QSPI layout and JEDEC profile | PASS | `SW-OTA-AUDIT-001/`, `HIL-QSPI-ERASE-001/` | Layout and QSPI scratch-cycle checks do not prove power-loss behavior |
| Independent Bootloader build, size, image and jump handoff | PASS (software + HIL sub-gates) | `SW-BOOTLOADER-IRQ-GUARD-001/`, `HIL-BOOT-RESET-002/`, `HIL-BOOT-VECTOR-001/`, `HIL-FW-UPDOWN-001/final-readback.log` | Final baseline readback is not a physical power-cut test |
| Package format, deterministic packaging and rejection of invalid packages | PASS | `SW-OTA-AUDIT-001/`, `HIL-FW-BADPKG-004/` | Signature/authentication is outside this first CRC/SHA release |
| TERP download, per-block CRC, resume and idempotent replay | PASS | `HIL-FW-STAGE-001/`, `HIL-FW-RESUME-010`, `HIL-RECOVERY-001/` | Serial close/reopen is not a power interruption |
| A/B state records, copy progress, integrity and rollback decision logic | PASS (native + HIL sub-gates) | `HIL-BOOT-STATE-001/`, `HIL-FW-STAGE-001/`, `HIL-TRIAL-CONFIRM-001/` | Production health-based confirmation remains separate |
| Normal upgrade and downgrade | PASS | `HIL-FW-UPDOWN-001/` | 1.1.2 and 1.1.1 readback hashes matched; final baseline restored |
| Trial fault injection / failed-trial path | PARTIAL | `HIL-TRIAL-CONFIRM-001/` | Forced trial/fault evidence exists, but OTA-only image has no production health reporter, so automatic confirmation is not claimed |
| Installation power-loss matrix (30 checkpoints) | BLOCKED | `HIL-POWER-*` is explicitly marked blocked | Requires controlled physical 3.3 V removal and reapplication; NRST/SWD reset is not equivalent |
| Production Trial auto-confirm | BLOCKED | `HIL-TRIAL-CONFIRM-001/` | The current no-SPI OTA-only harness intentionally does not start the production reporter |
| Model A/B OTA, golden self-test and model rollback | BLOCKED | `HIL-MODEL-*` | Phase 10 runtime/manifest contract is not integrated |
| Standalone Bootloader rescue communication | BLOCKED | `HIL-BOOT-RESCUE-001` is not implemented | `RECOVERY` currently waits; existing recovery transfer is app-side TERP and requires a runnable application |

## Final board state

The board was left in the safe baseline recorded by
`HIL-FW-UPDOWN-001/final-readback.log`:

- fixed Bootloader at `0x08000000`, SHA-256 prefix `EF335254...`;
- OTA-only baseline application at `0x08020000`, SHA-256 prefix `BA21D149...`;
- both state records `CONFIRMED` (generations `100/101`);
- `VTOR=0x08020000`, USART3 `CR1=0x0000002D`;
- post-fix COM8/UART3 stability: 10 samples over 60.5 s, health errors `0`.

The first downgrade attempt exposed a real Bootloader HardFault while internal
Flash was being erased/programmed with SysTick enabled. The minimal IRQ guard
was then built, programmed, locally regressed, and the same downgrade was
replayed successfully. The failure and repair are retained rather than hidden
in `HIL-FW-UPDOWN-001/`.

## Exit conclusion

All executable checks available in the current OTA-only/no-SPI scope are
complete and evidenced. Phase 11 is not a full acceptance exit until the four
blocked rows above are closed with the production integration, direct rescue
transport, and controlled power equipment.
