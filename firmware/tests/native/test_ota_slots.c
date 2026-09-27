#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "memory_layout.h"
#include "ota_slots.h"

static void test_candidate_recovery_and_model_slots_are_non_overlapping(void)
{
    ota_slot_region_t candidate = {0};
    ota_slot_region_t recovery = {0};
    ota_slot_region_t model_a = {0};
    ota_slot_region_t model_b = {0};

    assert(ota_slots_validate_layout());
    assert(ota_slots_get(OTA_SLOT_CANDIDATE, &candidate));
    assert(ota_slots_get(OTA_SLOT_RECOVERY, &recovery));
    assert(ota_slots_get(OTA_SLOT_MODEL_A, &model_a));
    assert(ota_slots_get(OTA_SLOT_MODEL_B, &model_b));
    assert(candidate.offset == TRANSPORT_OTA_QSPI_CANDIDATE_OFFSET);
    assert(recovery.offset == TRANSPORT_OTA_QSPI_RECOVERY_OFFSET);
    assert(candidate.offset + candidate.size <= recovery.offset);
    assert(recovery.offset + recovery.size <= model_a.offset);
    assert(model_a.offset + model_a.size <= model_b.offset);
}

static void test_slot_ranges_reject_out_of_bounds_requests(void)
{
    ota_slot_region_t candidate = {0};
    ota_slot_region_t recovery = {0};
    ota_slot_region_t model_b = {0};

    assert(ota_slots_get(OTA_SLOT_CANDIDATE, &candidate));
    assert(ota_slots_get(OTA_SLOT_RECOVERY, &recovery));
    assert(ota_slots_get(OTA_SLOT_MODEL_B, &model_b));
    assert(ota_slot_contains_range(&candidate, candidate.offset, candidate.size));
    assert(!ota_slot_contains_range(&candidate, candidate.offset, candidate.size + 1U));
    assert(ota_slot_contains_range(&recovery, recovery.offset + recovery.size - 1U, 1U));
    assert(!ota_slot_contains_range(&recovery, recovery.offset + recovery.size, 1U));
    assert(ota_slot_contains_range(&model_b, model_b.offset, model_b.size));
    assert(!ota_slot_contains_range(&model_b, model_b.offset - 1U, 1U));
}

static void test_inactive_model_slot_alternates_between_a_and_b(void)
{
    ota_slot_region_t inactive = {0};

    assert(ota_slots_get_inactive_model(OTA_MODEL_SLOT_A, &inactive));
    assert(inactive.offset == TRANSPORT_OTA_QSPI_MODEL_B_OFFSET);
    assert(ota_slots_get_inactive_model(OTA_MODEL_SLOT_B, &inactive));
    assert(inactive.offset == TRANSPORT_OTA_QSPI_MODEL_A_OFFSET);
    assert(!ota_slots_get_inactive_model(OTA_MODEL_SLOT_INVALID, &inactive));
}

int main(void)
{
    test_candidate_recovery_and_model_slots_are_non_overlapping();
    test_slot_ranges_reject_out_of_bounds_requests();
    test_inactive_model_slot_alternates_between_a_and_b();
    puts("ota slots: PASS");
    return 0;
}
