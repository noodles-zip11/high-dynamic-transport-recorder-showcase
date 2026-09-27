case_id: HIL-BOOT-VECTOR-001
board: STM32H743VIT6 / ST-Link DEVICE_SERIAL_REDACTED__
bootloader_sha256: 68751EA9EE77A4209FB4F11730050C1235FAA43A4B7EC503326F91F133E7FA15
application_sha256: 7C698943933506B019520BE5196BECCEF51FDC76CA7555AE38433377CD32DFB6
package_sha256: N/A
qspi_jedec: not accessed after invalid vector rejection
power_action: software reset
expected: an application Reset_Handler without the Thumb bit must not be jumped to.
observed: test image reset vector Thumb bit was cleared; PC was 08001C7E and VTOR 08000000. Restoring application-good.bin produced a matching SHA-256 and next reset reached PC 0803265C with VTOR 08020000.
verdict: PASS
raw_log: SWD console transcript in this run
