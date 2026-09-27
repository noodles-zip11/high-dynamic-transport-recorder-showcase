#ifndef TRANSPORT_RECORDER_BOOTLOADER_HAL_H
#define TRANSPORT_RECORDER_BOOTLOADER_HAL_H

#include <stdbool.h>

#include "bootloader_app.h"
#include "bootloader_storage.h"

bool bootloader_platform_hal_init(void);
bootloader_storage_ops_t bootloader_internal_flash_hal_ops(void);
bool bootloader_qspi_hal_init(void);
bootloader_storage_ops_t bootloader_qspi_hal_ops(void);
void bootloader_jump_to_application(const bootloader_application_vectors_t *vectors);
void bootloader_recovery_wait(void);

#endif
