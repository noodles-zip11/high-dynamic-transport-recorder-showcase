#include "icm45686_fifo.h"

#define ICM45686_FIFO_ACCEL_GYRO_HEADER_MASK UINT8_C(0xF8)
#define ICM45686_FIFO_ACCEL_GYRO_HEADER_VALUE UINT8_C(0x68)


//工具函数
static uint16_t read_u16(const uint8_t *data,
                         icm45686_fifo_endian_t endian)
{
    if (endian == ICM45686_FIFO_BIG_ENDIAN)
    {
        return ((uint16_t)data[0] << 8U) | (uint16_t)data[1];
    }

    return ((uint16_t)data[1] << 8U) | (uint16_t)data[0];
}
static int16_t read_s16(const uint8_t *data,
                        icm45686_fifo_endian_t endian)
{
    uint16_t raw = read_u16(data, endian);

    if (raw <= INT16_MAX)
    {
        return (int16_t)raw;
    }

    return (int16_t)((int32_t)raw - INT32_C(65536));
}
//工具函数

static int8_t read_s8(uint8_t data)
{
    if (data <= INT8_MAX)
    {
        return (int8_t)data;
    }

    return (int8_t)((int16_t)data - INT16_C(256));
}
//工具函数

static void parse_packet(const uint8_t *data,
                         icm45686_fifo_endian_t endian,
                         icm45686_fifo_sample_t *sample)
{
    /* ICM45686 数据手册 §6.1：16 字节 Accel+Gyro FIFO 包。 */
    sample->header = data[0];

    sample->accel[0] = read_s16(&data[1], endian);
    sample->accel[1] = read_s16(&data[3], endian);
    sample->accel[2] = read_s16(&data[5], endian);

    sample->gyro[0] = read_s16(&data[7], endian);
    sample->gyro[1] = read_s16(&data[9], endian);
    sample->gyro[2] = read_s16(&data[11], endian);

    sample->temperature = read_s8(data[13]);
    sample->timestamp = read_u16(&data[14], endian);
}

icm45686_fifo_parse_result_t icm45686_fifo_parse_accel_gyro(
    const uint8_t *data,
    size_t data_length,
    icm45686_fifo_endian_t endian,
    icm45686_fifo_sample_t *samples,
    size_t sample_capacity)
{
    icm45686_fifo_parse_result_t result = {
        .bytes_consumed = 0U,
        .samples_produced = 0U,
        .status = ICM45686_FIFO_PARSE_OK,
    };

    if (data == NULL || samples == NULL
        || (endian != ICM45686_FIFO_LITTLE_ENDIAN
            && endian != ICM45686_FIFO_BIG_ENDIAN))
    {
        result.status = ICM45686_FIFO_PARSE_INVALID_ARGUMENT;
        return result;
    }

    while (data_length - result.bytes_consumed
           >= ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE)
    {
        const uint8_t *packet = &data[result.bytes_consumed];

        if (result.samples_produced >= sample_capacity)
        {
            result.status = ICM45686_FIFO_PARSE_OUTPUT_FULL;
            return result;
        }

        if ((packet[0] & ICM45686_FIFO_ACCEL_GYRO_HEADER_MASK)
            != ICM45686_FIFO_ACCEL_GYRO_HEADER_VALUE)
        {
            result.status = ICM45686_FIFO_PARSE_INVALID_HEADER;
            return result;
        }

        parse_packet(packet, endian, &samples[result.samples_produced]);

        result.bytes_consumed += ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE;
        result.samples_produced++;
    }

    if (result.bytes_consumed != data_length)
    {
        result.status = ICM45686_FIFO_PARSE_INCOMPLETE;
    }

    return result;
}
