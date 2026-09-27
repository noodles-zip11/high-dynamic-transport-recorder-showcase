#ifndef TRANSPORT_RECORDER_BOOTLOADER_STORAGE_H
#define TRANSPORT_RECORDER_BOOTLOADER_STORAGE_H

#include <stdint.h>

typedef int (*bootloader_read_fn_t)(void *context, uint32_t address,
                                    uint8_t *data, uint32_t length);
typedef int (*bootloader_erase_fn_t)(void *context, uint32_t address,
                                     uint32_t length);
typedef int (*bootloader_program_fn_t)(void *context, uint32_t address,
                                       const uint8_t *data, uint32_t length);

typedef struct
{
    void *context;
    bootloader_read_fn_t read;
    bootloader_erase_fn_t erase;
    bootloader_program_fn_t program;
} bootloader_storage_ops_t;

typedef struct
{
    bootloader_storage_ops_t internal_flash;
    bootloader_storage_ops_t qspi;
} bootloader_storage_t;

#endif
