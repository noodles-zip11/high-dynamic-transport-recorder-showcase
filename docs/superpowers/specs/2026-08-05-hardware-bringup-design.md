# Hardware bring-up acceptance design

## Purpose and rule

This work validates the connected board peripherals without conflating hardware
proof with software tests. For every peripheral, use an unmodified vendor
example first when one exists and matches the physical interface. Then validate
the project BSP and driver path separately. A vendor example that uses different
pins or a different module mode is recorded as not applicable, not counted as a
pass.

All board operations occur only in the `feature/hardware-bringup` worktree.
Existing uncommitted changes in that worktree and in other worktrees are not
modified or staged.

## Devices and evidence sequence

| Device | Vendor layer | Project layer | Pass evidence |
| --- | --- | --- | --- |
| U2 SPI NOR | Vendor `SPI_W25Q64` example, using its unmodified image. | SPI2 board adapter plus `nor_flash_w25q`. | JEDEC `EF 40 17`, erase/program/read, power-cycle read, and sector restore. |
| U3 QSPI NOR | Vendor `QSPI_W25Q64` image, never invoking its write action during identity checking. | Project U3 QSPI configuration and a bounded diagnostic path. | JEDEC `EF 40 17`, ready status, read/program/erase/power-cycle read, and sector restore. |
| GY-601N1 in raw mode | No bundled vendor example matches `PS` floating raw-SPI operation; its MCU I2C/UART examples are explicitly not applicable. | SPI1 ICM45686 driver. | `WHO_AM_I(0x72)=0xE9`, static samples, then FIFO/INT1/DMA continuity. |
| SHT40 | Adafruit example is an Arduino reference, not an H743 flash image. Its command sequence is the vendor baseline. | I2C1 SHT4x adapter and driver. | Exactly one responding address, serial number with CRC, measurement with two CRCs, continuous readings. |
| USB-TTL / USART3 | Use a board-vendor example only if it targets PD8/PD9. | TERP on USART3. | Bidirectional HELLO/info, then event transfer and resume behavior. |

## Pin and protocol contracts

- U2: SPI2 PB12 nCS, PB13 SCK, PB14 MISO, PB15 MOSI; W25Q64-compatible,
  8 MiB, 4 KiB erase sector, 256-byte page.
- U3: QSPI PB10 nCS, PB2 CLK, PD11/PD12/PE2/PD13 IO0..IO3; W25Q64-compatible,
  8 MiB, 24-bit address, `FlashSize=22`.
- GY-601N1: `PS` remains floating. Its raw SPI pins connect SCLK to PA5,
  SDIO to PA7, SDO to PA6, nCS to PB0, and INT1 to PB1. All logic uses 3.3 V.
- SHT40: PB8 SCL and PB9 SDA, 3.3 V, camera FPC disconnected. The test probes
  0x44 and 0x45 and requires exactly one acknowledgement.
- USB-TTL: PD8 TX to adapter RX, PD9 RX to adapter TX, common ground, 3.3 V
  logic, 115200 8N1. UART1 FinSH remains separate.

## Flash data-safety procedure

No storage formatting, chip erase, or event-log operation is allowed in a
hardware probe. Before a destructive U2 or U3 test, the probe reads the chosen
4 KiB sector and exports the snapshot through SWD. The probe proceeds only when
every snapshot byte is `0xFF`; a non-erased sector stops the test without a
write, because a physical power-cycle cannot safely retain its 4 KiB backup in
MCU RAM. It then performs: sector erase, an address-sensitive pattern spanning
page boundaries, read-back comparison, physical power-cycle read-back, erase,
and a final all-`0xFF` comparison. A failed snapshot/export, erase, program,
or comparison stops the procedure and preserves evidence; it must not advance
to another peripheral.

U2 uses `0x007FF000..0x007FFFFF`. U3 requires an explicit sector selection at
execution time because its full capacity is reserved for future OTA/model slots;
the selected sector is always backed up and restored.

## Implementation and verification order

1. Add native tests that define the expected acceptance-result state and strict
   JEDEC identities. Run them red before implementation and green after.
2. Add a board-only probe with a volatile result structure readable through
   ST-Link. It calls the project adapters but is isolated from normal recording.
3. Build the H743 firmware and run the narrow native tests, then the applicable
   local suite and `git diff --check`.
4. For each device, back up current internal Flash, run the vendor layer where
   applicable, capture evidence, restore the original internal image, and
   verify its hash.
5. Run the project probe gates in the order shown in the table. Record command
   results, identities, CRCs, flash hashes, firmware hash, and board connection
   state under `evidence/hardware-bringup/`.
6. Run the normal TERP runtime only after U2 has passed its project test. Board
   acceptance remains partial until the 30-minute sensor checks and UART
   reconnect exercise complete.

## Failure handling

- Vendor layer fails, project layer not run: treat as hardware/connection issue
  until wiring, power, mode pins, and the vendor example are resolved.
- Vendor layer passes but project layer fails: keep the hardware result and
  debug only the project BSP/driver path.
- Project flash operation fails: stop immediately after preserving the raw
  result and snapshot; never retry by formatting the chip.
- Sensor identity passes but data, FIFO, interrupt, DMA, or long-run checks
  fail: report the successful lower-layer gate and the failing gate separately.

## Non-goals

This does not enable OTA, write production model data to U3, format U2, claim
power-cut recovery acceptance, or convert automated builds into hardware proof.
