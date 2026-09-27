#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_state.h"

static ota_state_record_t make_record(uint32_t generation, ota_state_kind_t state,
                                       uint32_t trial_count, uint32_t copy_offset)
{
    ota_state_record_t record = {
        .generation = generation,
        .state = state,
        .trial_count = trial_count,
        .copy_offset = copy_offset,
    };

    return record;
}

static void test_newest_valid_state_record_is_selected(void)
{
    uint8_t primary[OTA_STATE_RECORD_BYTES];
    uint8_t secondary[OTA_STATE_RECORD_BYTES];
    ota_state_record_t selected = {0};
    const ota_state_record_t first = make_record(7U, OTA_STATE_NORMAL, 0U, 0U);
    const ota_state_record_t second = make_record(8U, OTA_STATE_PENDING_INSTALL, 0U, 512U);

    ota_state_record_encode(&first, primary);
    ota_state_record_encode(&second, secondary);
    assert(ota_state_select_newest(primary, secondary, &selected)
           == OTA_STATE_SELECT_SECONDARY);
    assert(selected.generation == 8U);
    assert(selected.state == OTA_STATE_PENDING_INSTALL);
    assert(selected.copy_offset == 512U);
}

static void test_torn_or_corrupt_newest_record_falls_back_to_older_valid_record(void)
{
    uint8_t primary[OTA_STATE_RECORD_BYTES];
    uint8_t secondary[OTA_STATE_RECORD_BYTES];
    ota_state_record_t selected = {0};
    const ota_state_record_t first = make_record(41U, OTA_STATE_TRIAL, 1U, 1024U);
    const ota_state_record_t second = make_record(42U, OTA_STATE_TRIAL, 2U, 2048U);

    ota_state_record_encode(&first, primary);
    ota_state_record_encode(&second, secondary);
    secondary[OTA_STATE_OFFSET_COMMIT_MARKER] ^= 0x01U;
    assert(ota_state_select_newest(primary, secondary, &selected)
           == OTA_STATE_SELECT_PRIMARY);
    assert(selected.generation == 41U);

    ota_state_record_encode(&second, secondary);
    secondary[OTA_STATE_OFFSET_COPY_OFFSET] ^= 0x01U;
    assert(ota_state_select_newest(primary, secondary, &selected)
           == OTA_STATE_SELECT_PRIMARY);
    assert(selected.generation == 41U);

    primary[OTA_STATE_OFFSET_MAGIC] = 0U;
    assert(ota_state_select_newest(primary, secondary, &selected)
           == OTA_STATE_SELECT_RECOVERY);
}

static void test_only_two_erased_records_are_uninitialized(void)
{
    uint8_t primary[OTA_STATE_RECORD_BYTES];
    uint8_t secondary[OTA_STATE_RECORD_BYTES];
    ota_state_record_t selected = {0};

    memset(primary, 0xFF, sizeof(primary));
    memset(secondary, 0xFF, sizeof(secondary));
    assert(ota_state_select_newest(primary, secondary, &selected)
           == OTA_STATE_SELECT_UNINITIALIZED);
}

static void test_nonempty_invalid_records_require_recovery(void)
{
    uint8_t primary[OTA_STATE_RECORD_BYTES];
    uint8_t secondary[OTA_STATE_RECORD_BYTES];
    ota_state_record_t selected = {0};

    memset(primary, 0xFF, sizeof(primary));
    memset(secondary, 0xFF, sizeof(secondary));
    secondary[OTA_STATE_OFFSET_MAGIC] = 0U;
    assert(ota_state_select_newest(primary, secondary, &selected)
           == OTA_STATE_SELECT_RECOVERY);

    primary[OTA_STATE_OFFSET_MAGIC] = 0U;
    assert(ota_state_select_newest(primary, secondary, &selected)
           == OTA_STATE_SELECT_RECOVERY);
}

static void test_state_transitions_reject_illegal_progression(void)
{
    ota_state_record_t normal = make_record(3U, OTA_STATE_NORMAL, 0U, 0U);
    ota_state_record_t next = {0};

    assert(ota_state_transition(&normal, OTA_STATE_TRIAL, &next)
           == OTA_STATE_TRANSITION_ILLEGAL);
    assert(ota_state_transition(&normal, OTA_STATE_PENDING_INSTALL, &next)
           == OTA_STATE_TRANSITION_OK);
    assert(next.generation == 4U);
    assert(next.state == OTA_STATE_PENDING_INSTALL);
    assert(next.copy_offset == 0U);
}

static void test_trial_failure_creates_a_new_durable_record_and_torn_write_falls_back(void)
{
    const ota_state_record_t current = make_record(10U, OTA_STATE_TRIAL, 0U, 4096U);
    ota_state_record_t next = {0};
    ota_state_record_t selected = {0};
    uint8_t primary[OTA_STATE_RECORD_BYTES];
    uint8_t alternate[OTA_STATE_RECORD_BYTES];

    assert(ota_state_prepare_trial_failure(&current, &next)
           == OTA_STATE_TRIAL_CONTINUE);
    assert(current.generation == 10U);
    assert(current.trial_count == 0U);
    assert(current.state == OTA_STATE_TRIAL);
    assert(next.generation == 11U);
    assert(next.trial_count == 1U);
    assert(next.state == OTA_STATE_TRIAL);
    assert(next.copy_offset == current.copy_offset);
    assert(ota_state_transition(&current, OTA_STATE_ROLLBACK_REQUIRED, &selected)
           == OTA_STATE_TRANSITION_ILLEGAL);

    ota_state_record_encode(&current, primary);
    ota_state_record_encode(&next, alternate);
    assert(ota_state_select_newest(primary, alternate, &selected)
           == OTA_STATE_SELECT_SECONDARY);
    assert(selected.generation == next.generation);
    assert(selected.trial_count == 1U);

    alternate[OTA_STATE_OFFSET_COMMIT_MARKER] ^= 0x01U;
    assert(ota_state_select_newest(primary, alternate, &selected)
           == OTA_STATE_SELECT_PRIMARY);
    assert(selected.generation == current.generation);
    assert(selected.trial_count == current.trial_count);
}

static void test_trial_limit_and_counter_overflow_create_rollback_records(void)
{
    const ota_state_record_t at_limit = make_record(10U, OTA_STATE_TRIAL,
                                                     OTA_STATE_MAX_TRIALS - 1U, 0U);
    const ota_state_record_t overflow = make_record(11U, OTA_STATE_TRIAL,
                                                     UINT32_MAX, 0U);
    ota_state_record_t next = {0};

    assert(ota_state_prepare_trial_failure(&at_limit, &next)
           == OTA_STATE_TRIAL_ROLLBACK_REQUIRED);
    assert(next.generation == at_limit.generation + 1U);
    assert(next.trial_count == OTA_STATE_MAX_TRIALS);
    assert(next.state == OTA_STATE_ROLLBACK_REQUIRED);
    assert(ota_state_prepare_trial_failure(&overflow, &next)
           == OTA_STATE_TRIAL_ROLLBACK_REQUIRED);
    assert(next.generation == overflow.generation + 1U);
    assert(next.trial_count == UINT32_MAX);
    assert(next.state == OTA_STATE_ROLLBACK_REQUIRED);
}

int main(void)
{
    test_newest_valid_state_record_is_selected();
    test_torn_or_corrupt_newest_record_falls_back_to_older_valid_record();
    test_only_two_erased_records_are_uninitialized();
    test_nonempty_invalid_records_require_recovery();
    test_state_transitions_reject_illegal_progression();
    test_trial_failure_creates_a_new_durable_record_and_torn_write_falls_back();
    test_trial_limit_and_counter_overflow_create_rollback_records();
    puts("ota state: PASS");
    return 0;
}
