#ifndef TRANSPORT_RECORDER_SHT4X_I2C_H
#define TRANSPORT_RECORDER_SHT4X_I2C_H

#include <stdint.h>

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

rt_err_t board_sht4x_i2c_init(void);
rt_err_t board_sht4x_i2c_is_ready(uint8_t address);
rt_err_t board_sht4x_i2c_write(uint8_t address, const uint8_t *data,
                               rt_size_t length);
rt_err_t board_sht4x_i2c_read(uint8_t address, uint8_t *data,
                              rt_size_t length);

#ifdef __cplusplus
}
#endif

#endif
