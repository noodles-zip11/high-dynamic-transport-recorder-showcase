#include <stdbool.h>
#include <stdint.h>

#include "bootloader_hal.h"
#include "bootloader_qspi_profile.h"
#include "memory_layout.h"
#include "stm32h7xx_hal.h"

#define BOOTLOADER_QSPI_TIMEOUT_MS UINT32_C(1000)

static QSPI_HandleTypeDef bootloader_qspi;
static bool bootloader_qspi_ready;

static void bootloader_qspi_command_defaults(QSPI_CommandTypeDef *command)
{
    command->InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command->AddressSize = QSPI_ADDRESS_24_BITS;
    command->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command->DummyCycles = 0U;
    command->DdrMode = QSPI_DDR_MODE_DISABLE;
    command->DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command->SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
}

static int bootloader_qspi_range_is_valid(uint32_t address, uint32_t length)
{
    return address < TRANSPORT_OTA_QSPI_SIZE_BYTES
           && length <= TRANSPORT_OTA_QSPI_SIZE_BYTES
           && address <= TRANSPORT_OTA_QSPI_SIZE_BYTES - length;
}

static int bootloader_qspi_read_jedec(uint32_t *jedec_id)
{
    QSPI_CommandTypeDef command = {0};
    uint8_t bytes[3];

    if (jedec_id == NULL)
    {
        return 0;
    }
    bootloader_qspi_command_defaults(&command);
    command.Instruction = 0x9FU;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = sizeof(bytes);
    if (HAL_QSPI_Command(&bootloader_qspi, &command, BOOTLOADER_QSPI_TIMEOUT_MS) != HAL_OK
        || HAL_QSPI_Receive(&bootloader_qspi, bytes, BOOTLOADER_QSPI_TIMEOUT_MS) != HAL_OK)
    {
        return 0;
    }
    *jedec_id = ((uint32_t)bytes[0] << 16U) | ((uint32_t)bytes[1] << 8U) | bytes[2];
    return 1;
}

static int bootloader_qspi_write_enable(void)
{
    QSPI_CommandTypeDef command = {0};

    bootloader_qspi_command_defaults(&command);
    command.Instruction = 0x06U;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.DataMode = QSPI_DATA_NONE;
    return HAL_QSPI_Command(&bootloader_qspi, &command, BOOTLOADER_QSPI_TIMEOUT_MS)
           == HAL_OK;
}

static int bootloader_qspi_wait_ready(void)
{
    QSPI_CommandTypeDef command = {0};
    QSPI_AutoPollingTypeDef config = {0};

    bootloader_qspi_command_defaults(&command);
    command.Instruction = 0x05U;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = 1U;
    config.Match = 0U;
    config.Mask = 0x01U;
    config.MatchMode = QSPI_MATCH_MODE_AND;
    config.StatusBytesSize = 1U;
    config.Interval = 0x10U;
    config.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;
    return HAL_QSPI_AutoPolling(&bootloader_qspi, &command, &config,
                                BOOTLOADER_QSPI_TIMEOUT_MS)
           == HAL_OK;
}

static int bootloader_qspi_mutation_is_allowed(void)
{
    uint32_t jedec_id;

    return bootloader_qspi_ready && bootloader_qspi_read_jedec(&jedec_id)
           && bootloader_qspi_profile_is_expected_jedec(jedec_id);
}

static int bootloader_qspi_read(void *context, uint32_t address, uint8_t *data,
                                uint32_t length)
{
    QSPI_CommandTypeDef command = {0};

    (void)context;
    if (!bootloader_qspi_ready || (data == NULL && length != 0U)
        || !bootloader_qspi_range_is_valid(address, length))
    {
        return 0;
    }
    if (length == 0U)
    {
        return 1;
    }
    bootloader_qspi_command_defaults(&command);
    command.Instruction = BOOTLOADER_QSPI_READ_COMMAND;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.Address = address;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = length;
    return HAL_QSPI_Command(&bootloader_qspi, &command, BOOTLOADER_QSPI_TIMEOUT_MS) == HAL_OK
           && HAL_QSPI_Receive(&bootloader_qspi, data, BOOTLOADER_QSPI_TIMEOUT_MS) == HAL_OK;
}

static int bootloader_qspi_erase(void *context, uint32_t address, uint32_t length)
{
    QSPI_CommandTypeDef command = {0};

    (void)context;
    if (length != BOOTLOADER_QSPI_ERASE_BYTES
        || address % BOOTLOADER_QSPI_ERASE_BYTES != 0U
        || !bootloader_qspi_range_is_valid(address, length)
        || !bootloader_qspi_mutation_is_allowed()
        || !bootloader_qspi_write_enable())
    {
        return 0;
    }
    bootloader_qspi_command_defaults(&command);
    command.Instruction = BOOTLOADER_QSPI_ERASE_COMMAND;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.Address = address;
    command.DataMode = QSPI_DATA_NONE;
    return HAL_QSPI_Command(&bootloader_qspi, &command, BOOTLOADER_QSPI_TIMEOUT_MS) == HAL_OK
           && bootloader_qspi_wait_ready();
}

static int bootloader_qspi_program(void *context, uint32_t address,
                                   const uint8_t *data, uint32_t length)
{
    QSPI_CommandTypeDef command = {0};

    (void)context;
    if (data == NULL || length == 0U || length > BOOTLOADER_QSPI_PAGE_BYTES
        || !bootloader_qspi_range_is_valid(address, length)
        || address / BOOTLOADER_QSPI_PAGE_BYTES
               != (address + length - 1U) / BOOTLOADER_QSPI_PAGE_BYTES
        || !bootloader_qspi_mutation_is_allowed()
        || !bootloader_qspi_write_enable())
    {
        return 0;
    }
    bootloader_qspi_command_defaults(&command);
    command.Instruction = BOOTLOADER_QSPI_PROGRAM_COMMAND;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.Address = address;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = length;
    return HAL_QSPI_Command(&bootloader_qspi, &command, BOOTLOADER_QSPI_TIMEOUT_MS) == HAL_OK
           && HAL_QSPI_Transmit(&bootloader_qspi, (uint8_t *)data,
                                BOOTLOADER_QSPI_TIMEOUT_MS) == HAL_OK
           && bootloader_qspi_wait_ready();
}

bool bootloader_qspi_hal_init(void)
{
    RCC_PeriphCLKInitTypeDef clock_config = {0};
    uint32_t jedec_id;

    clock_config.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
    clock_config.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&clock_config) != HAL_OK)
    {
        return false;
    }
    bootloader_qspi.Instance = QUADSPI;
    bootloader_qspi.Init.ClockPrescaler = BOOTLOADER_QSPI_CLOCK_PRESCALER;
    bootloader_qspi.Init.FifoThreshold = 1U;
    bootloader_qspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    bootloader_qspi.Init.FlashSize = BOOTLOADER_QSPI_FLASH_SIZE;
    bootloader_qspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
    bootloader_qspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
    bootloader_qspi.Init.FlashID = QSPI_FLASH_ID_1;
    bootloader_qspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
    if (HAL_QSPI_Init(&bootloader_qspi) != HAL_OK
        || !bootloader_qspi_read_jedec(&jedec_id))
    {
        bootloader_qspi_ready = false;
        return false;
    }
    bootloader_qspi_ready = bootloader_qspi_profile_is_expected_jedec(jedec_id);
    return bootloader_qspi_ready;
}

bootloader_storage_ops_t bootloader_qspi_hal_ops(void)
{
    const bootloader_storage_ops_t ops = {
        .context = NULL,
        .read = bootloader_qspi_read,
        .erase = bootloader_qspi_erase,
        .program = bootloader_qspi_program,
    };

    return ops;
}
