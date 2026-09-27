#ifndef TRANSPORT_RECORDER_IMU_SPI_H
#define TRANSPORT_RECORDER_IMU_SPI_H

#include <stdint.h>

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef void (*board_imu_isr_handler_t)(void *context);

typedef void (*board_imu_dma_handler_t)(void *context,
                                        rt_err_t result);

rt_err_t board_imu_spi_dma_init(void);

rt_err_t board_imu_spi_transfer_dma(const uint8_t *tx,
                                    uint8_t *rx,
                                    rt_size_t length);

rt_err_t board_imu_spi_abort_dma(void);

rt_err_t board_imu_spi_set_dma_handler(board_imu_dma_handler_t handler,
                                       void *context);

rt_err_t board_imu_int1_init(void);

rt_err_t board_imu_int1_set_handler(board_imu_isr_handler_t handler,
                                    void *context);

rt_err_t board_imu_spi_init(void);

rt_err_t board_imu_spi_transfer(const uint8_t *tx,
                                uint8_t *rx,
                                rt_size_t length);

#ifdef __cplusplus
}
#endif

#endif
