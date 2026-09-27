case_id: HIL-QSPI-ERASE-001
board: STM32H743VIT6 / ST-Link DEVICE_SERIAL_REDACTED__
bootloader_sha256: 68751EA9EE77A4209FB4F11730050C1235FAA43A4B7EC503326F91F133E7FA15
application_sha256: 59D79B49062AD9119517015517FFA9DC598F22D06977EFEF522462D655700CBA
package_sha256: N/A
qspi_jedec: EF4017
power_action: reset
expected: QSPI at no more than 40 MHz completes 100 scratch-only erase/program/read cycles and rejects candidate/model writes before WREN.
observed: 100 cycles completed with CRC32 29058C73 and 74ACC073; candidate 00010000 and model A 00210000 requests were rejected.
verdict: PASS
raw_log: uart-100-cycles.log
