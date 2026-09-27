#ifndef TRANSPORT_RECORDER_U8_NOR_SPI_H
#define TRANSPORT_RECORDER_U8_NOR_SPI_H

#include <stdint.h>

#include <rtthread.h>

rt_err_t board_u8_nor_spi_init(void);
rt_err_t board_u8_nor_spi_transfer(const uint8_t *tx,
                                   uint8_t *rx,
                                   rt_size_t length);

#endif
