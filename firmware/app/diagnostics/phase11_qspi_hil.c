#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <rtthread.h>

#include "project_config.h"
#include "memory_layout.h"
#include "ota_qspi_candidate.h"
#include "phase11_qspi_hil.h"
#include "stm32h7xx_hal.h"

#define P11_QSPI_EXPECTED_JEDEC UINT32_C(0xEF4017)
#define P11_QSPI_SOURCE_HZ TRANSPORT_HCLK_HZ
#define P11_QSPI_MAX_HZ UINT32_C(40000000)
#define P11_QSPI_PRESCALER ((P11_QSPI_SOURCE_HZ / P11_QSPI_MAX_HZ) - 1U)
#define P11_QSPI_HZ (P11_QSPI_SOURCE_HZ / (P11_QSPI_PRESCALER + 1U))
#define P11_QSPI_FLASH_SIZE 22U
#define P11_QSPI_PAGE_BYTES UINT32_C(256)
#define P11_QSPI_ERASE_BYTES TRANSPORT_OTA_QSPI_HIL_SCRATCH_SIZE_BYTES
#define P11_QSPI_SCRATCH_BASE TRANSPORT_OTA_QSPI_HIL_SCRATCH_OFFSET
#define P11_QSPI_SCRATCH_END (P11_QSPI_SCRATCH_BASE + P11_QSPI_ERASE_BYTES)
#define P11_QSPI_TIMEOUT_MS UINT32_C(1000)

static QSPI_HandleTypeDef phase11_qspi;
static bool phase11_qspi_initialized;
static uint8_t phase11_qspi_erased[P11_QSPI_ERASE_BYTES];
static uint8_t phase11_qspi_incrementing[P11_QSPI_PAGE_BYTES];
static uint8_t phase11_qspi_crossing[32];
static uint8_t phase11_qspi_randomish[P11_QSPI_PAGE_BYTES];
static uint8_t phase11_qspi_verify[P11_QSPI_PAGE_BYTES];

static bool phase11_qspi_parse_cycle_count(const char *text, uint32_t *count)
{
    uint32_t value = 0U;

    if (text == NULL || count == NULL || *text == '\0')
    {
        return false;
    }
    while (*text != '\0')
    {
        if (*text < '0' || *text > '9' || value > 100U)
        {
            return false;
        }
        value = value * 10U + (uint32_t)(*text - '0');
        text++;
    }
    if (value == 0U || value > 100U)
    {
        return false;
    }
    *count = value;
    return true;
}

static uint32_t phase11_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    uint32_t index;

    for (index = 0U; index < length; index++)
    {
        uint32_t bit;

        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? UINT32_C(0xEDB88320) : 0U);
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

static void phase11_qspi_command_defaults(QSPI_CommandTypeDef *command)
{
    command->InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command->AddressSize = QSPI_ADDRESS_24_BITS;
    command->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command->DummyCycles = 0U;
    command->DdrMode = QSPI_DDR_MODE_DISABLE;
    command->DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command->SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
}

static bool phase11_qspi_scratch_range_is_valid(uint32_t address, uint32_t length)
{
    return length <= P11_QSPI_SCRATCH_END - P11_QSPI_SCRATCH_BASE
           && address >= P11_QSPI_SCRATCH_BASE
           && address <= P11_QSPI_SCRATCH_END - length;
}

static bool phase11_qspi_read_jedec(uint32_t *jedec)
{
    QSPI_CommandTypeDef command = {0};
    uint8_t bytes[3];

    phase11_qspi_command_defaults(&command);
    command.Instruction = 0x9FU;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = sizeof(bytes);
    if (HAL_QSPI_Command(&phase11_qspi, &command, P11_QSPI_TIMEOUT_MS) != HAL_OK
        || HAL_QSPI_Receive(&phase11_qspi, bytes, P11_QSPI_TIMEOUT_MS) != HAL_OK)
    {
        return false;
    }
    *jedec = ((uint32_t)bytes[0] << 16U) | ((uint32_t)bytes[1] << 8U) | bytes[2];
    return true;
}

static bool phase11_qspi_wait_ready(void)
{
    QSPI_CommandTypeDef command = {0};
    QSPI_AutoPollingTypeDef poll = {0};

    phase11_qspi_command_defaults(&command);
    command.Instruction = 0x05U;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = 1U;
    poll.Match = 0U;
    poll.Mask = 0x01U;
    poll.MatchMode = QSPI_MATCH_MODE_AND;
    poll.StatusBytesSize = 1U;
    poll.Interval = 0x10U;
    poll.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;
    return HAL_QSPI_AutoPolling(&phase11_qspi, &command, &poll, P11_QSPI_TIMEOUT_MS)
           == HAL_OK;
}

static bool phase11_qspi_write_enable(void)
{
    QSPI_CommandTypeDef command = {0};

    phase11_qspi_command_defaults(&command);
    command.Instruction = 0x06U;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.DataMode = QSPI_DATA_NONE;
    return HAL_QSPI_Command(&phase11_qspi, &command, P11_QSPI_TIMEOUT_MS) == HAL_OK;
}

static bool phase11_qspi_read(uint32_t address, uint8_t *data, uint32_t length)
{
    QSPI_CommandTypeDef command = {0};

    if (!phase11_qspi_scratch_range_is_valid(address, length))
    {
        rt_kprintf("P11,QSPI,GUARD,REJECT,%08lx,%lu,0\n", (unsigned long)address,
                   (unsigned long)length);
        return false;
    }
    phase11_qspi_command_defaults(&command);
    command.Instruction = 0x03U;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.Address = address;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = length;
    return HAL_QSPI_Command(&phase11_qspi, &command, P11_QSPI_TIMEOUT_MS) == HAL_OK
           && HAL_QSPI_Receive(&phase11_qspi, data, P11_QSPI_TIMEOUT_MS) == HAL_OK;
}

static bool phase11_qspi_erase(uint32_t address)
{
    QSPI_CommandTypeDef command = {0};
    uint32_t jedec;

    if (!phase11_qspi_scratch_range_is_valid(address, P11_QSPI_ERASE_BYTES)
        || address % P11_QSPI_ERASE_BYTES != 0U)
    {
        rt_kprintf("P11,QSPI,GUARD,REJECT,%08lx,%lu,0\n", (unsigned long)address,
                   (unsigned long)P11_QSPI_ERASE_BYTES);
        return false;
    }
    if (!phase11_qspi_read_jedec(&jedec) || jedec != P11_QSPI_EXPECTED_JEDEC
        || !phase11_qspi_write_enable())
    {
        return false;
    }
    rt_kprintf("P11,QSPI,ERASE,READY,%08lx,%lu,0\n", (unsigned long)address,
               (unsigned long)P11_QSPI_ERASE_BYTES);
    phase11_qspi_command_defaults(&command);
    command.Instruction = 0x20U;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.Address = address;
    command.DataMode = QSPI_DATA_NONE;
    return HAL_QSPI_Command(&phase11_qspi, &command, P11_QSPI_TIMEOUT_MS) == HAL_OK
           && phase11_qspi_wait_ready();
}

static bool phase11_qspi_program(uint32_t address, const uint8_t *data, uint32_t length)
{
    QSPI_CommandTypeDef command = {0};
    uint32_t jedec;

    if (data == NULL || length == 0U || length > P11_QSPI_PAGE_BYTES
        || !phase11_qspi_scratch_range_is_valid(address, length)
        || address / P11_QSPI_PAGE_BYTES
               != (address + length - 1U) / P11_QSPI_PAGE_BYTES)
    {
        rt_kprintf("P11,QSPI,GUARD,REJECT,%08lx,%lu,0\n", (unsigned long)address,
                   (unsigned long)length);
        return false;
    }
    if (!phase11_qspi_read_jedec(&jedec) || jedec != P11_QSPI_EXPECTED_JEDEC
        || !phase11_qspi_write_enable())
    {
        return false;
    }
    phase11_qspi_command_defaults(&command);
    command.Instruction = 0x02U;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.Address = address;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = length;
    return HAL_QSPI_Command(&phase11_qspi, &command, P11_QSPI_TIMEOUT_MS) == HAL_OK
           && HAL_QSPI_Transmit(&phase11_qspi, (uint8_t *)data, P11_QSPI_TIMEOUT_MS)
                  == HAL_OK
           && phase11_qspi_wait_ready();
}

static bool phase11_qspi_init(void)
{
    RCC_PeriphCLKInitTypeDef clock = {0};
    uint32_t jedec;

    if (phase11_qspi_initialized)
    {
        return true;
    }
    clock.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
    clock.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
    {
        return false;
    }
    phase11_qspi.Instance = QUADSPI;
    phase11_qspi.Init.ClockPrescaler = P11_QSPI_PRESCALER;
    phase11_qspi.Init.FifoThreshold = 1U;
    phase11_qspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    phase11_qspi.Init.FlashSize = P11_QSPI_FLASH_SIZE;
    phase11_qspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
    phase11_qspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
    phase11_qspi.Init.FlashID = QSPI_FLASH_ID_1;
    phase11_qspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
    if (HAL_QSPI_Init(&phase11_qspi) != HAL_OK || !phase11_qspi_read_jedec(&jedec)
        || jedec != P11_QSPI_EXPECTED_JEDEC)
    {
        return false;
    }
    phase11_qspi_initialized = true;
    rt_kprintf("P11,BOOT,JEDEC,OK,%06lx,%lu,%s\n", (unsigned long)jedec,
               (unsigned long)P11_QSPI_HZ, __DATE__ " " __TIME__);
    return true;
}

static bool phase11_qspi_run_cycle(uint32_t cycle)
{
    uint32_t index;

    for (index = 0U; index < P11_QSPI_PAGE_BYTES; index++)
    {
        phase11_qspi_incrementing[index] = (uint8_t)index;
        phase11_qspi_randomish[index] = (uint8_t)((index * 73U + 41U) & 0xFFU);
    }
    for (index = 0U; index < sizeof(phase11_qspi_crossing); index++)
    {
        phase11_qspi_crossing[index] = (index & 1U) == 0U ? 0xA5U : 0x5AU;
    }
    if (!phase11_qspi_erase(P11_QSPI_SCRATCH_BASE)
        || !phase11_qspi_read(P11_QSPI_SCRATCH_BASE, phase11_qspi_erased,
                              sizeof(phase11_qspi_erased)))
    {
        return false;
    }
    for (index = 0U; index < sizeof(phase11_qspi_erased); index++)
    {
        if (phase11_qspi_erased[index] != 0xFFU)
        {
            return false;
        }
    }
    if (!phase11_qspi_program(P11_QSPI_SCRATCH_BASE + 0x200U, phase11_qspi_incrementing,
                              sizeof(phase11_qspi_incrementing))
        || !phase11_qspi_program(P11_QSPI_SCRATCH_BASE + 0xF0U, phase11_qspi_crossing, 16U)
        || !phase11_qspi_program(P11_QSPI_SCRATCH_BASE + 0x100U,
                                 phase11_qspi_crossing + 16U, 16U)
        || !phase11_qspi_program(P11_QSPI_SCRATCH_BASE + 0xF00U, phase11_qspi_randomish,
                                 sizeof(phase11_qspi_randomish))
        || !phase11_qspi_read(P11_QSPI_SCRATCH_BASE + 0x200U, phase11_qspi_verify,
                              sizeof(phase11_qspi_verify))
        || memcmp(phase11_qspi_verify, phase11_qspi_incrementing,
                  sizeof(phase11_qspi_verify)) != 0
        || !phase11_qspi_read(P11_QSPI_SCRATCH_BASE + 0xF0U, phase11_qspi_verify, 16U)
        || memcmp(phase11_qspi_verify, phase11_qspi_crossing, 16U) != 0
        || !phase11_qspi_read(P11_QSPI_SCRATCH_BASE + 0x100U, phase11_qspi_verify, 16U)
        || memcmp(phase11_qspi_verify, phase11_qspi_crossing + 16U, 16U) != 0
        || !phase11_qspi_read(P11_QSPI_SCRATCH_BASE + 0xF00U, phase11_qspi_verify,
                              sizeof(phase11_qspi_verify))
        || memcmp(phase11_qspi_verify, phase11_qspi_randomish,
                  sizeof(phase11_qspi_verify)) != 0)
    {
        return false;
    }
    rt_kprintf("P11,QSPI,CYCLE,OK,%lu,%08lx,%08lx\n", (unsigned long)cycle,
               (unsigned long)phase11_crc32(phase11_qspi_incrementing,
                                             sizeof(phase11_qspi_incrementing)),
               (unsigned long)phase11_crc32(phase11_qspi_randomish,
                                             sizeof(phase11_qspi_randomish)));
    return true;
}

static void phase11_qspi_hil_run_locked(uint32_t cycle_count)
{
    uint32_t cycle;
    uint8_t boundary[32];

    if (cycle_count == 0U || cycle_count > 100U || !phase11_qspi_init())
    {
        rt_kprintf("P11,QSPI,INIT,FAIL,0,0,0\n");
        return;
    }
    for (cycle = 0U; cycle < 100U; cycle++)
    {
        uint32_t jedec;

        if (!phase11_qspi_read_jedec(&jedec) || jedec != P11_QSPI_EXPECTED_JEDEC)
        {
            rt_kprintf("P11,QSPI,JEDEC,FAIL,%lu,%06lx,0\n", (unsigned long)cycle,
                       (unsigned long)jedec);
            return;
        }
    }
    if (!phase11_qspi_read(P11_QSPI_SCRATCH_BASE + 0x0U, boundary, sizeof(boundary))
        || !phase11_qspi_read(P11_QSPI_SCRATCH_BASE + 0xF0U, boundary, sizeof(boundary))
        || !phase11_qspi_read(P11_QSPI_SCRATCH_BASE + 0xFFU, boundary, sizeof(boundary))
        || !phase11_qspi_read(P11_QSPI_SCRATCH_BASE + 0x100U, boundary, sizeof(boundary))
        || !phase11_qspi_read(P11_QSPI_SCRATCH_BASE + 0xF00U, boundary, sizeof(boundary))
        || !phase11_qspi_read(P11_QSPI_SCRATCH_BASE + 0xFE0U, boundary, sizeof(boundary)))
    {
        rt_kprintf("P11,QSPI,READ,FAIL,0,0,0\n");
        return;
    }
    for (cycle = 1U; cycle <= cycle_count; cycle++)
    {
        if (!phase11_qspi_run_cycle(cycle))
        {
            rt_kprintf("P11,QSPI,CYCLE,FAIL,%lu,0,0\n", (unsigned long)cycle);
            return;
        }
    }
    (void)phase11_qspi_program(UINT32_C(0x00010000), boundary, sizeof(boundary));
    (void)phase11_qspi_erase(UINT32_C(0x00210000));
    rt_kprintf("P11,QSPI,COMPLETE,OK,%lu,0,0\n", (unsigned long)cycle_count);
}

void phase11_qspi_hil_run(uint32_t cycle_count)
{
    if (!ota_qspi_bus_lock())
    {
        rt_kprintf("P11,QSPI,INIT,FAIL,0,0,0\n");
        return;
    }
    phase11_qspi_hil_run_locked(cycle_count);
    ota_qspi_bus_unlock();
}

static void qspi_hil(int argc, char **argv)
{
    uint32_t cycle_count = 100U;

    if (argc == 2 && !phase11_qspi_parse_cycle_count(argv[1], &cycle_count))
    {
        rt_kprintf("P11,QSPI,INIT,FAIL,0,0,0\n");
        return;
    }
    if (argc > 2)
    {
        rt_kprintf("P11,QSPI,INIT,FAIL,0,0,0\n");
        return;
    }
    phase11_qspi_hil_run(cycle_count);
}
MSH_CMD_EXPORT(qspi_hil, run Phase 11 W25Q64 scratch-sector HIL test);
