#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ai_result_sidecar.h"
#include "ota_crc32.h"

#define TEST_STORAGE_BYTES 12288U
#define TEST_MAX_WRITE_CALLS 512U
#define EXPECTED_AI_RESULT_SIDECAR_RECORD_BYTES 128U
#define EXPECTED_AI_RESULT_SIDECAR_HEADER_BYTES 60U
#define EXPECTED_AI_RESULT_SIDECAR_FORMAT_VERSION 2U
#define EXPECTED_AI_RESULT_SIDECAR_CRC_OFFSET 60U
#define EXPECTED_AI_RESULT_SIDECAR_COMMIT_OFFSET 64U
#define EXPECTED_AI_RESULT_SIDECAR_COMMIT_BYTES 4U
#define EXPECTED_AI_RESULT_SIDECAR_COMMIT_MARKER UINT32_C(0)
#define EXPECTED_AI_RESULT_SIDECAR_RESERVED_OFFSET 58U
#define EXPECTED_AI_RESULT_SIDECAR_RESERVED_BYTES 2U
#define EXPECTED_AI_RESULT_SIDECAR_LOGIT_COUNT 4U

typedef struct
{
    uint32_t offset;
    uint32_t length;
} test_storage_write_call_t;

typedef struct
{
    uint8_t bytes[TEST_STORAGE_BYTES];
    uint32_t write_fail_after;
    uint32_t written_bytes;
    uint32_t erase_count;
    uint32_t last_erase_offset;
    uint32_t last_erase_length;
    uint8_t fail_after_marker_once;
    uint8_t marker_failure_triggered;
    uint32_t write_call_count;
    test_storage_write_call_t write_calls[TEST_MAX_WRITE_CALLS];
} test_storage_t;

static int storage_read(void *context, uint32_t offset, uint8_t *data,
                        uint32_t length)
{
    test_storage_t *storage = context;

    if (storage == NULL || data == NULL || offset > TEST_STORAGE_BYTES
        || length > TEST_STORAGE_BYTES - offset)
    {
        return -1;
    }
    memcpy(data, &storage->bytes[offset], length);
    return 0;
}

static int storage_write(void *context, uint32_t offset, const uint8_t *data,
                         uint32_t length)
{
    test_storage_t *storage = context;
    uint32_t index;

    if (storage == NULL || data == NULL || offset > TEST_STORAGE_BYTES
        || length > TEST_STORAGE_BYTES - offset)
    {
        return -1;
    }
    if (storage->write_call_count >= TEST_MAX_WRITE_CALLS)
    {
        return -1;
    }
    storage->write_calls[storage->write_call_count].offset = offset;
    storage->write_calls[storage->write_call_count].length = length;
    storage->write_call_count++;
    for (index = 0U; index < length; index++)
    {
        if (storage->write_fail_after != 0U
            && storage->written_bytes >= storage->write_fail_after)
        {
            return -1;
        }
        if ((storage->bytes[offset + index] & data[index]) != data[index])
        {
            return -1;
        }
        storage->bytes[offset + index] &= data[index];
        storage->written_bytes++;
    }
    if (storage->fail_after_marker_once && length == sizeof(uint32_t)
        && !storage->marker_failure_triggered)
    {
        storage->marker_failure_triggered = 1U;
        return -1;
    }
    return 0;
}

static int storage_erase(void *context, uint32_t offset, uint32_t length)
{
    test_storage_t *storage = context;

    if (storage == NULL || offset > TEST_STORAGE_BYTES
        || length > TEST_STORAGE_BYTES - offset || length == 0U)
    {
        return -1;
    }
    memset(&storage->bytes[offset], 0xFF, length);
    storage->erase_count++;
    storage->last_erase_offset = offset;
    storage->last_erase_length = length;
    return 0;
}

static ai_result_sidecar_storage_t make_storage(test_storage_t *storage)
{
    return (ai_result_sidecar_storage_t){
        .read = storage_read,
        .write = storage_write,
        .erase = storage_erase,
        .context = storage,
        .capacity_bytes = TEST_STORAGE_BYTES,
        .erase_sector_bytes = 4096U,
    };
}

static ai_result_t make_result(uint32_t event_id, uint8_t class_index)
{
    return (ai_result_t){
        .event_id = event_id,
        .model_version = 2U,
        .status = AI_RESULT_STATUS_PREDICTION,
        .class_index = class_index,
        .class_count = 2U,
        .quality_flags = AI_RESULT_QUALITY_EVENT_FLAGS_VALID
                         | AI_RESULT_QUALITY_SAMPLE_COUNT_VALID,
        .event_flags = 0U,
        .sample_count = 2400U,
        .model_crc32 = UINT32_C(0xAABBCCDD),
        .confidence = class_index == 0U ? 0.75F : 0.875F,
        .logits = {class_index == 0U ? 1.0F : -1.0F,
                   class_index == 0U ? -1.0F : 1.0F},
        .failure_reason = AI_RESULT_FAILURE_NONE,
    };
}

static uint16_t get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8U);
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
           | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U)
           | ((uint32_t)data[3] << 24U);
}

static float get_float_le(const uint8_t *data)
{
    uint32_t bits = get_u32_le(data);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void test_v2_record_layout_and_remount_readback(void)
{
    test_storage_t storage = {0};
    ai_result_sidecar_storage_t io;
    ai_result_sidecar_t sidecar;
    ai_result_sidecar_t remounted;
    ai_result_t expected = make_result(42U, 1U);
    ai_result_t actual = {0};
    uint8_t record[EXPECTED_AI_RESULT_SIDECAR_RECORD_BYTES];
    uint32_t index;

    memset(storage.bytes, 0xFF, sizeof(storage.bytes));
    io = make_storage(&storage);
    assert(ai_result_sidecar_mount(&sidecar, &io) == AI_RESULT_SIDECAR_OK);
    assert(ai_result_sidecar_append(&sidecar, &expected)
           == AI_RESULT_SIDECAR_OK);
    assert(storage_read(&storage, 0U, record, sizeof(record)) == 0);
    assert(storage.write_call_count == 2U);
    assert(storage.write_calls[0].offset == 0U);
    assert(storage.write_calls[0].length
           == EXPECTED_AI_RESULT_SIDECAR_COMMIT_OFFSET);
    assert(storage.write_calls[1].offset
           == EXPECTED_AI_RESULT_SIDECAR_COMMIT_OFFSET);
    assert(storage.write_calls[1].length
           == EXPECTED_AI_RESULT_SIDECAR_COMMIT_BYTES);

    assert(get_u32_le(&record[0]) == UINT32_C(0x53524941));
    assert(get_u16_le(&record[4])
           == EXPECTED_AI_RESULT_SIDECAR_FORMAT_VERSION);
    assert(get_u16_le(&record[6])
           == EXPECTED_AI_RESULT_SIDECAR_HEADER_BYTES);
    assert(get_u32_le(&record[8]) == 1U);
    assert(get_u32_le(&record[12]) == expected.event_id);
    assert(get_u16_le(&record[16]) == expected.model_version);
    assert(record[18] == expected.status);
    assert(record[19] == expected.class_index);
    assert(record[20] == expected.class_count);
    assert(record[21] == expected.quality_flags);
    assert(get_u16_le(&record[22]) == expected.event_flags);
    assert(get_u32_le(&record[24]) == expected.sample_count);
    assert(get_u32_le(&record[28]) == expected.model_crc32);
    assert(get_float_le(&record[32]) == expected.confidence);
    assert(get_float_le(&record[36]) == expected.logits[0]);
    assert(get_float_le(&record[40]) == expected.logits[1]);
    assert(get_float_le(&record[44]) == expected.logits[2]);
    assert(get_float_le(&record[48]) == expected.logits[3]);
    assert(get_u16_le(&record[52]) == expected.failure_reason);
    assert(get_u32_le(&record[54]) == 1U);
    for (index = EXPECTED_AI_RESULT_SIDECAR_RESERVED_OFFSET;
         index < EXPECTED_AI_RESULT_SIDECAR_RESERVED_OFFSET
                 + EXPECTED_AI_RESULT_SIDECAR_RESERVED_BYTES;
         index++)
    {
        assert(record[index] == 0xFFU);
    }
    assert(get_u32_le(&record[EXPECTED_AI_RESULT_SIDECAR_CRC_OFFSET])
           == ota_crc32_compute(record, EXPECTED_AI_RESULT_SIDECAR_HEADER_BYTES));
    assert(get_u32_le(&record[EXPECTED_AI_RESULT_SIDECAR_COMMIT_OFFSET])
           == EXPECTED_AI_RESULT_SIDECAR_COMMIT_MARKER);
    for (index = 68U; index < sizeof(record); index++)
    {
        assert(record[index] == 0xFFU);
    }

    assert(ai_result_sidecar_mount(&remounted, &io)
           == AI_RESULT_SIDECAR_OK);
    assert(ai_result_sidecar_get_event(&remounted, expected.event_id, &actual)
           == AI_RESULT_SIDECAR_OK);
    assert(actual.event_id == expected.event_id);
    assert(actual.result_sequence == 1U);
    assert(actual.model_version == expected.model_version);
    assert(actual.status == expected.status);
    assert(actual.class_index == expected.class_index);
    assert(actual.class_count == expected.class_count);
    assert(actual.quality_flags == expected.quality_flags);
    assert(actual.event_flags == expected.event_flags);
    assert(actual.sample_count == expected.sample_count);
    assert(actual.model_crc32 == expected.model_crc32);
    assert(actual.confidence == expected.confidence);
    for (index = 0U; index < EXPECTED_AI_RESULT_SIDECAR_LOGIT_COUNT; index++)
    {
        assert(actual.logits[index] == expected.logits[index]);
    }
    assert(actual.failure_reason == expected.failure_reason);
}

static void test_v2_reader_rejects_crc_and_commit_corruption(void)
{
    test_storage_t storage = {0};
    ai_result_sidecar_storage_t io;
    ai_result_sidecar_t sidecar;
    ai_result_t result = make_result(43U, 0U);
    ai_result_t actual = {0};

    memset(storage.bytes, 0xFF, sizeof(storage.bytes));
    io = make_storage(&storage);
    assert(ai_result_sidecar_mount(&sidecar, &io) == AI_RESULT_SIDECAR_OK);
    assert(ai_result_sidecar_append(&sidecar, &result) == AI_RESULT_SIDECAR_OK);

    storage.bytes[60] ^= 0x01U;
    assert(ai_result_sidecar_mount(&sidecar, &io) == AI_RESULT_SIDECAR_OK);
    assert(ai_result_sidecar_get_event(&sidecar, result.event_id, &actual)
           == AI_RESULT_SIDECAR_NOT_FOUND);

    memset(storage.bytes, 0xFF, sizeof(storage.bytes));
    assert(ai_result_sidecar_mount(&sidecar, &io) == AI_RESULT_SIDECAR_OK);
    assert(ai_result_sidecar_append(&sidecar, &result) == AI_RESULT_SIDECAR_OK);
    storage.bytes[64] = 0xFFU;
    assert(ai_result_sidecar_mount(&sidecar, &io) == AI_RESULT_SIDECAR_OK);
    assert(ai_result_sidecar_get_event(&sidecar, result.event_id, &actual)
           == AI_RESULT_SIDECAR_NOT_FOUND);
}

static void test_round_trip_and_event_lookup(void)
{
    test_storage_t storage = {0};
    ai_result_sidecar_storage_t io;
    ai_result_sidecar_t sidecar;
    ai_result_t expected_first = make_result(41U, 0U);
    ai_result_t expected_second = make_result(42U, 1U);
    ai_result_t actual = {0};

    memset(storage.bytes, 0xFF, sizeof(storage.bytes));
    io = make_storage(&storage);
    assert(ai_result_sidecar_mount(&sidecar, &io) == 0);
    assert(ai_result_sidecar_append(&sidecar, &expected_first) == 0);
    assert(ai_result_sidecar_append(&sidecar, &expected_second) == 0);
    assert(ai_result_sidecar_get_latest(&sidecar, &actual) == 0);
    assert(actual.event_id == expected_second.event_id);
    assert(actual.class_index == expected_second.class_index);
    assert(actual.result_sequence == 2U);
    assert(ai_result_sidecar_get_event(&sidecar, expected_first.event_id,
                                       &actual) == 0);
    assert(actual.event_id == expected_first.event_id);
    assert(actual.model_version == expected_first.model_version);
    assert(ai_result_sidecar_get_event(&sidecar, 999U, &actual)
           == AI_RESULT_SIDECAR_NOT_FOUND);
}

static void test_mount_rejects_two_sector_journal(void)
{
    test_storage_t storage = {0};
    ai_result_sidecar_storage_t io;
    ai_result_sidecar_t sidecar;

    memset(storage.bytes, 0xFF, sizeof(storage.bytes));
    io = make_storage(&storage);
    io.capacity_bytes = 8192U;
    assert(ai_result_sidecar_mount(&sidecar, &io)
           == AI_RESULT_SIDECAR_INVALID_ARGUMENT);
}

static void test_mount_skips_torn_record_and_continues(void)
{
    test_storage_t storage = {0};
    ai_result_sidecar_storage_t io;
    ai_result_sidecar_t sidecar;
    ai_result_sidecar_t remounted;
    ai_result_t expected = make_result(7U, 0U);
    ai_result_t replacement = make_result(8U, 1U);
    ai_result_t actual = {0};

    memset(storage.bytes, 0xFF, sizeof(storage.bytes));
    io = make_storage(&storage);
    assert(ai_result_sidecar_mount(&sidecar, &io) == 0);
    assert(ai_result_sidecar_append(&sidecar, &expected) == 0);

    storage.write_fail_after = storage.written_bytes
                               + AI_RESULT_SIDECAR_HEADER_BYTES
                               + sizeof(uint32_t) + 2U;
    assert(ai_result_sidecar_append(&sidecar, &replacement) != 0);
    storage.write_fail_after = 0U;

    assert(ai_result_sidecar_mount(&remounted, &io) == 0);
    assert(ai_result_sidecar_get_latest(&remounted, &actual) == 0);
    assert(actual.event_id == expected.event_id);
    assert(ai_result_sidecar_append(&remounted, &replacement) == 0);
    assert(ai_result_sidecar_get_latest(&remounted, &actual) == 0);
    assert(actual.event_id == replacement.event_id);
    assert(actual.result_sequence == 2U);
}

static void test_full_journal_rotates_one_erase_sector(void)
{
    test_storage_t storage = {0};
    ai_result_sidecar_storage_t io;
    ai_result_sidecar_t sidecar;
    ai_result_t actual = {0};
    uint32_t event_id;
    const uint32_t slot_count = TEST_STORAGE_BYTES / AI_RESULT_SIDECAR_RECORD_BYTES;
    const uint32_t sector_slots = 4096U / AI_RESULT_SIDECAR_RECORD_BYTES;

    memset(storage.bytes, 0xFF, sizeof(storage.bytes));
    io = make_storage(&storage);
    assert(ai_result_sidecar_mount(&sidecar, &io) == 0);
    for (event_id = 1U; event_id <= slot_count + 1U; event_id++)
    {
        ai_result_t result = make_result(event_id, (uint8_t)(event_id & 1U));

        assert(ai_result_sidecar_append(&sidecar, &result) == 0);
    }
    assert(storage.erase_count == 1U);
    assert(storage.last_erase_offset == 0U);
    assert(storage.last_erase_length == 4096U);
    assert(ai_result_sidecar_get_latest(&sidecar, &actual) == 0);
    assert(actual.event_id == slot_count + 1U);

    assert(ai_result_sidecar_mount(&sidecar, &io) == 0);
    for (event_id = slot_count + 2U;
         event_id <= slot_count + sector_slots; event_id++)
    {
        ai_result_t result = make_result(event_id, (uint8_t)(event_id & 1U));

        assert(ai_result_sidecar_append(&sidecar, &result) == 0);
    }
    assert(ai_result_sidecar_mount(&sidecar, &io) == 0);
    {
        ai_result_t result = make_result(slot_count + sector_slots + 1U, 1U);

        assert(ai_result_sidecar_append(&sidecar, &result) == 0);
    }
    assert(storage.erase_count == 2U);
    assert(storage.last_erase_offset == 4096U);
    assert(storage.last_erase_length == 4096U);
    assert(ai_result_sidecar_get_latest(&sidecar, &actual) == 0);
    assert(actual.event_id == slot_count + sector_slots + 1U);
}

static void test_delayed_result_keeps_its_published_sequence(void)
{
    test_storage_t storage = {0};
    ai_result_sidecar_storage_t io;
    ai_result_sidecar_t sidecar;
    ai_result_t early = make_result(101U, 0U);
    ai_result_t late = make_result(102U, 1U);
    ai_result_t actual = {0};

    early.result_sequence = 1U;
    late.result_sequence = 2U;
    memset(storage.bytes, 0xFF, sizeof(storage.bytes));
    io = make_storage(&storage);
    assert(ai_result_sidecar_mount(&sidecar, &io) == 0);
    assert(ai_result_sidecar_append(&sidecar, &late) == 0);
    assert(ai_result_sidecar_append(&sidecar, &early) == 0);
    assert(ai_result_sidecar_get_latest(&sidecar, &actual) == 0);
    assert(actual.event_id == late.event_id);
    assert(actual.result_sequence == late.result_sequence);
    assert(ai_result_sidecar_get_event(&sidecar, early.event_id, &actual) == 0);
    assert(actual.result_sequence == early.result_sequence);

    assert(ai_result_sidecar_mount(&sidecar, &io) == 0);
    assert(ai_result_sidecar_get_latest(&sidecar, &actual) == 0);
    assert(actual.event_id == late.event_id);
    assert(actual.result_sequence == late.result_sequence);
    assert(ai_result_sidecar_get_event(&sidecar, early.event_id, &actual) == 0);
    assert(actual.event_id == early.event_id);
    assert(actual.result_sequence == early.result_sequence);
}

static void test_last_slot_torn_record_does_not_erase_latest_sector(void)
{
    test_storage_t storage = {0};
    ai_result_sidecar_storage_t io;
    ai_result_sidecar_t sidecar;
    ai_result_sidecar_t remounted;
    ai_result_t actual = {0};
    uint32_t event_id;

    memset(storage.bytes, 0xFF, sizeof(storage.bytes));
    io = make_storage(&storage);
    assert(ai_result_sidecar_mount(&sidecar, &io) == 0);
    for (event_id = 1U; event_id <= 31U; event_id++)
    {
        ai_result_t result = make_result(event_id, (uint8_t)(event_id & 1U));

        assert(ai_result_sidecar_append(&sidecar, &result) == 0);
    }
    storage.write_fail_after = storage.written_bytes
                               + AI_RESULT_SIDECAR_HEADER_BYTES
                               + sizeof(uint32_t) + 2U;
    {
        ai_result_t torn = make_result(32U, 0U);

        assert(ai_result_sidecar_append(&sidecar, &torn) != 0);
    }
    storage.write_fail_after = 0U;

    assert(ai_result_sidecar_mount(&remounted, &io) == 0);
    {
        ai_result_t replacement = make_result(33U, 1U);

        assert(ai_result_sidecar_append(&remounted, &replacement) == 0);
    }
    assert(storage.erase_count == 0U);
    assert(ai_result_sidecar_get_event(&remounted, 31U, &actual) == 0);
    assert(actual.event_id == 31U);
    assert(ai_result_sidecar_get_event(&remounted, 33U, &actual) == 0);
    assert(actual.event_id == 33U);
}

static void test_physical_cursor_survives_lower_sequence_tail(void)
{
    test_storage_t storage = {0};
    ai_result_sidecar_storage_t io;
    ai_result_sidecar_t sidecar;
    ai_result_sidecar_t remounted;
    ai_result_t high = make_result(1000U, 0U);
    ai_result_t low = make_result(1001U, 1U);
    ai_result_t next = make_result(1002U, 0U);
    ai_result_t actual = {0};
    uint32_t event_id;
    const uint32_t slot_count = TEST_STORAGE_BYTES / AI_RESULT_SIDECAR_RECORD_BYTES;
    const uint32_t sector_slots = 4096U / AI_RESULT_SIDECAR_RECORD_BYTES;

    high.result_sequence = 100U;
    low.result_sequence = 1U;
    memset(storage.bytes, 0xFF, sizeof(storage.bytes));
    io = make_storage(&storage);
    assert(ai_result_sidecar_mount(&sidecar, &io) == 0);
    for (event_id = 1U; event_id < sector_slots * 2U; event_id++)
    {
        ai_result_t result = make_result(event_id, (uint8_t)(event_id & 1U));

        assert(ai_result_sidecar_append(&sidecar, &result) == 0);
    }
    assert(ai_result_sidecar_append(&sidecar, &high) == 0);
    for (event_id = sector_slots * 2U;
         event_id < slot_count - 1U; event_id++)
    {
        ai_result_t result = make_result(event_id, (uint8_t)(event_id & 1U));

        result.result_sequence = event_id;
        assert(ai_result_sidecar_append(&sidecar, &result) == 0);
    }
    assert(ai_result_sidecar_append(&sidecar, &low) == 0);

    assert(ai_result_sidecar_mount(&remounted, &io) == 0);
    assert(ai_result_sidecar_get_latest(&remounted, &actual) == 0);
    assert(actual.event_id == high.event_id);
    next.result_sequence = 0U;
    assert(ai_result_sidecar_append(&remounted, &next) == 0);
    assert(storage.erase_count == 1U);
    assert(storage.last_erase_offset == 0U);
    assert(ai_result_sidecar_get_event(&remounted, high.event_id, &actual) == 0);
    assert(actual.event_id == high.event_id);
    assert(actual.result_sequence == high.result_sequence);
    assert(ai_result_sidecar_get_event(&remounted, low.event_id, &actual) == 0);
    assert(actual.event_id == low.event_id);
}

static void test_committed_marker_write_error_is_confirmed_by_readback(void)
{
    test_storage_t storage = {0};
    ai_result_sidecar_storage_t io;
    ai_result_sidecar_t sidecar;
    ai_result_t first = make_result(1100U, 0U);
    ai_result_t second = make_result(1101U, 1U);
    ai_result_t actual = {0};

    memset(storage.bytes, 0xFF, sizeof(storage.bytes));
    storage.fail_after_marker_once = 1U;
    io = make_storage(&storage);
    assert(ai_result_sidecar_mount(&sidecar, &io) == 0);
    assert(ai_result_sidecar_append(&sidecar, &first) == 0);
    assert(sidecar.next_generation == 2U);
    assert(ai_result_sidecar_append(&sidecar, &second) == 0);
    assert(ai_result_sidecar_mount(&sidecar, &io) == 0);
    assert(ai_result_sidecar_get_event(&sidecar, first.event_id, &actual) == 0);
    assert(actual.event_id == first.event_id);
    assert(actual.result_sequence == 1U);
    assert(ai_result_sidecar_get_latest(&sidecar, &actual) == 0);
    assert(actual.event_id == second.event_id);
}

int main(void)
{
    test_v2_record_layout_and_remount_readback();
    test_v2_reader_rejects_crc_and_commit_corruption();
    test_round_trip_and_event_lookup();
    test_mount_rejects_two_sector_journal();
    test_mount_skips_torn_record_and_continues();
    test_full_journal_rotates_one_erase_sector();
    test_delayed_result_keeps_its_published_sequence();
    test_last_slot_torn_record_does_not_erase_latest_sector();
    test_physical_cursor_survives_lower_sequence_tail();
    test_committed_marker_write_error_is_confirmed_by_readback();
    puts("ai result sidecar: PASS");
    return 0;
}
