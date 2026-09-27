#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fake_nor.h"
#include "power_runtime.h"

#define MSH_CMD_EXPORT_ALIAS(function, alias, description)                   \
    static int (*const msh_export_##alias)(int, char **)                     \
        __attribute__((unused)) = function
#include "../../app/storage/storage_service.c"

#define TEST_FLASH_BYTES (20U * 4096U)

static uint32_t test_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    uint32_t index;
    uint32_t bit;

    for (index = 0U; index < length; ++index)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc >> 1U)
                  ^ ((crc & 1U) != 0U ? UINT32_C(0xEDB88320) : 0U);
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

static void test_put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void test_build_valid_event(uint8_t *data, uint32_t event_id)
{
    static const uint8_t samples[16] = {
        0x7FU, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU,
    };

    memset(data, 0, 80U);
    memcpy(data, "EV01", 4U);
    data[4] = 1U;
    data[6] = 64U;
    test_put_u32_le(&data[8], event_id);
    test_put_u32_le(&data[20], 1600U);
    test_put_u32_le(&data[28], 1U);
    test_put_u32_le(&data[32], 0U);
    test_put_u32_le(&data[48], sizeof(samples));
    test_put_u32_le(&data[52], test_crc32(samples, sizeof(samples)));
    memcpy(&data[64], samples, sizeof(samples));
}

static uint32_t test_power_blockers;
static uint32_t test_power_peak_blockers;
static rt_bool_t test_power_release_without_hold;
static rt_bool_t test_power_fail_maintenance;
static power_blocker_t test_power_fail_acquire;
static power_mode_t test_power_mode = POWER_MODE_MONITOR;

static void reset_power_test_state(void)
{
    test_power_blockers = 0U;
    test_power_peak_blockers = 0U;
    test_power_release_without_hold = RT_FALSE;
    test_power_fail_maintenance = RT_FALSE;
    test_power_fail_acquire = (power_blocker_t)0U;
    test_power_mode = POWER_MODE_MONITOR;
}

rt_err_t power_runtime_acquire_blocker(power_blocker_t blocker)
{
    if (blocker == test_power_fail_acquire)
    {
        return -RT_ERROR;
    }
    test_power_blockers |= (uint32_t)blocker;
    test_power_peak_blockers |= test_power_blockers;
    return RT_EOK;
}

void power_runtime_release_blocker(power_blocker_t blocker)
{
    if ((test_power_blockers & (uint32_t)blocker) == 0U)
    {
        test_power_release_without_hold = RT_TRUE;
    }
    test_power_blockers &= ~((uint32_t)blocker);
}

rt_err_t power_runtime_enter_maintenance_if_monitor(rt_bool_t *entered)
{
    if (entered == RT_NULL)
    {
        return -RT_EINVAL;
    }
    *entered = RT_FALSE;
    if (test_power_fail_maintenance)
    {
        return -RT_ERROR;
    }
    if (test_power_mode == POWER_MODE_MAINTENANCE)
    {
        return RT_EOK;
    }
    if (test_power_mode != POWER_MODE_MONITOR)
    {
        return -RT_EINVAL;
    }
    test_power_mode = POWER_MODE_MAINTENANCE;
    *entered = RT_TRUE;
    return RT_EOK;
}

rt_err_t power_runtime_exit_maintenance(void)
{
    if (test_power_mode != POWER_MODE_MAINTENANCE)
    {
        return -RT_EINVAL;
    }
    test_power_mode = POWER_MODE_MONITOR;
    return RT_EOK;
}

rt_err_t power_runtime_exit_maintenance_if_owned(void)
{
    return power_runtime_exit_maintenance();
}

rt_err_t board_u8_nor_spi_init(void)
{
    return -RT_ERROR;
}

rt_err_t board_u8_nor_spi_transfer(const uint8_t *tx,
                                   uint8_t *rx,
                                   rt_size_t length)
{
    (void)tx;
    (void)rx;
    (void)length;
    return -RT_ERROR;
}

void nor_flash_w25q_init(nor_flash_w25q_t *flash,
                         nor_flash_transfer_fn transfer_fn,
                         void *transfer_context)
{
    (void)flash;
    (void)transfer_fn;
    (void)transfer_context;
}

rt_err_t nor_flash_w25q_probe(nor_flash_w25q_t *flash,
                              uint8_t jedec_id[3])
{
    (void)flash;
    (void)jedec_id;
    return -RT_ERROR;
}

static int expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "storage service format: %s\n", message);
        return 0;
    }
    return 1;
}

int main(void)
{
    uint8_t storage[TEST_FLASH_BYTES];
    uint8_t payload[80] = {0};
    fake_nor_t flash = {0};
    uint32_t erase_count_after_format;
    uint32_t erase_count_before_failed_format;

    if (!expect(fake_nor_init(&flash, storage, sizeof(storage)) == RT_EOK,
                "the test NOR must initialize"))
    {
        return 1;
    }
    storage_flash.device = flash.device;
    storage_started = RT_TRUE;
    storage_mutex_initialized = RT_TRUE;
    storage_format_confirmation_init(&storage_format_confirmation);
    reset_power_test_state();
    test_build_valid_event(payload, 1U);

    if (!expect(storage_service_confirm_format(100U) != RT_EOK,
                "confirm without request must fail")
        || !expect(flash.erase_call_count == 0U,
                   "confirm without request must not erase"))
    {
        return 1;
    }

    if (!expect(storage_service_request_format(100U) == RT_EOK,
                "a started service must accept a format request")
        || !expect(flash.erase_call_count == 0U,
                   "request alone must not erase")
        || !expect(storage_service_confirm_format(
                       100U + STORAGE_FORMAT_CONFIRM_WINDOW_TICKS) != RT_EOK,
                   "an expired confirmation must fail")
        || !expect(flash.erase_call_count == 0U,
                   "an expired confirmation must not erase"))
    {
        return 1;
    }

    if (!expect(storage_service_request_format(500U) == RT_EOK,
                "a fresh format request must be accepted")
        || !expect(storage_service_confirm_format(501U) == RT_EOK,
                   "a confirmation inside the window must format"))
    {
        return 1;
    }
    erase_count_after_format = flash.erase_call_count;
    if (!expect(erase_count_after_format != 0U,
                "valid confirmation must execute real NOR erases")
        || !expect((test_power_peak_blockers
                    & (POWER_BLOCKER_STORAGE | POWER_BLOCKER_OTA_DIAGNOSTIC))
                       == (POWER_BLOCKER_STORAGE | POWER_BLOCKER_OTA_DIAGNOSTIC),
                   "full format must hold STORAGE and OTA_DIAGNOSTIC")
        || !expect(test_power_blockers == 0U,
                   "successful format must release all power blockers")
        || !expect(test_power_release_without_hold == RT_FALSE,
                   "successful format must not release an unowned blocker")
        || !expect(test_power_mode == POWER_MODE_MONITOR,
                   "format maintenance window must return to MONITOR")
        || !expect(storage_service_confirm_format(502U) != RT_EOK,
                   "a successful confirmation must be single use")
        || !expect(flash.erase_call_count == erase_count_after_format,
                   "repeated confirmation must not erase again"))
    {
        return 1;
    }

    reset_power_test_state();
    if (!expect(storage_service_event_begin(1U, sizeof(payload)) == RT_EOK,
                "event begin must succeed on a formatted log")
        || !expect((test_power_peak_blockers & POWER_BLOCKER_STORAGE) != 0U,
                   "event begin must hold STORAGE")
        || !expect(test_power_blockers == 0U,
                   "event begin must release STORAGE")
        || !expect(test_power_release_without_hold == RT_FALSE,
                   "event begin must not release an unowned blocker"))
    {
        return 1;
    }

    reset_power_test_state();
    if (!expect(storage_service_event_write(payload, sizeof(payload),
                                            RT_NULL) == RT_EOK,
                "event write must succeed for the active record")
        || !expect((test_power_peak_blockers & POWER_BLOCKER_STORAGE) != 0U,
                   "event write must hold STORAGE")
        || !expect(test_power_blockers == 0U,
                   "event write must release STORAGE")
        || !expect(test_power_release_without_hold == RT_FALSE,
                   "event write must not release an unowned blocker"))
    {
        return 1;
    }

    reset_power_test_state();
    if (!expect(storage_service_event_begin(2U, sizeof(payload)) == RT_EOK,
                "second event begin must succeed before abort")
        || !expect(storage_service_event_abort() == RT_EOK,
                   "event abort must remount the log")
        || !expect((test_power_peak_blockers & POWER_BLOCKER_STORAGE) != 0U,
                   "event abort must hold STORAGE")
        || !expect(test_power_blockers == 0U,
                   "event abort must release STORAGE")
        || !expect(test_power_release_without_hold == RT_FALSE,
                   "event abort must not release an unowned blocker"))
    {
        return 1;
    }

    fake_nor_set_busy(&flash, RT_TRUE);
    reset_power_test_state();
    erase_count_before_failed_format = flash.erase_call_count;
    if (!expect(storage_service_request_format(700U) == RT_EOK,
                "a failed format must still be requestable")
        || !expect(storage_service_confirm_format(701U) != RT_EOK,
                   "busy NOR must make format fail")
        || !expect(test_power_blockers == 0U,
                   "failed format must release all power blockers")
        || !expect(test_power_release_without_hold == RT_FALSE,
                   "failed format must not release an unowned blocker")
        || !expect(flash.erase_call_count == erase_count_before_failed_format,
                   "busy format must not count a successful erase"))
    {
        fake_nor_set_busy(&flash, RT_FALSE);
        return 1;
    }
    fake_nor_set_busy(&flash, RT_FALSE);

    reset_power_test_state();
    test_power_fail_acquire = POWER_BLOCKER_STORAGE;
    if (!expect(storage_service_request_format(800U) == RT_EOK,
                "a blocker failure test must be requestable")
        || !expect(storage_service_confirm_format(801U) != RT_EOK,
                   "storage blocker failure must reject format")
        || !expect(test_power_blockers == 0U,
                   "storage blocker failure must leave no hold")
        || !expect(test_power_mode == POWER_MODE_MONITOR,
                   "storage blocker failure must exit maintenance")
        || !expect(flash.erase_call_count == erase_count_before_failed_format,
                   "storage blocker failure must not erase NOR"))
    {
        return 1;
    }

    reset_power_test_state();
    test_power_fail_acquire = POWER_BLOCKER_OTA_DIAGNOSTIC;
    if (!expect(storage_service_request_format(900U) == RT_EOK,
                "an OTA blocker failure test must be requestable")
        || !expect(storage_service_confirm_format(901U) != RT_EOK,
                   "OTA blocker failure must reject format")
        || !expect(test_power_blockers == 0U,
                   "OTA blocker failure must release STORAGE")
        || !expect(test_power_mode == POWER_MODE_MONITOR,
                   "OTA blocker failure must exit maintenance")
        || !expect(flash.erase_call_count == erase_count_before_failed_format,
                   "OTA blocker failure must not erase NOR"))
    {
        return 1;
    }

    reset_power_test_state();
    test_power_fail_maintenance = RT_TRUE;
    if (!expect(storage_service_request_format(1000U) == RT_EOK,
                "a maintenance failure test must be requestable")
        || !expect(storage_service_confirm_format(1001U) != RT_EOK,
                   "maintenance failure must reject format")
        || !expect(test_power_blockers == 0U,
                   "maintenance failure must leave no hold")
        || !expect(flash.erase_call_count == erase_count_before_failed_format,
                   "maintenance failure must not erase NOR"))
    {
        return 1;
    }

    puts("storage service format: PASS");
    return 0;
}
