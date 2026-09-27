#ifndef TRANSPORT_RECORDER_NOR_FLASH_W25Q_H
#define TRANSPORT_RECORDER_NOR_FLASH_W25Q_H

#include <stdint.h>

#include "nor_flash.h"

typedef rt_err_t (*nor_flash_transfer_fn)(void *context,
                                          const uint8_t *tx,
                                          uint8_t *rx,
                                          uint32_t length);

typedef struct
{
    nor_flash_t device;
    nor_flash_transfer_fn transfer;
    void *transfer_context;
} nor_flash_w25q_t;

void nor_flash_w25q_init(nor_flash_w25q_t *flash,
                         nor_flash_transfer_fn transfer,
                         void *transfer_context);
rt_err_t nor_flash_w25q_probe(nor_flash_w25q_t *flash,
                              uint8_t jedec_id[3]);

#endif
