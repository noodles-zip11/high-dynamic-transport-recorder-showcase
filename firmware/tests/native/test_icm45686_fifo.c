#include <stdint.h>
#include <stdio.h>

#include "icm45686_fifo.h"

static const uint8_t big_endian_packet[ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE] = {
    UINT8_C(0x68), UINT8_C(0x12), UINT8_C(0x34), UINT8_C(0xFF),
    UINT8_C(0xFE), UINT8_C(0x80), UINT8_C(0x01), UINT8_C(0x7F),
    UINT8_C(0xFF), UINT8_C(0x80), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0x01), UINT8_C(0xF0), UINT8_C(0xBE), UINT8_C(0xEF),
};

static const uint8_t little_endian_packet[ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE] = {
    UINT8_C(0x68), UINT8_C(0x34), UINT8_C(0x12), UINT8_C(0xFE),
    UINT8_C(0xFF), UINT8_C(0x01), UINT8_C(0x80), UINT8_C(0xFF),
    UINT8_C(0x7F), UINT8_C(0x00), UINT8_C(0x80), UINT8_C(0x01),
    UINT8_C(0x00), UINT8_C(0xF0), UINT8_C(0xEF), UINT8_C(0xBE),
};

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "icm45686 fifo: %s\n", message);
        return 0;
    }

    return 1;
}

static int expect_sample(const icm45686_fifo_sample_t *sample)
{
    return expect(sample->header == UINT8_C(0x68), "header mismatch")
        && expect(sample->accel[0] == INT16_C(0x1234), "accel X mismatch")
        && expect(sample->accel[1] == -INT16_C(2), "accel Y mismatch")
        && expect(sample->accel[2] == -INT16_C(32767), "accel Z mismatch")
        && expect(sample->gyro[0] == INT16_MAX, "gyro X mismatch")
        && expect(sample->gyro[1] == INT16_MIN, "gyro Y mismatch")
        && expect(sample->gyro[2] == INT16_C(1), "gyro Z mismatch")
        && expect(sample->temperature == -INT8_C(16), "temperature mismatch")
        && expect(sample->timestamp == UINT16_C(0xBEEF), "timestamp mismatch");
}

static int test_packet(const uint8_t *packet, icm45686_fifo_endian_t endian)
{
    icm45686_fifo_sample_t sample = {0};
    icm45686_fifo_parse_result_t result;

    result = icm45686_fifo_parse_accel_gyro(
        packet,
        ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE,
        endian,
        &sample,
        1U);

    return expect(result.status == ICM45686_FIFO_PARSE_OK,
                  "complete packet must parse successfully")
        && expect(result.bytes_consumed == ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE,
                  "complete packet must consume 16 bytes")
        && expect(result.samples_produced == 1U,
                  "complete packet must produce one sample")
        && expect_sample(&sample);
}

static int test_partial_packet(void)
{
    icm45686_fifo_sample_t sample = {0};
    icm45686_fifo_parse_result_t result;

    result = icm45686_fifo_parse_accel_gyro(
        big_endian_packet,
        ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE - 1U,
        ICM45686_FIFO_BIG_ENDIAN,
        &sample,
        1U);

    return expect(result.status == ICM45686_FIFO_PARSE_INCOMPLETE,
                  "partial packet must be retained for the next buffer")
        && expect(result.bytes_consumed == 0U,
                  "partial packet must not consume bytes")
        && expect(result.samples_produced == 0U,
                  "partial packet must not produce a sample");
}

static int test_invalid_header(void)
{
    uint8_t packet[ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE] = {0};
    icm45686_fifo_sample_t sample = {0};
    icm45686_fifo_parse_result_t result;

    packet[0] = UINT8_C(0x60);
    result = icm45686_fifo_parse_accel_gyro(
        packet,
        sizeof(packet),
        ICM45686_FIFO_BIG_ENDIAN,
        &sample,
        1U);

    return expect(result.status == ICM45686_FIFO_PARSE_INVALID_HEADER,
                  "non-16-byte header must be rejected")
        && expect(result.bytes_consumed == 0U,
                  "invalid header must not consume bytes")
        && expect(result.samples_produced == 0U,
                  "invalid header must not produce a sample");
}

static int test_output_full(void)
{
    uint8_t packets[ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE * 2U] = {0};
    icm45686_fifo_sample_t sample = {0};
    icm45686_fifo_parse_result_t result;
    size_t index;

    for (index = 0U; index < ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE; index++)
    {
        packets[index] = big_endian_packet[index];
        packets[index + ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE] = big_endian_packet[index];
    }

    result = icm45686_fifo_parse_accel_gyro(
        packets,
        sizeof(packets),
        ICM45686_FIFO_BIG_ENDIAN,
        &sample,
        1U);

    return expect(result.status == ICM45686_FIFO_PARSE_OUTPUT_FULL,
                  "parser must report a full output buffer")
        && expect(result.bytes_consumed == ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE,
                  "output-full parser must consume only one packet")
        && expect(result.samples_produced == 1U,
                  "output-full parser must produce one sample")
        && expect_sample(&sample);
}

static int test_multiple_packets(void)
{
    uint8_t packets[ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE * 2U] = {0};
    icm45686_fifo_sample_t samples[2] = {0};
    icm45686_fifo_parse_result_t result;
    size_t index;

    for (index = 0U; index < ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE; index++)
    {
        packets[index] = big_endian_packet[index];
        packets[index + ICM45686_FIFO_ACCEL_GYRO_PACKET_SIZE] = big_endian_packet[index];
    }

    result = icm45686_fifo_parse_accel_gyro(
        packets,
        sizeof(packets),
        ICM45686_FIFO_BIG_ENDIAN,
        samples,
        2U);

    return expect(result.status == ICM45686_FIFO_PARSE_OK,
                  "two complete packets must parse successfully")
        && expect(result.bytes_consumed == sizeof(packets),
                  "two complete packets must consume all bytes")
        && expect(result.samples_produced == 2U,
                  "two complete packets must produce two samples")
        && expect_sample(&samples[0])
        && expect_sample(&samples[1]);
}

static int test_invalid_arguments(void)
{
    icm45686_fifo_sample_t sample = {0};
    icm45686_fifo_parse_result_t result;

    result = icm45686_fifo_parse_accel_gyro(
        NULL,
        0U,
        ICM45686_FIFO_BIG_ENDIAN,
        &sample,
        1U);
    if (!expect(result.status == ICM45686_FIFO_PARSE_INVALID_ARGUMENT,
                "parser must reject null input"))
    {
        return 0;
    }

    result = icm45686_fifo_parse_accel_gyro(
        big_endian_packet,
        sizeof(big_endian_packet),
        (icm45686_fifo_endian_t)99,
        &sample,
        1U);
    if (!expect(result.status == ICM45686_FIFO_PARSE_INVALID_ARGUMENT,
                "parser must reject an unknown byte order"))
    {
        return 0;
    }

    result = icm45686_fifo_parse_accel_gyro(
        big_endian_packet,
        sizeof(big_endian_packet),
        ICM45686_FIFO_BIG_ENDIAN,
        NULL,
        1U);
    return expect(result.status == ICM45686_FIFO_PARSE_INVALID_ARGUMENT,
                  "parser must reject null output");
}

int main(void)
{
    if (!test_packet(big_endian_packet, ICM45686_FIFO_BIG_ENDIAN)
        || !test_packet(little_endian_packet, ICM45686_FIFO_LITTLE_ENDIAN)
        || !test_partial_packet() || !test_invalid_header()
        || !test_output_full() || !test_multiple_packets()
        || !test_invalid_arguments())
    {
        return 1;
    }

    puts("icm45686 fifo: PASS");
    return 0;
}
