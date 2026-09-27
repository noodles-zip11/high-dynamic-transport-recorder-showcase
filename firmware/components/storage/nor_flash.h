#ifndef TRANSPORT_RECORDER_NOR_FLASH_H
#define TRANSPORT_RECORDER_NOR_FLASH_H

#include <stdint.h>

#include <rtthread.h>

typedef struct nor_flash nor_flash_t;

typedef rt_err_t (*nor_flash_read_fn)(nor_flash_t *flash,
                                      uint32_t offset,
                                      uint8_t *data,
                                      uint32_t length);
typedef rt_err_t (*nor_flash_program_fn)(nor_flash_t *flash,
                                         uint32_t offset,
                                         const uint8_t *data,
                                         uint32_t length);
typedef rt_err_t (*nor_flash_erase_sector_fn)(nor_flash_t *flash,
                                              uint32_t offset);

struct nor_flash
{
    void *context;
    uint32_t capacity_bytes;
    uint32_t erase_sector_bytes;
    uint32_t page_bytes;
    nor_flash_read_fn read;
    nor_flash_program_fn program;
    nor_flash_erase_sector_fn erase_sector;
};

#endif
