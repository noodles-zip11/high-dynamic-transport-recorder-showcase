#ifndef TRANSPORT_RECORDER_BOOTLOADER_STATE_STORE_H
#define TRANSPORT_RECORDER_BOOTLOADER_STATE_STORE_H

#include <stdbool.h>

#include "bootloader_storage.h"
#include "ota_state.h"

ota_state_selection_t bootloader_state_store_load(const bootloader_storage_t *storage,
                                                  ota_state_record_t *state);
bool bootloader_state_store_persist(const bootloader_storage_t *storage,
                                    ota_state_selection_t current_selection,
                                    const ota_state_record_t *next_state);

#endif
