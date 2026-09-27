case_id: HIL-BOOT-STATE-001
board: STM32H743VIT6 / ST-Link DEVICE_SERIAL_REDACTED__
bootloader_sha256: 68751EA9EE77A4209FB4F11730050C1235FAA43A4B7EC503326F91F133E7FA15
application_sha256: HIL one-cycle build
package_sha256: N/A
qspi_jedec: not accessed after state corruption
power_action: software reset
expected: one erased and one non-erased invalid state record enters recovery and does not jump to the application.
observed: primary read FFFFFFFF, secondary read FFFFFF00; PC 08001C7E and VTOR 08000000. Restoring state-before.bin yielded identical SHA-256 and the next reset reached application VTOR 08020000.
verdict: PASS
raw_log: SWD console transcript in this run
