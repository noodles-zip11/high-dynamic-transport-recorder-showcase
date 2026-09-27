#include <stdbool.h>
#include <stddef.h>

#include "bootloader_action.h"

static void bootloader_set_recovery(bootloader_boot_decision_t *decision)
{
    decision->action = BOOTLOADER_ACTION_RECOVERY;
    decision->persist_state = false;
}

bool bootloader_select_action(const bootloader_boot_context_t *context,
                              bootloader_boot_decision_t *decision)
{
    ota_state_record_t next_state;
    ota_state_trial_result_t trial_result;

    if (context == NULL || decision == NULL)
    {
        return false;
    }

    decision->next_state = context->state;
    decision->persist_state = false;
    if (context->state_selection == OTA_STATE_SELECT_RECOVERY)
    {
        bootloader_set_recovery(decision);
        return true;
    }
    if (context->state_selection == OTA_STATE_SELECT_UNINITIALIZED)
    {
        if (context->application_is_valid)
        {
            decision->action = BOOTLOADER_ACTION_PROVISION_NORMAL;
            decision->persist_state = true;
            decision->next_state.generation = 0U;
            decision->next_state.state = OTA_STATE_NORMAL;
            decision->next_state.trial_count = 0U;
            decision->next_state.copy_offset = 0U;
        }
        else
        {
            bootloader_set_recovery(decision);
        }
        return true;
    }

    switch (context->state.state)
    {
    case OTA_STATE_NORMAL:
    case OTA_STATE_CONFIRMED:
        if (context->application_is_valid)
        {
            decision->action = BOOTLOADER_ACTION_BOOT_APPLICATION;
        }
        else
        {
            bootloader_set_recovery(decision);
        }
        return true;

    case OTA_STATE_PENDING_INSTALL:
    case OTA_STATE_INSTALLING:
        if (context->candidate_is_valid)
        {
            decision->action = BOOTLOADER_ACTION_INSTALL_CANDIDATE;
        }
        else
        {
            bootloader_set_recovery(decision);
        }
        return true;

    case OTA_STATE_TRIAL:
        if (!context->application_is_valid)
        {
            bootloader_set_recovery(decision);
            return true;
        }
        trial_result = ota_state_prepare_trial_failure(&context->state, &next_state);
        if (trial_result == OTA_STATE_TRIAL_CONTINUE)
        {
            decision->action = BOOTLOADER_ACTION_BOOT_TRIAL_APPLICATION;
            decision->persist_state = true;
            decision->next_state = next_state;
        }
        else if (trial_result == OTA_STATE_TRIAL_ROLLBACK_REQUIRED
                 && context->recovery_is_valid)
        {
            decision->action = BOOTLOADER_ACTION_INSTALL_RECOVERY;
            decision->persist_state = true;
            decision->next_state = next_state;
        }
        else
        {
            bootloader_set_recovery(decision);
        }
        return true;

    case OTA_STATE_ROLLBACK_REQUIRED:
        if (context->recovery_is_valid)
        {
            decision->action = BOOTLOADER_ACTION_INSTALL_RECOVERY;
        }
        else
        {
            bootloader_set_recovery(decision);
        }
        return true;

    default:
        bootloader_set_recovery(decision);
        return true;
    }
}
