#ifndef TRANSPORT_RECORDER_BOOTLOADER_FLASH_POLICY_H
#define TRANSPORT_RECORDER_BOOTLOADER_FLASH_POLICY_H

#include <stdint.h>

#include "memory_layout.h"

/*
 * Return whether a non-empty internal-Flash range is writable by the
 * Bootloader.  The upper-bound checks must precede subtraction so a caller
 * cannot turn an out-of-range address into an unsigned-underflowed length.
 */
static inline int bootloader_flash_range_is_mutable(uint32_t address,
                                                    uint32_t length)
{
    const uint32_t application_end = TRANSPORT_OTA_APPLICATION_BASE
                                     + TRANSPORT_OTA_APPLICATION_SIZE_BYTES;

    if (length == 0U)
    {
        return 0;
    }
    if (address >= TRANSPORT_OTA_APPLICATION_BASE
        && address <= application_end
        && length <= application_end - address)
    {
        return 1;
    }
    return (address >= TRANSPORT_OTA_STATE_PRIMARY_BASE
            && address <= TRANSPORT_OTA_STATE_PRIMARY_BASE
                              + TRANSPORT_OTA_STATE_RECORD_SIZE_BYTES
            && length <= TRANSPORT_OTA_STATE_RECORD_SIZE_BYTES
            && address - TRANSPORT_OTA_STATE_PRIMARY_BASE
                   <= TRANSPORT_OTA_STATE_RECORD_SIZE_BYTES - length)
           || (address >= TRANSPORT_OTA_STATE_SECONDARY_BASE
               && address <= TRANSPORT_OTA_STATE_SECONDARY_BASE
                                 + TRANSPORT_OTA_STATE_RECORD_SIZE_BYTES
               && length <= TRANSPORT_OTA_STATE_RECORD_SIZE_BYTES
               && address - TRANSPORT_OTA_STATE_SECONDARY_BASE
                      <= TRANSPORT_OTA_STATE_RECORD_SIZE_BYTES - length);
}

#endif
