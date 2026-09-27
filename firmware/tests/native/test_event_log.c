#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "event_log.h"
#include "event_log_format.h"
#include "fake_nor.h"

static void test_event_log_owns_crc_read_scratch(void)
{
    event_log_t log;

    assert(sizeof(log.read_bytes) == 256U);
    assert(sizeof(log.read_event_header) == 160U);
    assert(sizeof(log.read_payload_bytes) == 256U);
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

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    uint32_t index;
    uint32_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? UINT32_C(0xEDB88320) : 0U);
        }
    }

    return crc;
}

static void put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void build_valid_ev01(uint8_t *data, uint32_t event_id)
{
    static const uint8_t payload[16] = {
        0x7FU, 0x01U, 0x02U, 0x03U,
        0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU,
        0x0CU, 0x0DU, 0x0EU, 0x0FU,
    };

    memset(data, 0, 80U);
    memcpy(data, "EV01", 4U);
    data[4] = 1U;
    data[6] = 64U;
    put_u32_le(&data[8], event_id);
    put_u32_le(&data[20], 1600U);
    put_u32_le(&data[28], 1U);
    put_u32_le(&data[32], 0U);
    put_u32_le(&data[48], sizeof(payload));
    memcpy(&data[64], payload, sizeof(payload));
    put_u32_le(&data[52],
               crc32_update(UINT32_C(0xFFFFFFFF), payload, sizeof(payload))
               ^ UINT32_C(0xFFFFFFFF));
}

static void build_valid_ev02(uint8_t *data, uint32_t event_id)
{
    static const uint8_t payload[16] = {
        0x7FU, 0x01U, 0x02U, 0x03U,
        0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU,
        0x0CU, 0x0DU, 0x0EU, 0x0FU,
    };

    memset(data, 0, 144U);
    memcpy(data, "EV02", 4U);
    put_u16_le(&data[4], 2U);
    put_u16_le(&data[6], 128U);
    put_u32_le(&data[8], event_id);
    put_u32_le(&data[32], 1600U);
    put_u32_le(&data[40], 1U);
    put_u32_le(&data[44], 0U);
    put_u32_le(&data[60], sizeof(payload));
    memcpy(&data[128], payload, sizeof(payload));
    put_u32_le(&data[64],
               crc32_update(UINT32_C(0xFFFFFFFF), payload, sizeof(payload))
               ^ UINT32_C(0xFFFFFFFF));
}

static void build_valid_ev03(uint8_t *data, uint32_t event_id)
{
    static const uint8_t payload[16] = {
        0x7FU, 0x01U, 0x02U, 0x03U,
        0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU,
        0x0CU, 0x0DU, 0x0EU, 0x0FU,
    };

    memset(data, 0, 176U);
    memcpy(data, "EV03", 4U);
    put_u16_le(&data[4], 3U);
    put_u16_le(&data[6], 160U);
    put_u32_le(&data[8], event_id);
    put_u32_le(&data[32], 1600U);
    put_u32_le(&data[40], 1U);
    put_u32_le(&data[44], 0U);
    put_u32_le(&data[60], sizeof(payload));
    memcpy(&data[160], payload, sizeof(payload));
    put_u32_le(&data[64],
               crc32_update(UINT32_C(0xFFFFFFFF), payload, sizeof(payload))
               ^ UINT32_C(0xFFFFFFFF));
}

static void assert_valid_superblock(fake_nor_t *flash, uint32_t offset)
{
    uint8_t data[EVENT_LOG_SUPERBLOCK_HEADER_BYTES];

    assert(flash->device.read(&flash->device, offset, data, sizeof(data)) == 0);
    assert(memcmp(data, "ELS1", 4U) == 0);
    assert(get_u16_le(&data[4]) == EVENT_LOG_FORMAT_VERSION);
    assert(get_u16_le(&data[6]) == EVENT_LOG_SUPERBLOCK_HEADER_BYTES);
    assert(get_u32_le(&data[16]) == 1U);
    assert(get_u32_le(&data[20])
           == (crc32_update(UINT32_C(0xFFFFFFFF), data, 20U) ^ UINT32_C(0xFFFFFFFF)));
    assert(get_u32_le(&data[24]) == EVENT_LOG_COMMIT_VALID);
}

#define TEST_FLASH_BYTES (20U * 4096U)
#define TEST_EV01_BYTES 80U
#define TEST_EV02_BYTES 144U
#define TEST_EV03_BYTES 176U
#define EXPECTED_EVENT_LOG_DATA_OFFSET 8192U
#define EXPECTED_EVENT_LOG_RECORD_HEADER_BYTES 32U
#define EXPECTED_EVENT_LOG_RECORD_FOOTER_BYTES 16U
#define EXPECTED_EVENT_LOG_RECORD_SPAN 4096U
#define EXPECTED_EVENT_LOG_COMMIT_VALID UINT32_C(0)
#define TEST_EVENT_PROGRAM_BYTES \
    ((EVENT_LOG_RECORD_HEADER_BYTES - 4U) + TEST_EV01_BYTES \
     + (EVENT_LOG_RECORD_FOOTER_BYTES - 4U))

static void append_valid_event(event_log_t *log, uint32_t event_id)
{
    uint8_t event_bytes[TEST_EV01_BYTES];

    build_valid_ev01(event_bytes, event_id);
    assert(event_log_append_begin(log, event_id, sizeof(event_bytes)) == RT_EOK);
    assert(event_log_append_write(log, event_bytes, sizeof(event_bytes)) == RT_EOK);
}

static void assert_recovered_event_count(fake_nor_t *flash,
                                         uint32_t expected_count,
                                         uint32_t expected_next_event_id)
{
    event_log_t recovered = {0};
    event_log_status_t status = {0};

    assert(event_log_mount(&recovered, &flash->device) == RT_EOK);
    assert(event_log_get_status(&recovered, &status) == RT_EOK);
    assert(status.committed_event_count == expected_count);
    assert(status.next_event_id == expected_next_event_id);
    if (expected_count != 0U)
    {
        assert(event_log_verify_event(&recovered, 1U) == RT_EOK);
    }
}

static void test_ev02_is_committed_and_recovers_with_legacy_ev01_checks_intact(void)
{
    uint8_t storage[TEST_FLASH_BYTES];
    uint8_t event_bytes[TEST_EV02_BYTES];
    fake_nor_t flash = {0};
    event_log_t formatted = {0};
    event_log_t recovered = {0};
    event_log_status_t status = {0};

    build_valid_ev02(event_bytes, 1U);
    assert(fake_nor_init(&flash, storage, sizeof(storage)) == RT_EOK);
    assert(event_log_format(&formatted, &flash.device) == RT_EOK);
    assert(event_log_append_begin(&formatted, 1U, sizeof(event_bytes)) == RT_EOK);
    assert(event_log_append_write(&formatted, event_bytes, sizeof(event_bytes)) == RT_EOK);
    assert(event_log_mount(&recovered, &flash.device) == RT_EOK);
    assert(event_log_get_status(&recovered, &status) == RT_EOK);
    assert(status.committed_event_count == 1U);
    assert(status.next_event_id == 2U);
    assert(event_log_verify_event(&recovered, 1U) == RT_EOK);
}

static void test_ev02_with_a_bad_inner_payload_crc_is_rejected(void)
{
    uint8_t storage[TEST_FLASH_BYTES];
    uint8_t event_bytes[TEST_EV02_BYTES];
    fake_nor_t flash = {0};
    event_log_t log = {0};
    event_log_t recovered = {0};
    event_log_status_t status = {0};

    build_valid_ev02(event_bytes, 1U);
    event_bytes[64] ^= 0x01U;
    assert(fake_nor_init(&flash, storage, sizeof(storage)) == RT_EOK);
    assert(event_log_format(&log, &flash.device) == RT_EOK);
    assert(event_log_append_begin(&log, 1U, sizeof(event_bytes)) == RT_EOK);
    assert(event_log_append_write(&log, event_bytes, sizeof(event_bytes)) != RT_EOK);
    assert(event_log_get_status(&log, &status) == RT_EOK);
    assert(status.committed_event_count == 0U);
    assert(event_log_mount(&recovered, &flash.device) == RT_EOK);
    assert(event_log_get_status(&recovered, &status) == RT_EOK);
    assert(status.committed_event_count == 0U);
}

static void test_ev03_is_committed_and_recovers_with_legacy_formats_intact(void)
{
    uint8_t storage[TEST_FLASH_BYTES];
    uint8_t event_bytes[TEST_EV03_BYTES];
    fake_nor_t flash = {0};
    event_log_t formatted = {0};
    event_log_t recovered = {0};
    event_log_status_t status = {0};

    build_valid_ev03(event_bytes, 1U);
    assert(fake_nor_init(&flash, storage, sizeof(storage)) == RT_EOK);
    assert(event_log_format(&formatted, &flash.device) == RT_EOK);
    assert(event_log_append_begin(&formatted, 1U, sizeof(event_bytes)) == RT_EOK);
    assert(event_log_append_write(&formatted, event_bytes, sizeof(event_bytes)) == RT_EOK);
    assert(event_log_mount(&recovered, &flash.device) == RT_EOK);
    assert(event_log_get_status(&recovered, &status) == RT_EOK);
    assert(status.committed_event_count == 1U);
    assert(status.next_event_id == 2U);
    assert(event_log_verify_event(&recovered, 1U) == RT_EOK);
}

static void test_el01_record_layout_and_ev03_readback_are_frozen(void)
{
    uint8_t storage[TEST_FLASH_BYTES];
    uint8_t event_bytes[TEST_EV03_BYTES];
    uint8_t record_header[EXPECTED_EVENT_LOG_RECORD_HEADER_BYTES];
    uint8_t record_footer[EXPECTED_EVENT_LOG_RECORD_FOOTER_BYTES];
    uint8_t readback[TEST_EV03_BYTES];
    fake_nor_t flash = {0};
    event_log_t log = {0};
    uint32_t event_crc;
    uint32_t read_length = 0U;
    uint32_t record_offset = EXPECTED_EVENT_LOG_DATA_OFFSET;
    uint32_t footer_offset = record_offset
                             + EXPECTED_EVENT_LOG_RECORD_HEADER_BYTES
                             + sizeof(event_bytes);

    build_valid_ev03(event_bytes, 1U);
    event_crc = crc32_update(UINT32_C(0xFFFFFFFF), event_bytes,
                             sizeof(event_bytes))
                ^ UINT32_C(0xFFFFFFFF);

    assert(fake_nor_init(&flash, storage, sizeof(storage)) == RT_EOK);
    assert(event_log_format(&log, &flash.device) == RT_EOK);
    assert(event_log_append_begin(&log, 1U, sizeof(event_bytes)) == RT_EOK);
    assert(event_log_append_write(&log, event_bytes, sizeof(event_bytes)) == RT_EOK);

    assert(flash.device.read(&flash.device, record_offset,
                             record_header, sizeof(record_header)) == RT_EOK);
    assert(memcmp(record_header, "EL01", 4U) == 0);
    assert(get_u16_le(&record_header[4]) == 1U);
    assert(get_u16_le(&record_header[6])
           == EXPECTED_EVENT_LOG_RECORD_HEADER_BYTES);
    assert(get_u32_le(&record_header[8])
           == EXPECTED_EVENT_LOG_RECORD_SPAN);
    assert(get_u32_le(&record_header[12]) == 1U);
    assert(get_u32_le(&record_header[16]) == sizeof(event_bytes));
    assert(get_u32_le(&record_header[20]) == event_crc);
    assert(get_u32_le(&record_header[24])
           == (crc32_update(UINT32_C(0xFFFFFFFF), record_header, 24U)
               ^ UINT32_C(0xFFFFFFFF)));
    assert(get_u32_le(&record_header[28]) == UINT32_C(0xFFFFFFFF));

    assert(flash.device.read(&flash.device,
                             footer_offset,
                             record_footer, sizeof(record_footer)) == RT_EOK);
    assert(memcmp(record_footer, "ELC1", 4U) == 0);
    assert(get_u32_le(&record_footer[4]) == event_crc);
    assert(get_u32_le(&record_footer[8])
           == EXPECTED_EVENT_LOG_COMMIT_VALID);
    assert(get_u32_le(&record_footer[12]) == UINT32_C(0xFFFFFFFF));

    assert(event_log_read_event(&log, 1U, 0U, readback, sizeof(readback),
                                &read_length) == RT_EOK);
    assert(read_length == sizeof(readback));
    assert(memcmp(readback, event_bytes, sizeof(event_bytes)) == 0);
    assert(event_log_verify_event(&log, 1U) == RT_EOK);
}

static void test_metadata_queries_have_bounded_read_cost(void)
{
    uint8_t storage[TEST_FLASH_BYTES];
    fake_nor_t flash = {0};
    event_log_t log = {0};
    event_log_t recovered = {0};
    event_log_event_info_t info = {0};
    event_log_event_info_t page[2] = {0};
    uint8_t bytes[16];
    uint32_t read_length;
    uint16_t event_count;
    uint32_t next_event_id;
    uint32_t event_id;

    assert(fake_nor_init(&flash, storage, sizeof(storage)) == RT_EOK);
    assert(event_log_format(&log, &flash.device) == RT_EOK);
    for (event_id = 1U; event_id <= 18U; event_id++)
    {
        append_valid_event(&log, event_id);
    }
    assert(event_log_mount(&recovered, &flash.device) == RT_EOK);

    fake_nor_reset_counters(&flash);
    assert(event_log_get_event_info(&recovered, 18U, &info) == RT_EOK);
    assert(info.event_id == 18U);
    assert(flash.read_call_count <= 4U);
    assert(flash.read_byte_count <= 96U);

    fake_nor_reset_counters(&flash);
    assert(event_log_read_event(&recovered, 18U, 0U, bytes, sizeof(bytes),
                                &read_length) == RT_EOK);
    assert(read_length == sizeof(bytes));
    assert(event_log_read_event(&recovered, 18U, 16U, bytes, sizeof(bytes),
                                &read_length) == RT_EOK);
    assert(read_length == sizeof(bytes));
    assert(flash.read_call_count == 2U);
    assert(flash.read_byte_count == 32U);

    fake_nor_reset_counters(&flash);
    assert(event_log_list_events(&recovered, 16U, 2U, page, 2U,
                                 &event_count, &next_event_id) == RT_EOK);
    assert(event_count == 2U);
    assert(page[0].event_id == 17U && page[1].event_id == 18U);
    assert(next_event_id == 0U);
    assert(flash.read_call_count <= 4U);
    assert(flash.read_byte_count <= 96U);
}

static void test_power_cut_at_each_event_byte(void)
{
    static uint8_t base_storage[TEST_FLASH_BYTES];
    static uint8_t interrupted_storage[TEST_FLASH_BYTES];
    uint8_t event_bytes[TEST_EV01_BYTES];
    fake_nor_t base_flash = {0};
    fake_nor_t interrupted_flash = {0};
    event_log_t base_log = {0};
    event_log_t interrupted_log = {0};
    uint32_t cut_after_bytes;
    rt_err_t result;

    build_valid_ev01(event_bytes, 2U);
    assert(TEST_EVENT_PROGRAM_BYTES == 120U);
    assert(fake_nor_init(&base_flash, base_storage, sizeof(base_storage)) == RT_EOK);
    assert(event_log_format(&base_log, &base_flash.device) == RT_EOK);
    append_valid_event(&base_log, 1U);

    for (cut_after_bytes = 0U;
         cut_after_bytes < TEST_EVENT_PROGRAM_BYTES;
         cut_after_bytes++)
    {
        assert(fake_nor_init(&interrupted_flash, interrupted_storage,
                             sizeof(interrupted_storage)) == RT_EOK);
        memcpy(interrupted_storage, base_storage, sizeof(interrupted_storage));
        assert(event_log_mount(&interrupted_log, &interrupted_flash.device) == RT_EOK);

        fake_nor_arm_power_cut(&interrupted_flash, cut_after_bytes);
        result = event_log_append_begin(&interrupted_log, 2U, sizeof(event_bytes));
        if (result == RT_EOK)
        {
            result = event_log_append_write(&interrupted_log, event_bytes,
                                            sizeof(event_bytes));
        }
        assert(result != RT_EOK);
        assert(interrupted_flash.power_cut_triggered == RT_TRUE);

        fake_nor_clear_power_cut(&interrupted_flash);
        assert_recovered_event_count(&interrupted_flash, 1U, 2U);
    }
}

static void test_power_cut_at_each_generation_three_checkpoint_byte(void)
{
    static uint8_t base_storage[TEST_FLASH_BYTES];
    static uint8_t interrupted_storage[TEST_FLASH_BYTES];
    fake_nor_t base_flash = {0};
    fake_nor_t interrupted_flash = {0};
    event_log_t base_log = {0};
    event_log_t interrupted_log = {0};
    uint8_t event_bytes[TEST_EV01_BYTES];
    uint32_t index;
    uint32_t cut_after_bytes;

    assert(fake_nor_init(&base_flash, base_storage, sizeof(base_storage)) == RT_EOK);
    assert(event_log_format(&base_log, &base_flash.device) == RT_EOK);
    for (index = 1U; index < 16U; index++)
    {
        append_valid_event(&base_log, index);
    }
    build_valid_ev01(event_bytes, 16U);

    for (cut_after_bytes = TEST_EVENT_PROGRAM_BYTES;
         cut_after_bytes < TEST_EVENT_PROGRAM_BYTES + 28U;
         cut_after_bytes++)
    {
        assert(fake_nor_init(&interrupted_flash, interrupted_storage,
                             sizeof(interrupted_storage)) == RT_EOK);
        memcpy(interrupted_storage, base_storage, sizeof(interrupted_storage));
        assert(event_log_mount(&interrupted_log, &interrupted_flash.device) == RT_EOK);

        fake_nor_arm_power_cut(&interrupted_flash, cut_after_bytes);
        assert(event_log_append_begin(&interrupted_log, 16U, sizeof(event_bytes)) == RT_EOK);
        assert(event_log_append_write(&interrupted_log, event_bytes,
                                      sizeof(event_bytes)) != RT_EOK);
        assert(interrupted_flash.power_cut_triggered == RT_TRUE);

        fake_nor_clear_power_cut(&interrupted_flash);
        assert_recovered_event_count(&interrupted_flash, 16U, 17U);
    }
}

static void test_mount_busy_sets_error_state(void)
{
    uint8_t storage[20U * 1024U];
    fake_nor_t flash = {0};
    event_log_t formatted = {0};
    event_log_t mounted = {0};
    event_log_status_t status = {0};

    assert(fake_nor_init(&flash, storage, sizeof(storage)) == RT_EOK);
    assert(event_log_format(&formatted, &flash.device) == RT_EOK);

    fake_nor_set_busy(&flash, RT_TRUE);
    assert(event_log_mount(&mounted, &flash.device) != RT_EOK);
    assert(event_log_get_status(&mounted, &status) == RT_EOK);
    assert(status.state == EVENT_LOG_ERROR);
    fake_nor_set_busy(&flash, RT_FALSE);
}

static void test_power_cut_during_erase_preserves_committed_event(void)
{
    uint8_t storage[20U * 1024U];
    uint8_t event_bytes[TEST_EV01_BYTES];
    fake_nor_t flash = {0};
    event_log_t log = {0};

    build_valid_ev01(event_bytes, 1U);
    assert(fake_nor_init(&flash, storage, sizeof(storage)) == RT_EOK);
    assert(event_log_format(&log, &flash.device) == RT_EOK);
    append_valid_event(&log, 1U);

    fake_nor_arm_erase_power_cut(&flash);
    assert(event_log_append_begin(&log, 2U, sizeof(event_bytes)) != RT_EOK);
    assert(flash.erase_power_cut_triggered == RT_TRUE);
    fake_nor_clear_erase_power_cut(&flash);
    assert_recovered_event_count(&flash, 1U, 2U);
}

static void test_data_scan_read_failure_sets_error_state(void)
{
    uint8_t storage[20U * 1024U];
    uint8_t cleared_byte = 0U;
    fake_nor_t flash = {0};
    event_log_t formatted = {0};
    event_log_t mounted = {0};
    event_log_status_t status = {0};

    assert(fake_nor_init(&flash, storage, sizeof(storage)) == RT_EOK);
    assert(event_log_format(&formatted, &flash.device) == RT_EOK);
    assert(flash.device.program(&flash.device, EVENT_LOG_SUPERBLOCK_A_OFFSET,
                                &cleared_byte, 1U) == RT_EOK);
    assert(flash.device.program(&flash.device, EVENT_LOG_SUPERBLOCK_B_OFFSET,
                                &cleared_byte, 1U) == RT_EOK);

    fake_nor_set_read_failure_after(&flash, 2U);
    assert(event_log_mount(&mounted, &flash.device) != RT_EOK);
    assert(event_log_get_status(&mounted, &status) == RT_EOK);
    assert(status.state == EVENT_LOG_ERROR);
}

int main(void)
{
    uint8_t storage[20U * 1024U];
    uint8_t verify_storage[20U * 1024U];
    uint8_t mismatch_storage[20U * 1024U];
    uint8_t bad_crc_storage[20U * 1024U];
    uint8_t event_bytes[80] = {0};
    uint8_t mismatched_id_event[80] = {0};
    uint8_t bad_payload_crc_event[80] = {0};
    uint8_t first_read[17] = {0};
    uint8_t second_read[63] = {0};
    uint8_t readback[80] = {0};
    fake_nor_t flash = {0};
    fake_nor_t verify_flash = {0};
    fake_nor_t mismatch_flash = {0};
    fake_nor_t bad_crc_flash = {0};
    event_log_t log = {0};
    event_log_t recovered = {0};
    event_log_t recovered_again = {0};
    event_log_t verify_log = {0};
    event_log_t mismatch_log = {0};
    event_log_t mismatch_recovered = {0};
    event_log_t bad_crc_log = {0};
    event_log_t bad_crc_recovered = {0};
    event_log_status_t status = {0};
    event_log_recovery_info_t recovery_info = {0};
    event_log_event_info_t event_info = {0};
    uint32_t readback_length = 0U;
    uint8_t cleared_byte = 0U;
    uint8_t changed_payload_byte = 0x7EU;

    build_valid_ev01(event_bytes, 1U);
    memcpy(mismatched_id_event, event_bytes, sizeof(mismatched_id_event));
    put_u32_le(&mismatched_id_event[8], 2U);
    memcpy(bad_payload_crc_event, event_bytes, sizeof(bad_payload_crc_event));
    bad_payload_crc_event[52] ^= 0x01U;

    assert(fake_nor_init(&flash, storage, sizeof(storage)) == 0);
    assert(event_log_format(&log, &flash.device) == 0);
    assert_valid_superblock(&flash, EVENT_LOG_SUPERBLOCK_A_OFFSET);
    assert_valid_superblock(&flash, EVENT_LOG_SUPERBLOCK_B_OFFSET);
    assert(event_log_mount(&log, &flash.device) == 0);
    assert(event_log_append_begin(&log, 1U, sizeof(event_bytes)) == 0);
    assert(event_log_append_write(&log, event_bytes, sizeof(event_bytes)) == 0);
    assert(event_log_get_event_info(&log, 1U, &event_info) == 0);
    assert(event_info.event_id == 1U);
    assert(event_info.ev01_length == sizeof(event_bytes));
    assert(event_log_verify_event(&log, 1U) == 0);
    assert(event_log_read_event(&log, 1U, 0U, first_read, sizeof(first_read),
                                &readback_length) == 0);
    assert(readback_length == sizeof(first_read));
    assert(event_log_read_event(&log, 1U, sizeof(first_read), second_read,
                                sizeof(second_read), &readback_length) == 0);
    assert(readback_length == sizeof(second_read));
    memcpy(readback, first_read, sizeof(first_read));
    memcpy(&readback[sizeof(first_read)], second_read, sizeof(second_read));
    assert(memcmp(readback, event_bytes, sizeof(event_bytes)) == 0);

    assert(fake_nor_init(&mismatch_flash, mismatch_storage, sizeof(mismatch_storage)) == 0);
    assert(event_log_format(&mismatch_log, &mismatch_flash.device) == 0);
    assert(event_log_append_begin(&mismatch_log, 1U, sizeof(mismatched_id_event)) == 0);
    assert(event_log_append_write(&mismatch_log, mismatched_id_event,
                                  sizeof(mismatched_id_event)) != 0);
    assert(event_log_get_status(&mismatch_log, &status) == 0);
    assert(status.committed_event_count == 0U);
    assert(status.next_event_id == 1U);
    assert(event_log_mount(&mismatch_recovered, &mismatch_flash.device) == 0);
    assert(event_log_get_status(&mismatch_recovered, &status) == 0);
    assert(status.committed_event_count == 0U);
    assert(status.next_event_id == 1U);

    assert(fake_nor_init(&bad_crc_flash, bad_crc_storage, sizeof(bad_crc_storage)) == 0);
    assert(event_log_format(&bad_crc_log, &bad_crc_flash.device) == 0);
    assert(event_log_append_begin(&bad_crc_log, 1U, sizeof(bad_payload_crc_event)) == 0);
    assert(event_log_append_write(&bad_crc_log, bad_payload_crc_event,
                                  sizeof(bad_payload_crc_event)) != 0);
    assert(event_log_get_status(&bad_crc_log, &status) == 0);
    assert(status.committed_event_count == 0U);
    assert(status.next_event_id == 1U);
    assert(event_log_mount(&bad_crc_recovered, &bad_crc_flash.device) == 0);
    assert(event_log_get_status(&bad_crc_recovered, &status) == 0);
    assert(status.committed_event_count == 0U);
    assert(status.next_event_id == 1U);

    assert(fake_nor_init(&verify_flash, verify_storage, sizeof(verify_storage)) == 0);
    assert(event_log_format(&verify_log, &verify_flash.device) == 0);
    assert(event_log_append_begin(&verify_log, 1U, sizeof(event_bytes)) == 0);
    assert(event_log_append_write(&verify_log, event_bytes, sizeof(event_bytes)) == 0);
    assert(event_log_verify_event(&verify_log, 1U) == 0);
    assert(verify_flash.device.program(&verify_flash.device,
                                       EVENT_LOG_DATA_OFFSET + 32U + 64U,
                                       &changed_payload_byte, 1U) == 0);
    assert(event_log_verify_event(&verify_log, 1U) != 0);

    assert(flash.device.program(&flash.device, EVENT_LOG_SUPERBLOCK_A_OFFSET,
                                &cleared_byte, 1U) == 0);
    assert(event_log_mount(&recovered, &flash.device) == 0);
    assert(event_log_get_status(&recovered, &status) == 0);
    assert(event_log_get_recovery_info(&recovered, &recovery_info) == 0);
    assert(recovery_info.source == EVENT_LOG_RECOVERY_SUPERBLOCK_B);
    assert(status.committed_event_count == 1U);
    assert(status.next_event_id == 2U);

    assert(event_log_format(&log, &flash.device) == 0);
    assert(flash.device.program(&flash.device, EVENT_LOG_SUPERBLOCK_B_OFFSET,
                                &cleared_byte, 1U) == 0);
    assert(event_log_mount(&recovered, &flash.device) == 0);
    assert(event_log_get_recovery_info(&recovered, &recovery_info) == 0);
    assert(recovery_info.source == EVENT_LOG_RECOVERY_SUPERBLOCK_A);
    assert(event_log_get_status(&recovered, &status) == 0);
    assert(status.committed_event_count == 0U);
    assert(status.next_event_id == 1U);

    assert(event_log_append_begin(&recovered, 1U, sizeof(event_bytes)) == 0);
    assert(event_log_append_write(&recovered, event_bytes, sizeof(event_bytes)) == 0);
    assert(flash.device.program(&flash.device, EVENT_LOG_SUPERBLOCK_A_OFFSET,
                                &cleared_byte, 1U) == 0);
    assert(event_log_mount(&recovered_again, &flash.device) == 0);
    assert(event_log_get_status(&recovered_again, &status) == 0);
    assert(event_log_get_recovery_info(&recovered_again, &recovery_info) == 0);
    assert(recovery_info.source == EVENT_LOG_RECOVERY_DATA_SCAN);
    assert(recovery_info.discarded_incomplete_record_count == 0U);
    assert(status.committed_event_count == 1U);
    assert(status.next_event_id == 2U);

    fake_nor_set_power_cut(&flash, 30U);
    assert(event_log_append_begin(&recovered_again, 2U, sizeof(event_bytes)) == 0);
    assert(event_log_append_write(&recovered_again, event_bytes, sizeof(event_bytes)) != 0);
    fake_nor_set_power_cut(&flash, 0U);
    assert(event_log_mount(&recovered, &flash.device) == 0);
    assert(event_log_get_status(&recovered, &status) == 0);
    assert(event_log_get_recovery_info(&recovered, &recovery_info) == 0);
    assert(recovery_info.source == EVENT_LOG_RECOVERY_DATA_SCAN);
    assert(recovery_info.discarded_incomplete_record_count == 1U);
    assert(status.committed_event_count == 1U);
    assert(status.next_event_id == 2U);
    memset(event_bytes, 0, sizeof(event_bytes));
    assert(event_log_append_begin(&recovered, 2U, sizeof(event_bytes)) == 0);
    assert(event_log_append_write(&recovered, event_bytes, sizeof(event_bytes)) != 0);
    assert(event_log_mount(&log, &flash.device) == 0);
    assert(event_log_get_status(&log, &status) == 0);
    assert(status.committed_event_count == 1U);
    assert(status.next_event_id == 2U);
    assert(event_log_append_begin(&log, 2U, sizeof(event_bytes)) == 0);
    assert(event_log_format(&log, &flash.device) != 0);
    event_log_append_abort(&log);

    test_event_log_owns_crc_read_scratch();
    test_power_cut_at_each_event_byte();
    test_power_cut_at_each_generation_three_checkpoint_byte();
    test_data_scan_read_failure_sets_error_state();
    test_mount_busy_sets_error_state();
    test_power_cut_during_erase_preserves_committed_event();
    test_ev02_is_committed_and_recovers_with_legacy_ev01_checks_intact();
    test_ev02_with_a_bad_inner_payload_crc_is_rejected();
    test_ev03_is_committed_and_recovers_with_legacy_formats_intact();
    test_el01_record_layout_and_ev03_readback_are_frozen();
    test_metadata_queries_have_bounded_read_cost();

    return 0;
}
