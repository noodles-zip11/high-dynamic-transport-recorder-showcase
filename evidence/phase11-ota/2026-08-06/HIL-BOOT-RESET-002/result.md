case_id: HIL-BOOT-RESET-002
board: STM32H743VIT6 / ST-Link DEVICE_SERIAL_REDACTED__
bootloader_sha256: 68751EA9EE77A4209FB4F11730050C1235FAA43A4B7EC503326F91F133E7FA15
application_sha256: HIL one-cycle build
package_sha256: N/A
qspi_jedec: EF4017
power_action: software reset x10
expected: Bootloader provisions a blank state once, transfers to the application, and all ten software resets reach the protected QSPI HIL completion line.
observed: state primary starts with OTST record data; CPU PC was in the application region and raw log contains ten complete P11 QSPI HIL runs.
verdict: PASS
raw_log: uart.log
