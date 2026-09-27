#ifndef TRANSPORT_RECORDER_BOOTLOADER_ACTION_H
#define TRANSPORT_RECORDER_BOOTLOADER_ACTION_H

#include <stdbool.h>

#include "ota_state.h"

typedef enum
{
    BOOTLOADER_ACTION_BOOT_APPLICATION = 0,
    BOOTLOADER_ACTION_BOOT_TRIAL_APPLICATION,
    BOOTLOADER_ACTION_INSTALL_CANDIDATE,
    BOOTLOADER_ACTION_INSTALL_RECOVERY,
    BOOTLOADER_ACTION_PROVISION_NORMAL,
    BOOTLOADER_ACTION_RECOVERY,
} bootloader_action_t;

typedef struct
{
    ota_state_selection_t state_selection;
    ota_state_record_t state;
    int application_is_valid;
    int candidate_is_valid;
    int recovery_is_valid;
} bootloader_boot_context_t;

typedef struct
{
    bootloader_action_t action;
    bool persist_state;
    ota_state_record_t next_state;
} bootloader_boot_decision_t;

bool bootloader_select_action(const bootloader_boot_context_t *context,
                              bootloader_boot_decision_t *decision);

#endif
