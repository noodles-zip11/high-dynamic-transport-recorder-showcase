#ifndef TRANSPORT_RECORDER_ICM45686_FIFO_H
#define TRANSPORT_RECORDER_ICM45686_FIFO_H

#include <stddef.h>
#include <stdint.h>

#define ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE 16U


typedef enum
{
    ICM45686_FIFO_PARSE_OK = 0,
    ICM45686_FIFO_PARSE_INCOMPLETE, //包还没收全
    ICM45686_FIFO_PARSE_INVALID_HEADER, //收到的不是当前支持的 16 字节六轴包
    ICM45686_FIFO_PARSE_OUTPUT_FULL, //调用者给的样本数组装满了
    ICM45686_FIFO_PARSE_INVALID_ARGUMENT, //空指针或错误参数
} icm45686_fifo_parse_status_t;


typedef struct
{
    int16_t accel[3];  //X、Y、Z 加速度原始值
    int16_t gyro[3];   //X、Y、Z 角速度原始值
    int8_t temperature; //16 字节 FIFO 帧中的 1 字节温度原始值
    uint16_t timestamp; //FIFO 内的 2 字节时间戳
    uint8_t header;      //保留原始包头，之后可查看 ODR 变化、FSYNC 等标志
} icm45686_fifo_sample_t;

typedef enum
{
    ICM45686_FIFO_LITTLE_ENDIAN = 0,
    ICM45686_FIFO_BIG_ENDIAN,
} icm45686_fifo_endian_t;

typedef struct
{
    size_t bytes_consumed;
    size_t samples_produced;
    icm45686_fifo_parse_status_t status;
} icm45686_fifo_parse_result_t;

icm45686_fifo_parse_result_t icm45686_fifo_parse_accel_gyro(
    const uint8_t *data,
    size_t data_length,
    icm45686_fifo_endian_t endian,
    icm45686_fifo_sample_t *samples,
    size_t sample_capacity);


#endif
