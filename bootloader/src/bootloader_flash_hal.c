#include <stdint.h>
#include <string.h>

#include "bootloader_copy.h"
#include "bootloader_flash_policy.h"
#include "bootloader_hal.h"
#include "memory_layout.h"
#include "stm32h7xx_hal.h"

#define BOOTLOADER_FLASH_BANK_BYTES UINT32_C(0x00100000)

static int bootloader_internal_flash_read(void *context, uint32_t address,
                                          uint8_t *data, uint32_t length)
{
    (void)context;
    if (data == NULL || address < TRANSPORT_OTA_FLASH_BASE
        || length > TRANSPORT_OTA_FLASH_SIZE_BYTES
        || address - TRANSPORT_OTA_FLASH_BASE
               > TRANSPORT_OTA_FLASH_SIZE_BYTES - length)
    {
        return 0;
    }
    memcpy(data, (const void *)(uintptr_t)address, length);
    return 1;
}

static int bootloader_flash_sector_for_address(uint32_t address, uint32_t *bank,
                                               uint32_t *sector)
{
    uint32_t bank_base;

    if (bank == NULL || sector == NULL || address < TRANSPORT_OTA_FLASH_BASE
        || address >= TRANSPORT_OTA_FLASH_BASE + TRANSPORT_OTA_FLASH_SIZE_BYTES
        || address % BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES != 0U)
    {
        return 0;
    }
    if (address < TRANSPORT_OTA_FLASH_BASE + BOOTLOADER_FLASH_BANK_BYTES)
    {
        *bank = FLASH_BANK_1;
        bank_base = TRANSPORT_OTA_FLASH_BASE;
    }
    else
    {
        *bank = FLASH_BANK_2;
        bank_base = TRANSPORT_OTA_FLASH_BASE + BOOTLOADER_FLASH_BANK_BYTES;
    }
    *sector = (address - bank_base) / BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES;
    return *sector <= FLASH_SECTOR_7;
}

static int bootloader_internal_flash_erase(void *context, uint32_t address,
                                           uint32_t length)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0U;
    uint32_t bank;
    uint32_t sector;
    HAL_StatusTypeDef status;

    (void)context;
    if (length != BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES
        || !bootloader_flash_range_is_mutable(address, length)
        || !bootloader_flash_sector_for_address(address, &bank, &sector)
        || HAL_FLASH_Unlock() != HAL_OK)
    {
        return 0;
    }

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = bank;
    erase.Sector = sector;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    {
        const uint32_t primask = __get_PRIMASK();

        __disable_irq();
        status = HAL_FLASHEx_Erase(&erase, &sector_error);
        if (primask == 0U)
        {
            __enable_irq();
        }
    }
    (void)HAL_FLASH_Lock();
    return status == HAL_OK && sector_error == UINT32_MAX;
}

static int bootloader_internal_flash_program(void *context, uint32_t address,
                                             const uint8_t *data, uint32_t length)
{
    HAL_StatusTypeDef status;

    (void)context;
    if (data == NULL
        || address % BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES != 0U
        || length != BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES
        || (uintptr_t)data % BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES != 0U
        || !bootloader_flash_range_is_mutable(address, length)
        || HAL_FLASH_Unlock() != HAL_OK)
    {
        return 0;
    }
    {
        const uint32_t primask = __get_PRIMASK();

        __disable_irq();
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address,
                                   (uint32_t)(uintptr_t)data);
        if (primask == 0U)
        {
            __enable_irq();
        }
    }
    (void)HAL_FLASH_Lock();
    return status == HAL_OK;
}

bootloader_storage_ops_t bootloader_internal_flash_hal_ops(void)
{
    const bootloader_storage_ops_t ops = {
        .context = NULL,
        .read = bootloader_internal_flash_read,
        .erase = bootloader_internal_flash_erase,
        .program = bootloader_internal_flash_program,
    };

    return ops;
}
