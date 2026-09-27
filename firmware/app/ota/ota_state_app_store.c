#include "ota_state_app_store.h"

#include <string.h>

#include "memory_layout.h"
#include "ota_state.h"
#include "power_runtime.h"
#include "stm32h7xx_hal.h"

#define OTA_STATE_FLASH_PAGE_BYTES UINT32_C(0x00020000)
#define OTA_STATE_FLASHWORD_BYTES UINT32_C(32)

static _Alignas(OTA_STATE_FLASHWORD_BYTES) uint8_t encoded[OTA_STATE_FLASHWORD_BYTES * 2U];

static int state_page(uint32_t address, uint32_t *bank, uint32_t *sector)
{
    if (bank == NULL || sector == NULL)
    {
        return 0;
    }
    if (address == TRANSPORT_OTA_STATE_PRIMARY_BASE)
    {
        *bank = FLASH_BANK_2;
        *sector = 6U;
        return 1;
    }
    if (address == TRANSPORT_OTA_STATE_SECONDARY_BASE)
    {
        *bank = FLASH_BANK_2;
        *sector = 7U;
        return 1;
    }
    return 0;
}

static int erase_state_page(uint32_t address)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t bank;
    uint32_t sector;
    uint32_t sector_error = 0U;

    if (!state_page(address, &bank, &sector) || HAL_FLASH_Unlock() != HAL_OK)
    {
        return -1;
    }
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = bank;
    erase.Sector = sector;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK || sector_error != UINT32_MAX)
    {
        (void)HAL_FLASH_Lock();
        return -1;
    }
    (void)HAL_FLASH_Lock();
    return 0;
}

static int program_state_page(uint32_t address, const ota_state_record_t *record)
{
    if (record == NULL)
    {
        return -1;
    }
    memset(encoded, 0xFF, sizeof(encoded));
    ota_state_record_encode(record, encoded);
    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return -1;
    }
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address,
                          (uint32_t)(uintptr_t)encoded) != HAL_OK
        || HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                             address + OTA_STATE_FLASHWORD_BYTES,
                             (uint32_t)(uintptr_t)(encoded + OTA_STATE_FLASHWORD_BYTES))
               != HAL_OK)
    {
        (void)HAL_FLASH_Lock();
        return -1;
    }
    (void)HAL_FLASH_Lock();
    return 0;
}

static int state_store_transition(ota_state_kind_t next_state)
{
    uint8_t primary[OTA_STATE_RECORD_BYTES];
    uint8_t secondary[OTA_STATE_RECORD_BYTES];
    ota_state_record_t current;
    ota_state_record_t pending;
    ota_state_selection_t selection;
    uint32_t target;
    rt_bool_t ota_blocker_held;
    int result;

    memcpy(primary, (const void *)(uintptr_t)TRANSPORT_OTA_STATE_PRIMARY_BASE, sizeof(primary));
    memcpy(secondary, (const void *)(uintptr_t)TRANSPORT_OTA_STATE_SECONDARY_BASE,
           sizeof(secondary));
    selection = ota_state_select_newest(primary, secondary, &current);
    if (selection != OTA_STATE_SELECT_PRIMARY && selection != OTA_STATE_SELECT_SECONDARY)
    {
        return -1;
    }
    if (next_state == OTA_STATE_CONFIRMED && current.state != OTA_STATE_TRIAL)
    {
        return 1;
    }
    if (ota_state_transition(&current, next_state, &pending)
               != OTA_STATE_TRANSITION_OK)
    {
        return -1;
    }
    target = selection == OTA_STATE_SELECT_PRIMARY ? TRANSPORT_OTA_STATE_SECONDARY_BASE
                                                    : TRANSPORT_OTA_STATE_PRIMARY_BASE;
    ota_blocker_held = power_runtime_acquire_blocker(POWER_BLOCKER_OTA_DIAGNOSTIC)
                       == RT_EOK;
    if (!ota_blocker_held)
    {
        return -1;
    }
    result = erase_state_page(target) == 0
                 && program_state_page(target, &pending) == 0 ? 0 : -1;
    power_runtime_release_blocker(POWER_BLOCKER_OTA_DIAGNOSTIC);
    return result;
}

int ota_state_app_store_mark_pending_install(void *context)
{
    (void)context;
    return state_store_transition(OTA_STATE_PENDING_INSTALL);
}

int ota_state_app_store_confirm_trial(void *context)
{
    (void)context;
    return state_store_transition(OTA_STATE_CONFIRMED);
}
