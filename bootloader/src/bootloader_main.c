#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bootloader_action.h"
#include "bootloader_app.h"
#include "bootloader_copy.h"
#include "bootloader_hal.h"
#include "bootloader_state_store.h"
#include "memory_layout.h"
#include "stm32h7xx.h"

#ifndef BOOTLOADER_COPY_BYTES_PER_BOOT
#define BOOTLOADER_COPY_BYTES_PER_BOOT UINT32_C(4096)
#endif

static _Alignas(BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES)
uint8_t bootloader_copy_scratch[BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES];

void _init(void)
{
}

void _fini(void)
{
}

static ota_state_selection_t bootloader_next_selection(ota_state_selection_t selection)
{
    return selection == OTA_STATE_SELECT_PRIMARY ? OTA_STATE_SELECT_SECONDARY
                                                  : OTA_STATE_SELECT_PRIMARY;
}

static bool bootloader_prepare_install_state(const ota_state_record_t *current,
                                             ota_state_record_t *installing)
{
    if (current == NULL || installing == NULL)
    {
        return false;
    }
    if (current->state == OTA_STATE_INSTALLING)
    {
        *installing = *current;
        return true;
    }
    return ota_state_transition(current, OTA_STATE_INSTALLING, installing)
           == OTA_STATE_TRANSITION_OK;
}

static bool bootloader_install(const bootloader_storage_t *storage,
                               ota_state_selection_t selection,
                               const ota_state_record_t *current,
                               uint32_t package_offset,
                               const ota_package_manifest_t *manifest)
{
    bootloader_copy_request_t request;
    bootloader_copy_result_t copy_result;
    ota_state_record_t installing;

    if (storage == NULL || current == NULL || manifest == NULL
        || !bootloader_prepare_install_state(current, &installing))
    {
        return false;
    }
    if (installing.generation != current->generation)
    {
        if (!bootloader_state_store_persist(storage, selection, &installing))
        {
            return false;
        }
        selection = bootloader_next_selection(selection);
    }

    request.qspi_image_offset = package_offset + OTA_PACKAGE_MANIFEST_BYTES;
    request.image_length = manifest->image_length;
    request.maximum_copy_bytes = BOOTLOADER_COPY_BYTES_PER_BOOT;
    request.expected_image_crc32 = manifest->image_crc32;
    memcpy(request.expected_image_sha256, manifest->image_sha256,
           sizeof(request.expected_image_sha256));
    if (!bootloader_copy_engine_run(storage, &request, &installing,
                                    bootloader_copy_scratch,
                                    sizeof(bootloader_copy_scratch), &copy_result)
        || !bootloader_state_store_persist(storage, selection, &copy_result.next_state))
    {
        return false;
    }
    if (copy_result.complete)
    {
        NVIC_SystemReset();
    }
    return true;
}

void entry(void)
{
    bootloader_storage_t storage;
    bootloader_application_vectors_t vectors = {0};
    ota_state_record_t state = {0};
    ota_package_manifest_t candidate_manifest = {0};
    ota_package_manifest_t recovery_manifest = {0};
    bootloader_boot_context_t context;
    bootloader_boot_decision_t decision;
    ota_state_selection_t selection;
    int qspi_ready;

    if (!bootloader_platform_hal_init())
    {
        bootloader_recovery_wait();
    }
    qspi_ready = bootloader_qspi_hal_init();

    storage.internal_flash = bootloader_internal_flash_hal_ops();
    storage.qspi = bootloader_qspi_hal_ops();
    selection = bootloader_state_store_load(&storage, &state);
    context.state_selection = selection;
    context.state = state;
    context.application_is_valid = bootloader_application_vectors_read_and_validate(&storage,
                                                                                     &vectors);
    context.candidate_is_valid = qspi_ready && bootloader_package_validate_from_qspi(
        &storage, TRANSPORT_OTA_QSPI_CANDIDATE_OFFSET,
        TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES, &candidate_manifest);
    context.recovery_is_valid = qspi_ready && bootloader_package_validate_from_qspi(
        &storage, TRANSPORT_OTA_QSPI_RECOVERY_OFFSET,
        TRANSPORT_OTA_QSPI_RECOVERY_SIZE_BYTES, &recovery_manifest);
    if (!bootloader_select_action(&context, &decision))
    {
        bootloader_recovery_wait();
    }

    if (decision.persist_state)
    {
        if (!bootloader_state_store_persist(&storage, selection, &decision.next_state))
        {
            bootloader_recovery_wait();
        }
        selection = bootloader_next_selection(selection);
        state = decision.next_state;
    }

    switch (decision.action)
    {
    case BOOTLOADER_ACTION_BOOT_APPLICATION:
    case BOOTLOADER_ACTION_BOOT_TRIAL_APPLICATION:
    case BOOTLOADER_ACTION_PROVISION_NORMAL:
        bootloader_jump_to_application(&vectors);
        break;
    case BOOTLOADER_ACTION_INSTALL_CANDIDATE:
        if (bootloader_install(&storage, selection, &state,
                               TRANSPORT_OTA_QSPI_CANDIDATE_OFFSET,
                               &candidate_manifest))
        {
            NVIC_SystemReset();
        }
        break;
    case BOOTLOADER_ACTION_INSTALL_RECOVERY:
        if (bootloader_install(&storage, selection, &state,
                               TRANSPORT_OTA_QSPI_RECOVERY_OFFSET,
                               &recovery_manifest))
        {
            NVIC_SystemReset();
        }
        break;
    case BOOTLOADER_ACTION_RECOVERY:
    default:
        break;
    }
    bootloader_recovery_wait();
}
