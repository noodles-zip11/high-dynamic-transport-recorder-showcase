#ifndef TRANSPORT_RECORDER_ICM45686_H
#define TRANSPORT_RECORDER_ICM45686_H

#include <stdint.h>

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ICM45686_REG_WHO_AM_I UINT8_C(0x72)
#define ICM45686_WHO_AM_I_VALUE UINT8_C(0xE9)

#define ICM45686_REG_SREG_CTRL UINT8_C(0x67)
#define ICM45686_SREG_CTRL_DATA_BIG_ENDIAN UINT8_C(0x02)
#define ICM45686_REG_IREG_ADDR_15_8 UINT8_C(0x7C)
#define ICM45686_REG_IREG_DATA UINT8_C(0x7E)
#define ICM45686_INDIRECT_SREG_CTRL_ADDRESS UINT16_C(0xA267)
#define ICM45686_INDIRECT_INT1_PULSE_ADDRESS UINT16_C(0xA26A)

#define ICM45686_REG_FIFO_COUNT_0 UINT8_C(0x12)
#define ICM45686_REG_FIFO_COUNT_1 UINT8_C(0x13)
#define ICM45686_REG_FIFO_DATA UINT8_C(0x14)

//寄存器地址
#define ICM45686_REG_PWR_MGMT0 UINT8_C(0x10)
#define ICM45686_REG_ACCEL_CONFIG0 UINT8_C(0x1B)
#define ICM45686_REG_GYRO_CONFIG0 UINT8_C(0x1C)

//写入寄存器的值：1.6 kHz、Accel ±16 g、Gyro ±2000 dps
#define ICM45686_PWR_MGMT0_ACCEL_GYRO_LN UINT8_C(0x0F)
#define ICM45686_ACCEL_CONFIG0_16G_1600HZ UINT8_C(0x15)
#define ICM45686_GYRO_CONFIG0_2000DPS_1600HZ UINT8_C(0x15)

//寄存器地址
#define ICM45686_REG_FIFO_CONFIG0 UINT8_C(0x1D)
#define ICM45686_REG_FIFO_CONFIG1_0 UINT8_C(0x1E)
#define ICM45686_REG_FIFO_CONFIG1_1 UINT8_C(0x1F)
#define ICM45686_REG_FIFO_CONFIG3 UINT8_C(0x21)
#define ICM45686_REG_FIFO_CONFIG4 UINT8_C(0x22)

//写入寄存器，FIFO模式和深度
#define ICM45686_FIFO_CONFIG0_BYPASS_2KB UINT8_C(0x07)
#define ICM45686_FIFO_CONFIG0_STREAM_2KB UINT8_C(0x47)
#define ICM45686_FIFO_CONFIG3_ACCEL_GYRO_IF UINT8_C(0x07)
#define ICM45686_FIFO_CONFIG4_TIMESTAMP UINT8_C(0x02)
#define ICM45686_FIFO_SAMPLE_BYTES UINT16_C(16)
#define ICM45686_FIFO_WATERMARK_SAMPLES UINT16_C(32)

#define ICM45686_REG_INT1_CONFIG0 UINT8_C(0x16)
#define ICM45686_REG_INT1_CONFIG1 UINT8_C(0x17)
#define ICM45686_REG_INT1_CONFIG2 UINT8_C(0x18)
#define ICM45686_REG_INT1_STATUS0 UINT8_C(0x19)

#define ICM45686_REG_INT_PULSE_MIN_ON_INTF1 UINT8_C(0x6A)

#define ICM45686_INT1_CONFIG0_FIFO_THS_ENABLE UINT8_C(0x02)
#define ICM45686_INT1_CONFIG2_PUSH_PULL_PULSE_ACTIVE_HIGH UINT8_C(0x01)
#define ICM45686_INT1_PULSE_DURATION_100US UINT8_C(0x00)




typedef rt_err_t (*icm45686_transfer_fn)(const uint8_t *tx,
                                         uint8_t *rx,
                                         rt_size_t length);


typedef struct
{
    icm45686_transfer_fn transfer;
} icm45686_bus_t;

typedef struct
{
    icm45686_bus_t bus;
} icm45686_t;

rt_err_t icm45686_init(icm45686_t *device,
                       const icm45686_bus_t *bus);

rt_err_t icm45686_read_reg(icm45686_t *device,
                           uint8_t register_address,
                           uint8_t *data,
                           rt_size_t length);

rt_err_t icm45686_write_reg(icm45686_t *device,
                            uint8_t register_address,
                            const uint8_t *data,
                            rt_size_t length);

rt_err_t icm45686_probe(icm45686_t *device);

rt_err_t icm45686_configure_1600hz_16g_2000dps(icm45686_t *device);

rt_err_t icm45686_configure_fifo_accel_gyro_32_samples(
    icm45686_t *device);

rt_err_t icm45686_configure_big_endian(icm45686_t *device);

rt_err_t icm45686_read_fifo_count(icm45686_t *device,
                                  uint16_t *fifo_count_bytes);

rt_err_t icm45686_read_fifo_dma(icm45686_t *device,
                                icm45686_transfer_fn transfer_dma,
                                uint8_t *tx,
                                uint8_t *rx,
                                rt_size_t fifo_data_length);

rt_err_t icm45686_configure_int1_fifo_watermark(
    icm45686_t *device);

rt_err_t icm45686_verify_acquisition_config(icm45686_t *device);

rt_err_t icm45686_configure_acquisition(icm45686_t *device);
#ifdef __cplusplus
}
#endif

#endif
