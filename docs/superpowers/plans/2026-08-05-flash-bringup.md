# Flash Bring-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce reproducible vendor-first and project-path acceptance evidence for the board U2 SPI NOR and U3 QSPI NOR without formatting either device.

**Architecture:** Add a small, stateful flash-validation core that accepts the existing `nor_flash_t` interface and only writes an initially erased 4 KiB sector. The U2 board adapter and a new U3 QSPI single-line NOR adapter each supply a `nor_flash_t`, while an RT-Thread diagnostic exports volatile results through SWD between the write and power-cycle phases.

**Tech Stack:** C11, RT-Thread, STM32H743 HAL SPI/QSPI, SCons, native C tests, ST-Link and STM32CubeProgrammer.

---

### Task 1: Make the U2 JEDEC identity strict

**Files:**
- Modify: `firmware/tests/native/test_nor_flash_w25q.c`
- Modify: `firmware/components/storage/nor_flash_w25q.c`

- [ ] **Step 1: Add failing rejection assertions**

```c
mock.jedec_id[0] = 0x20U;
mock.jedec_id[1] = 0x40U;
mock.jedec_id[2] = 0x17U;
assert(nor_flash_w25q_probe(&flash, jedec_id) != RT_EOK);

mock.jedec_id[0] = 0xEFU;
mock.jedec_id[1] = 0x40U;
mock.jedec_id[2] = 0x18U;
assert(nor_flash_w25q_probe(&flash, jedec_id) != RT_EOK);
```

- [ ] **Step 2: Run the test red**

Run: `pwsh scripts/test_native.ps1`; then `firmware/tests/native/build/test_nor_flash_w25q.exe`

Expected: failure because the current probe accepts any manufacturer/type with capacity code `0x17`.

- [ ] **Step 3: Check all three expected ID bytes**

```c
if (flash == RT_NULL || jedec_id == RT_NULL
    || transfer(flash, tx, rx, sizeof(tx)) != RT_EOK
    || rx[1] != 0xEFU || rx[2] != 0x40U || rx[3] != 0x17U)
{
    return -RT_ERROR;
}
```

- [ ] **Step 4: Run the test green**

Run: `pwsh scripts/test_native.ps1`; then `firmware/tests/native/build/test_nor_flash_w25q.exe`

Expected: PASS.

### Task 2: Add the generic safe-sector validation core

**Files:**
- Create: `firmware/app/diagnostics/flash_validation.h`
- Create: `firmware/app/diagnostics/flash_validation.c`
- Create: `firmware/tests/native/test_flash_validation.c`
- Modify: `firmware/tests/native/SConstruct`
- Modify: `scripts/test_native.ps1`

- [ ] **Step 1: Write failing tests for three sector states**

```c
assert(flash_validation_run(&flash, TEST_OFFSET, &result) == RT_EOK);
assert(result.stage == FLASH_VALIDATION_AWAITING_POWER_CYCLE);
assert(result.initial_sector_erased == RT_TRUE);

assert(flash_validation_run(&flash, TEST_OFFSET, &result) == RT_EOK);
assert(result.stage == FLASH_VALIDATION_COMPLETE);
assert(result.power_cycle_pattern_verified == RT_TRUE);

fake_nor_fill(&fake, TEST_OFFSET, 0x00U, 1U);
assert(flash_validation_run(&flash, TEST_OFFSET, &result) != RT_EOK);
assert(result.stage == FLASH_VALIDATION_NON_ERASED_REFUSED);
```

- [ ] **Step 2: Run the new test red**

Run: `pwsh scripts/test_native.ps1`; then `firmware/tests/native/build/test_flash_validation.exe`

Expected: target missing or assertions fail before implementation.

- [ ] **Step 3: Implement the fixed 4 KiB two-boot state machine**

```c
typedef enum {
    FLASH_VALIDATION_FAILED = 0,
    FLASH_VALIDATION_AWAITING_POWER_CYCLE,
    FLASH_VALIDATION_COMPLETE,
    FLASH_VALIDATION_NON_ERASED_REFUSED,
} flash_validation_stage_t;

rt_err_t flash_validation_run(nor_flash_t *flash, uint32_t sector_offset,
                              flash_validation_result_t *result);
```

The first invocation accepts only an all-`0xFF` sector, writes a deterministic
page-spanning 4 KiB pattern, and reports `AWAITING_POWER_CYCLE`. A boot after
physical power removal detects that exact pattern, sets the power-cycle flag,
erases the sector, verifies it is all `0xFF`, and reports `COMPLETE`. Any other
initial contents return `NON_ERASED_REFUSED` without erase or program.

- [ ] **Step 4: Run the new test green and existing NOR test**

Run: `pwsh scripts/test_native.ps1`; then run `test_flash_validation.exe` and
`test_nor_flash_w25q.exe` from `firmware/tests/native/build/`.

Expected: both PASS.

### Task 3: Add a U3 QSPI W25Q64 adapter

**Files:**
- Create: `firmware/bsp/openmv4_h743/u3_qspi_nor.h`
- Create: `firmware/bsp/openmv4_h743/u3_qspi_nor.c`
- Create: `firmware/tests/native/test_u3_qspi_nor_contract.c`
- Modify: `firmware/bsp/openmv4_h743/board_pinmap.h`
- Modify: `firmware/bsp/openmv4_h743/stm32h7xx_hal_conf.h`
- Modify: `firmware/SConstruct`
- Modify: `firmware/tests/native/SConstruct`

- [ ] **Step 1: Add a failing pin-and-configuration contract test**

```python
assert "BOARD_U3_QSPI_NCS_GPIO_PIN GPIO_PIN_10" in pinmap
assert "BOARD_U3_QSPI_CLK_GPIO_PIN GPIO_PIN_2" in pinmap
assert "BOARD_U3_QSPI_FLASH_SIZE 22U" in qspi_source
assert "QSPI_CLOCK_MODE_3" in qspi_source
```

- [ ] **Step 2: Run the contract test red**

Run: `python -m pytest host/tests/test_openmv4_h743_board_contract.py -q`

Expected: failure until the U3 QSPI adapter is present and explicitly defines
the W25Q64 geometry.

- [ ] **Step 3: Implement single-line QSPI command/read/program/erase**

The adapter owns one `QSPI_HandleTypeDef`; it configures U3 only with PB10,
PB2, PD11, PD12, PE2 and PD13, `FlashSize=22`, mode 3 and single-line commands
`0x9F`, `0x05`, `0x03`, `0x06`, `0x02` and `0x20`. It returns a
`nor_flash_t` and rejects any JEDEC ID other than `EF 40 17`.

- [ ] **Step 4: Run the contract test green and build U3 HAL support**

Run: `python -m pytest host/tests/test_openmv4_h743_board_contract.py -q`

Expected: PASS.

Run: `.venv\\Scripts\\python.exe -m SCons -Q -C firmware -j4`

Expected: H743 ELF builds with `stm32h7xx_hal_qspi.c` linked.

### Task 4: Wire the board-only flash diagnostic and capture evidence

**Files:**
- Create: `firmware/app/diagnostics/board_flash_diagnostic.h`
- Create: `firmware/app/diagnostics/board_flash_diagnostic.c`
- Modify: `firmware/app/runtime/app_runtime.c`
- Modify: `firmware/SConstruct`
- Modify: `firmware/tests/native/SConstruct`
- Modify: `scripts/test_native.ps1`
- Create: `evidence/hardware-bringup/2026-08-05-flash-validation.md`

- [ ] **Step 1: Add a failing result-layout test**

```c
assert(offsetof(board_flash_diagnostic_result_t, u2) == 0U);
assert(sizeof(board_flash_diagnostic_result_t) <= 256U);
```

- [ ] **Step 2: Run the layout test red**

Run: `pwsh scripts/test_native.ps1 -Test test_board_flash_diagnostic`

Expected: target missing before the diagnostic result type exists.

- [ ] **Step 3: Implement the diagnostic entry point**

```c
void board_flash_diagnostic_run(void);
extern volatile board_flash_diagnostic_result_t board_flash_diagnostic_result;
```

The entry point initializes U2 and U3 independently, records each strict JEDEC
ID and status, and invokes `flash_validation_run` only for the configured
sector. The result structure lives in DMA-safe D2 SRAM and is read by ST-Link;
it never calls event-log format, event capture, OTA, or normal TERP commands.

- [ ] **Step 4: Run native tests, full software gate, and H743 build**

Run: `pwsh scripts/run_tests.ps1`

Expected: native, host, and AI tests PASS.

Run: `.venv\\Scripts\\python.exe -m SCons -Q -C firmware -j4`

Expected: H743 ELF PASS.

Run: `git diff --check`

Expected: no output.

- [ ] **Step 5: Execute vendor-first and project board gates**

1. Back up the current MCU internal Flash and record its SHA-256.
2. Flash the unmodified vendor `SPI_W25Q64` and `QSPI_W25Q64` images one at a
   time, read their identity evidence without selecting a write action, and
   restore the backup after each.
3. Flash the project diagnostic, read `board_flash_diagnostic_result` through
   SWD, and require both U2 and U3 sectors to be erased before writes occur.
4. When both report `AWAITING_POWER_CYCLE`, physically remove all board power,
   reapply it, and read the second boot's result. Require `COMPLETE` plus final
   all-`0xFF` sector states before recording PASS.
5. Restore normal firmware, verify its SHA-256, and save raw logs, screenshots,
   IDs, and failure data in the evidence file.

No commit, merge, or staging is part of this plan because none is authorized.
