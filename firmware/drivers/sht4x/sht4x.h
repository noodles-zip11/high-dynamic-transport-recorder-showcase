#ifndef TRANSPORT_RECORDER_SHT4X_H
#define TRANSPORT_RECORDER_SHT4X_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHT4X_ADDRESS_0X44 0x44U
#define SHT4X_ADDRESS_0X45 0x45U
#define SHT4X_MEASURE_HIGH_PRECISION_COMMAND 0xFDU

typedef rt_err_t (*sht4x_write_fn)(uint8_t address,
                                   const uint8_t *data,
                                   rt_size_t length,
                                   void *context);
typedef rt_err_t (*sht4x_read_fn)(uint8_t address,
                                  uint8_t *data,
                                  rt_size_t length,
                                  void *context);
typedef void (*sht4x_delay_ms_fn)(uint32_t milliseconds, void *context);

typedef struct
{
    sht4x_write_fn write;
    sht4x_read_fn read;
    sht4x_delay_ms_fn delay_ms;
    void *context;
} sht4x_bus_t;

typedef struct
{
    int16_t temperature_centi_c;
    uint32_t humidity_milli_rh;
} sht4x_measurement_t;

typedef struct
{
    sht4x_bus_t bus;
    uint8_t address;
} sht4x_t;

uint8_t sht4x_crc8(const uint8_t *data, rt_size_t length);
rt_err_t sht4x_init(sht4x_t *device, const sht4x_bus_t *bus, uint8_t address);
rt_err_t sht4x_probe(const sht4x_bus_t *bus, uint8_t *address_out);
rt_err_t sht4x_measure(sht4x_t *device, sht4x_measurement_t *measurement);

#ifdef __cplusplus
}
#endif

#endif
