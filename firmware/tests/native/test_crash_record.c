#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "crash_record.h"

static unsigned int test_count;

static const uint8_t CRASH_RECORD_V1_GOLDEN_RECORD[128] = {
    UINT8_C(0x43), UINT8_C(0x52), UINT8_C(0x56), UINT8_C(0x31),
    UINT8_C(0x01), UINT8_C(0x00), UINT8_C(0x20), UINT8_C(0x00),
    UINT8_C(0x80), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0x07), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0x03), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0x05), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0xF9), UINT8_C(0xFF), UINT8_C(0xFF), UINT8_C(0xFF),
    UINT8_C(0x00), UINT8_C(0x10), UINT8_C(0x00), UINT8_C(0x24),
    UINT8_C(0x01), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0x02), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0x03), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0x04), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0x0C), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0x56), UINT8_C(0x34), UINT8_C(0x02), UINT8_C(0x08),
    UINT8_C(0x67), UINT8_C(0x45), UINT8_C(0x02), UINT8_C(0x08),
    UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x61),
    UINT8_C(0x00), UINT8_C(0x82), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x40),
    UINT8_C(0x08), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
    UINT8_C(0xAA), UINT8_C(0xAA), UINT8_C(0x00), UINT8_C(0x24),
    UINT8_C(0xBB), UINT8_C(0xBB), UINT8_C(0x00), UINT8_C(0x24),
    UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x01),
    UINT8_C(0xC0), UINT8_C(0xC1), UINT8_C(0xC2), UINT8_C(0xC3),
    UINT8_C(0xC4), UINT8_C(0xC5), UINT8_C(0xC6), UINT8_C(0xC7),
    UINT8_C(0xC8), UINT8_C(0xC9), UINT8_C(0xCA), UINT8_C(0xCB),
    UINT8_C(0xCC), UINT8_C(0xCD), UINT8_C(0xCE), UINT8_C(0xCF),
    UINT8_C(0xD0), UINT8_C(0xD1), UINT8_C(0xD2), UINT8_C(0xD3),
    UINT8_C(0xD4), UINT8_C(0xD5), UINT8_C(0xD6), UINT8_C(0xD7),
    UINT8_C(0xD8), UINT8_C(0xD9), UINT8_C(0xDA), UINT8_C(0xDB),
    UINT8_C(0xDC), UINT8_C(0xDD), UINT8_C(0xDE), UINT8_C(0xDF),
    UINT8_C(0x40), UINT8_C(0x6E), UINT8_C(0xC5), UINT8_C(0x9A),
    UINT8_C(0x3C), UINT8_C(0x5A), UINT8_C(0xC3), UINT8_C(0xA5),
};

static int expect(int condition, const char *message)
{
    test_count++;
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static uint16_t get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
           | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U)
           | ((uint32_t)data[3] << 24U);
}

static void put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t independent_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t index;

    for (index = 0U; index < length; index++)
    {
        uint32_t bit;

        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (crc >> 1U)
                  ^ ((crc & 1U) != 0U ? UINT32_C(0xEDB88320) : 0U);
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

static void make_valid_slot(uint8_t *slot)
{
    uint32_t index;

    memset(slot, 0xA5, CRASH_RECORD_V1_SLOT_SIZE_BYTES);
    memset(slot, 0, CRASH_RECORD_V1_RECORD_SIZE_BYTES);
    put_u32_le(&slot[0], UINT32_C(0x31565243));
    put_u16_le(&slot[4], 1U);
    put_u16_le(&slot[6], 32U);
    put_u32_le(&slot[8], 128U);
    put_u32_le(&slot[12], 7U);
    put_u32_le(&slot[16], 3U);
    put_u32_le(&slot[20], UINT32_C(0x00000005));
    put_u32_le(&slot[24], UINT32_C(0xFFFFFFF9));
    put_u32_le(&slot[28], UINT32_C(0x24001000));
    put_u32_le(&slot[32], UINT32_C(0x00000001));
    put_u32_le(&slot[36], UINT32_C(0x00000002));
    put_u32_le(&slot[40], UINT32_C(0x00000003));
    put_u32_le(&slot[44], UINT32_C(0x00000004));
    put_u32_le(&slot[48], UINT32_C(0x0000000C));
    put_u32_le(&slot[52], UINT32_C(0x08023456));
    put_u32_le(&slot[56], UINT32_C(0x08024567));
    put_u32_le(&slot[60], UINT32_C(0x61000000));
    put_u32_le(&slot[64], UINT32_C(0x00008200));
    put_u32_le(&slot[68], UINT32_C(0x40000000));
    put_u32_le(&slot[72], UINT32_C(0x00000008));
    put_u32_le(&slot[76], UINT32_C(0x2400AAAA));
    put_u32_le(&slot[80], UINT32_C(0x2400BBBB));
    put_u32_le(&slot[84], UINT32_C(0x01000000));
    for (index = 0U; index < 32U; index++)
    {
        slot[88U + index] = (uint8_t)(0xC0U + index);
    }
    put_u32_le(&slot[120], independent_crc32(slot, 120U));
    put_u32_le(&slot[124], UINT32_C(0xA5C35A3C));
    put_u32_le(&slot[128], 7U);
}

static void make_valid_slot_with_sequence(uint8_t *slot,
                                           uint32_t sequence,
                                           uint32_t ack_marker)
{
    make_valid_slot(slot);
    put_u32_le(&slot[12], sequence);
    put_u32_le(&slot[120], independent_crc32(slot, 120U));
    put_u32_le(&slot[128], ack_marker);
}

static void make_record_template(crash_record_v1_t *record)
{
    uint32_t index;

    memset(record, 0, sizeof(*record));
    record->fault_kind = CRASH_RECORD_FAULT_HARDFAULT;
    record->capture_flags = 2U;
    record->exc_return = UINT32_C(0xFFFFFFF9);
    record->sp = UINT32_C(0x24001000);
    record->r0 = 1U;
    record->r1 = 2U;
    record->r2 = 3U;
    record->r3 = 4U;
    record->r12 = 12U;
    record->lr = UINT32_C(0x08023456);
    record->pc = UINT32_C(0x08024567);
    record->xpsr = UINT32_C(0x61000000);
    record->cfsr = UINT32_C(0x00008200);
    record->hfsr = UINT32_C(0x40000000);
    record->shcsr = UINT32_C(0x00000008);
    record->mmfar = UINT32_C(0x2400AAAA);
    record->bfar = UINT32_C(0x2400BBBB);
    record->reset_flags = UINT32_C(0x01000000);
    for (index = 0U; index < 32U; index++)
    {
        record->build_id[index] = (uint8_t)(0xC0U + index);
    }
}

typedef struct
{
    uint8_t slot_index;
    uint16_t offset;
    uint32_t value;
    uint8_t width;
} write_trace_entry_t;

typedef struct
{
    write_trace_entry_t entries[128];
    size_t count;
} write_trace_t;

static void trace_write(void *context,
                        uint8_t slot_index,
                        uint16_t offset,
                        uint32_t value,
                        uint8_t width)
{
    write_trace_t *trace = context;

    if (trace->count < sizeof(trace->entries) / sizeof(trace->entries[0]))
    {
        trace->entries[trace->count].slot_index = slot_index;
        trace->entries[trace->count].offset = offset;
        trace->entries[trace->count].value = value;
        trace->entries[trace->count].width = width;
        trace->count++;
    }
}

static void trace_barrier(void *context)
{
    trace_write(context, 0xFFU, 0xFFFFU, 0U, 0U);
}

static int test_fixed_contract(void)
{
    if (!expect(sizeof(crash_record_v1_t) == 128U,
                "CrashRecord v1 record must be 128 bytes")
        || !expect(CRASH_RECORD_V1_SLOT_SIZE_BYTES == 256U,
                   "CrashRecord slot must be 256 bytes")
        || !expect(CRASH_RECORD_V1_ACK_OFFSET == 128U,
                   "ACK marker must follow the 128-byte record")
        || !expect(CRASH_RECORD_V1_MAGIC == UINT32_C(0x31565243),
                   "magic literal must remain frozen")
        || !expect(CRASH_RECORD_V1_FORMAT_VERSION == 1U,
                   "format version literal must remain frozen")
        || !expect(CRASH_RECORD_V1_COMMIT_INVALID == 0U,
                   "invalid commit literal must remain frozen")
        || !expect(CRASH_RECORD_V1_COMMIT_VALID == UINT32_C(0xA5C35A3C),
                   "valid commit literal must remain frozen"))
    {
        return 0;
    }

    return expect(offsetof(crash_record_v1_t, magic) == 0U,
                  "magic offset")
           && expect(offsetof(crash_record_v1_t, format_version) == 4U,
                     "version offset")
           && expect(offsetof(crash_record_v1_t, header_length) == 6U,
                     "header length offset")
           && expect(offsetof(crash_record_v1_t, record_length) == 8U,
                     "record length offset")
           && expect(offsetof(crash_record_v1_t, sequence) == 12U,
                     "sequence offset")
           && expect(offsetof(crash_record_v1_t, fault_kind) == 16U,
                     "fault kind offset")
           && expect(offsetof(crash_record_v1_t, capture_flags) == 20U,
                     "capture flags offset")
           && expect(offsetof(crash_record_v1_t, exc_return) == 24U,
                     "EXC_RETURN offset")
           && expect(offsetof(crash_record_v1_t, sp) == 28U,
                     "SP offset")
           && expect(offsetof(crash_record_v1_t, r0) == 32U,
                     "r0 offset")
           && expect(offsetof(crash_record_v1_t, pc) == 56U,
                     "PC offset")
           && expect(offsetof(crash_record_v1_t, cfsr) == 64U,
                     "CFSR offset")
           && expect(offsetof(crash_record_v1_t, reset_flags) == 84U,
                     "reset flags offset")
           && expect(offsetof(crash_record_v1_t, build_id) == 88U,
                     "build ID offset")
           && expect(offsetof(crash_record_v1_t, crc32) == 120U,
                     "CRC offset")
           && expect(offsetof(crash_record_v1_t, commit_marker) == 124U,
                     "commit offset");
}

static int test_golden_decode_and_crc(void)
{
    uint8_t slot[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    crash_record_v1_t record;
    uint32_t index;

    make_valid_slot(slot);
    for (index = 0U; index < sizeof(CRASH_RECORD_V1_GOLDEN_RECORD);
         index++)
    {
        if (!expect(slot[index] == CRASH_RECORD_V1_GOLDEN_RECORD[index],
                    "complete v1 golden record bytes"))
        {
            return 0;
        }
    }
    if (!expect(get_u32_le(&slot[0]) == UINT32_C(0x31565243),
                "golden magic bytes")
        || !expect(get_u16_le(&slot[4]) == 1U,
                   "golden version bytes")
        || !expect(get_u16_le(&slot[6]) == 32U,
                   "golden header length bytes")
        || !expect(get_u32_le(&slot[8]) == 128U,
                   "golden record length bytes")
        || !expect(get_u32_le(&slot[120]) == independent_crc32(slot, 120U),
                   "CRC must cover bytes 0 through 119")
        || !expect(get_u32_le(&slot[124]) == UINT32_C(0xA5C35A3C),
                   "golden commit bytes")
        || !expect(get_u32_le(&slot[128]) == 7U,
                   "ACK is outside the record CRC range")
        || !expect(crash_record_v1_validate(slot, &record),
                   "golden record must validate"))
    {
        return 0;
    }

    return expect(record.sequence == 7U, "decoded sequence")
           && expect(record.fault_kind == 3U, "decoded fault kind")
           && expect(record.capture_flags == 5U,
                     "decoded capture flags")
           && expect(record.pc == UINT32_C(0x08024567), "decoded PC")
           && expect(record.reset_flags == UINT32_C(0x01000000),
                     "decoded reset flags")
           && expect(record.build_id[0] == UINT8_C(0xC0)
                          && record.build_id[31] == UINT8_C(0xDF),
                      "decoded build ID")
           && expect(record.crc32 == get_u32_le(&slot[120]),
                     "decoded CRC");
}

static int expect_invalid_after_change(uint8_t *slot,
                                       size_t offset,
                                       uint32_t value,
                                       const char *message)
{
    crash_record_v1_t record;

    put_u32_le(&slot[offset], value);
    return expect(!crash_record_v1_validate(slot, &record), message);
}

static int test_rejection_matrix(void)
{
    uint8_t slot[CRASH_RECORD_V1_SLOT_SIZE_BYTES];

    make_valid_slot(slot);
    if (!expect_invalid_after_change(slot, 0U, UINT32_C(0xDEADBEEF),
                                     "bad magic must reject"))
    {
        return 0;
    }
    make_valid_slot(slot);
    if (!expect_invalid_after_change(slot, 4U, 2U,
                                     "unknown version must reject"))
    {
        return 0;
    }
    make_valid_slot(slot);
    if (!expect_invalid_after_change(slot, 6U, 31U,
                                     "bad header length must reject"))
    {
        return 0;
    }
    make_valid_slot(slot);
    if (!expect_invalid_after_change(slot, 8U, 129U,
                                     "bad record length must reject"))
    {
        return 0;
    }
    make_valid_slot(slot);
    if (!expect_invalid_after_change(slot, 12U, 0U,
                                     "zero sequence must reject"))
    {
        return 0;
    }
    make_valid_slot(slot);
    if (!expect_invalid_after_change(slot, 16U, 0U,
                                     "zero fault kind must reject"))
    {
        return 0;
    }
    make_valid_slot(slot);
    if (!expect_invalid_after_change(slot, 16U, 99U,
                                     "unknown fault kind must reject"))
    {
        return 0;
    }
    make_valid_slot(slot);
    if (!expect_invalid_after_change(slot, 124U, 0U,
                                     "missing commit must reject"))
    {
        return 0;
    }

    make_valid_slot(slot);
    slot[32] ^= 1U;
    if (!expect(!crash_record_v1_validate(slot, NULL),
                "CRC corruption must reject"))
    {
        return 0;
    }

    make_valid_slot(slot);
    put_u32_le(&slot[124], CRASH_RECORD_V1_COMMIT_INVALID);
    return expect(!crash_record_v1_validate(slot, NULL),
                  "torn record with old commit invalidated must reject");
}

static int test_encode_round_trip(void)
{
    uint8_t slot[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    crash_record_v1_t record;
    uint32_t index;

    memset(&record, 0, sizeof(record));
    record.magic = CRASH_RECORD_V1_MAGIC;
    record.format_version = CRASH_RECORD_V1_FORMAT_VERSION;
    record.header_length = CRASH_RECORD_V1_HEADER_LENGTH_BYTES;
    record.record_length = CRASH_RECORD_V1_RECORD_SIZE_BYTES;
    record.sequence = 7U;
    record.fault_kind = CRASH_RECORD_FAULT_BUSFAULT;
    record.capture_flags = 5U;
    record.exc_return = UINT32_C(0xFFFFFFF9);
    record.sp = UINT32_C(0x24001000);
    record.pc = UINT32_C(0x08024567);
    for (index = 0U; index < 32U; index++)
    {
        record.build_id[index] = (uint8_t)(0xC0U + index);
    }
    record.crc32 = UINT32_C(0x12345678);
    record.commit_marker = CRASH_RECORD_V1_COMMIT_VALID;

    memset(slot, 0, sizeof(slot));
    crash_record_v1_encode(&record, slot);
    return expect(get_u32_le(&slot[0]) == CRASH_RECORD_V1_MAGIC,
                  "encode magic")
           && expect(get_u16_le(&slot[4])
                          == CRASH_RECORD_V1_FORMAT_VERSION,
                      "encode version")
           && expect(get_u32_le(&slot[12]) == 7U, "encode sequence")
           && expect(get_u32_le(&slot[16]) == CRASH_RECORD_FAULT_BUSFAULT,
                      "encode fault kind")
           && expect(get_u32_le(&slot[56]) == UINT32_C(0x08024567),
                      "encode PC")
           && expect(slot[88] == UINT8_C(0xC0)
                          && slot[119] == UINT8_C(0xDF),
                      "encode build ID")
           && expect(get_u32_le(&slot[120]) == UINT32_C(0x12345678),
                      "encode CRC")
           && expect(get_u32_le(&slot[124]) == CRASH_RECORD_V1_COMMIT_VALID,
                      "encode commit");
}

static int test_recovery_selection(void)
{
    _Alignas(4) uint8_t slot_a[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    _Alignas(4) uint8_t slot_b[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    crash_record_storage_t storage = {0};
    crash_record_v1_t record;
    uint32_t ack_marker = 0U;
    uint8_t slot_index = 0xFFU;

    storage.slot_a = slot_a;
    storage.slot_b = slot_b;

    memset(slot_a, 0, sizeof(slot_a));
    memset(slot_b, 0, sizeof(slot_b));
    make_valid_slot_with_sequence(slot_a, 9U, 0U);
    if (!expect(crash_record_recover(&storage, &record, &slot_index,
                                     &ack_marker)
                    == CRASH_RECORD_STATUS_OK
                    && slot_index == 0U
                    && record.sequence == 9U
                    && ack_marker == 0U,
                "one valid slot must recover"))
    {
        return 0;
    }

    make_valid_slot_with_sequence(slot_b, 9U, 9U);
    if (!expect(crash_record_recover(&storage, &record, &slot_index,
                                     &ack_marker)
                    == CRASH_RECORD_STATUS_OK
                    && slot_index == 0U,
                "equal sequences must choose slot A"))
    {
        return 0;
    }

    make_valid_slot_with_sequence(slot_a, 10U, 0U);
    make_valid_slot_with_sequence(slot_b, 11U, 0U);
    if (!expect(crash_record_recover(&storage, &record, &slot_index,
                                     &ack_marker)
                    == CRASH_RECORD_STATUS_OK
                    && slot_index == 1U
                    && record.sequence == 11U,
                "ordinary sequence order must choose newest"))
    {
        return 0;
    }

    make_valid_slot_with_sequence(slot_a, UINT32_MAX, 0U);
    make_valid_slot_with_sequence(slot_b, 1U, 0U);
    if (!expect(crash_record_recover(&storage, &record, &slot_index,
                                     &ack_marker)
                    == CRASH_RECORD_STATUS_OK
                    && slot_index == 1U
                    && record.sequence == 1U,
                "sequence wrap must choose one as newer than max"))
    {
        return 0;
    }

    make_valid_slot_with_sequence(slot_a, 1U, 0U);
    make_valid_slot_with_sequence(slot_b, UINT32_C(0x80000001), 0U);
    if (!expect(crash_record_recover(&storage, &record, &slot_index,
                                     &ack_marker)
                    == CRASH_RECORD_STATUS_SEQUENCE_AMBIGUOUS,
                "half-range sequence must be ambiguous"))
    {
        return 0;
    }

    memset(slot_a, 0, sizeof(slot_a));
    memset(slot_b, 0, sizeof(slot_b));
    return expect(crash_record_recover(&storage, &record, &slot_index,
                                       &ack_marker)
                       == CRASH_RECORD_STATUS_NO_VALID,
                   "two invalid slots must recover no record");
}

static int test_write_requires_barrier(void)
{
    _Alignas(4) uint8_t slot_a[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    _Alignas(4) uint8_t slot_b[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    uint8_t before_a[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    uint8_t before_b[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    crash_record_storage_t storage = {0};
    crash_record_v1_t template;

    memset(slot_a, 0, sizeof(slot_a));
    memset(slot_b, 0, sizeof(slot_b));
    memcpy(before_a, slot_a, sizeof(before_a));
    memcpy(before_b, slot_b, sizeof(before_b));
    storage.slot_a = slot_a;
    storage.slot_b = slot_b;
    make_record_template(&template);
    return expect(crash_record_write_next(&storage, &template, NULL, NULL)
                       == CRASH_RECORD_STATUS_INVALID_ARGUMENT
                       && memcmp(slot_a, before_a, sizeof(slot_a)) == 0
                       && memcmp(slot_b, before_b, sizeof(slot_b)) == 0,
                  "write transaction must require a barrier primitive");
}

static int trace_has_write(const write_trace_t *trace,
                           size_t index,
                           uint8_t slot_index,
                           uint16_t offset,
                           uint32_t value,
                           uint8_t width)
{
    return index < trace->count
           && trace->entries[index].slot_index == slot_index
           && trace->entries[index].offset == offset
           && trace->entries[index].value == value
           && trace->entries[index].width == width;
}

static int test_target_priority_and_transaction(void)
{
    _Alignas(4) uint8_t slot_a[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    _Alignas(4) uint8_t slot_b[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    crash_record_storage_t storage = {0};
    crash_record_v1_t template;
    crash_record_v1_t written;
    write_trace_t trace = {0};
    uint8_t target = 0xFFU;
    size_t barrier_index = 0U;
    size_t index;
    int found_barrier = 0;

    storage.slot_a = slot_a;
    storage.slot_b = slot_b;
    storage.barrier = trace_barrier;
    storage.barrier_context = &trace;
    storage.write_hook = trace_write;
    storage.write_hook_context = &trace;
    make_record_template(&template);

    make_valid_slot_with_sequence(slot_a, 20U, 20U);
    memset(slot_b, 0, sizeof(slot_b));
    if (!expect(crash_record_write_next(&storage, &template, &written, &target)
                    == CRASH_RECORD_STATUS_OK
                    && target == 1U
                    && written.sequence == 21U,
                "invalid slot must have target priority"))
    {
        return 0;
    }

    make_valid_slot_with_sequence(slot_a, 20U, 20U);
    make_valid_slot_with_sequence(slot_b, 21U, 0U);
    trace.count = 0U;
    if (!expect(crash_record_write_next(&storage, &template, &written, &target)
                    == CRASH_RECORD_STATUS_OK
                    && target == 0U
                    && written.sequence == 22U,
                "oldest ACKed non-newest slot must be selected"))
    {
        return 0;
    }

    if (!expect(trace_has_write(&trace, 0U, 0U, 124U,
                                CRASH_RECORD_V1_COMMIT_INVALID, 4U)
                    && trace_has_write(&trace, 1U, 0U,
                                       CRASH_RECORD_V1_ACK_OFFSET,
                                       CRASH_RECORD_V1_ACK_UNACKED, 4U),
                "transaction must invalidate commit before ACK reset"))
    {
        return 0;
    }

    for (index = 0U; index < trace.count; index++)
    {
        if (trace.entries[index].offset == 0xFFFFU)
        {
            barrier_index = index;
            found_barrier = 1;
            break;
        }
    }
    if (!expect(found_barrier, "transaction must execute supplied barrier")
        || !expect(barrier_index > 0U
                       && trace.entries[barrier_index - 1U].offset == 120U
                       && trace.entries[barrier_index - 1U].width == 4U,
                   "CRC must be written immediately before barrier")
        || !expect(barrier_index + 1U < trace.count
                       && trace.entries[barrier_index + 1U].offset == 124U
                       && trace.entries[barrier_index + 1U].value
                              == CRASH_RECORD_V1_COMMIT_VALID,
                   "valid commit must be written after barrier"))
    {
        return 0;
    }

    if (!expect(crash_record_v1_validate(slot_a, &written),
                "committed target must validate after transaction"))
    {
        return 0;
    }
    return expect(get_u32_le(&slot_a[128]) == CRASH_RECORD_V1_ACK_UNACKED,
                  "new transaction must reset target ACK");
}

static int test_sequence_and_ack(void)
{
    _Alignas(4) uint8_t slot_a[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    _Alignas(4) uint8_t slot_b[CRASH_RECORD_V1_SLOT_SIZE_BYTES];
    uint8_t record_before[CRASH_RECORD_V1_RECORD_SIZE_BYTES];
    crash_record_storage_t storage = {0};
    crash_record_v1_t template;
    crash_record_v1_t written;
    write_trace_t trace = {0};
    uint8_t target = 0xFFU;

    storage.slot_a = slot_a;
    storage.slot_b = slot_b;
    storage.barrier = trace_barrier;
    storage.barrier_context = &trace;
    storage.write_hook = trace_write;
    storage.write_hook_context = &trace;
    make_record_template(&template);

    memset(slot_a, 0, sizeof(slot_a));
    memset(slot_b, 0, sizeof(slot_b));
    if (!expect(crash_record_write_next(&storage, &template, &written, &target)
                    == CRASH_RECORD_STATUS_OK
                    && written.sequence == 1U
                    && written.sequence != 0U,
                "first record sequence must start at one"))
    {
        return 0;
    }

    make_valid_slot_with_sequence(slot_a, UINT32_MAX, 0U);
    memset(slot_b, 0, sizeof(slot_b));
    if (!expect(crash_record_write_next(&storage, &template, &written, &target)
                    == CRASH_RECORD_STATUS_OK
                    && written.sequence == 1U,
                "sequence increment must skip zero"))
    {
        return 0;
    }

    make_valid_slot_with_sequence(slot_a, 7U, 0U);
    memset(slot_b, 0, sizeof(slot_b));
    memcpy(record_before, slot_a, sizeof(record_before));
    trace.count = 0U;
    if (!expect(crash_record_ack(&storage, 7U) == CRASH_RECORD_STATUS_OK,
                "matching sequence must ACK"))
    {
        return 0;
    }
    if (!expect(get_u32_le(&slot_a[128]) == 7U
                    && memcmp(record_before, slot_a, sizeof(record_before))
                           == 0
                    && trace.count == 2U
                    && trace_has_write(&trace, 0U, 0U,
                                       CRASH_RECORD_V1_ACK_OFFSET, 7U, 4U)
                    && trace.entries[1U].offset == 0xFFFFU,
                "ACK must write the independent marker then persist it"))
    {
        return 0;
    }

    trace.count = 0U;
    if (!expect(crash_record_ack(&storage, 7U) == CRASH_RECORD_STATUS_OK
                    && trace.count == 0U,
                "duplicate ACK must be idempotent"))
    {
        return 0;
    }

    return expect(crash_record_ack(&storage, 6U)
                       == CRASH_RECORD_STATUS_STALE
                       && trace.count == 0U
                       && get_u32_le(&slot_a[128]) == 7U,
                   "stale ACK must not change the slot");
}

int main(void)
{
    if (!test_fixed_contract()
        || !test_golden_decode_and_crc()
        || !test_rejection_matrix()
        || !test_encode_round_trip()
        || !test_recovery_selection()
        || !test_write_requires_barrier()
        || !test_target_priority_and_transaction()
        || !test_sequence_and_ack())
    {
        return 1;
    }

    printf("crash record: PASS (%u checks)\n", test_count);
    return 0;
}
