#include <stddef.h>

#include "memory_layout.h"
#include "ota_slots.h"

static const ota_slot_region_t ota_slots[] = {
    [OTA_SLOT_CANDIDATE] = {
        .offset = TRANSPORT_OTA_QSPI_CANDIDATE_OFFSET,
        .size = TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
    },
    [OTA_SLOT_RECOVERY] = {
        .offset = TRANSPORT_OTA_QSPI_RECOVERY_OFFSET,
        .size = TRANSPORT_OTA_QSPI_RECOVERY_SIZE_BYTES,
    },
    [OTA_SLOT_MODEL_A] = {
        .offset = TRANSPORT_OTA_QSPI_MODEL_A_OFFSET,
        .size = TRANSPORT_OTA_QSPI_MODEL_SLOT_CAPACITY_BYTES,
    },
    [OTA_SLOT_MODEL_B] = {
        .offset = TRANSPORT_OTA_QSPI_MODEL_B_OFFSET,
        .size = TRANSPORT_OTA_QSPI_MODEL_SLOT_CAPACITY_BYTES,
    },
};

static int ota_slots_region_fits_qspi(const ota_slot_region_t *region)
{
    const uint64_t end = (uint64_t)region->offset + region->size;

    return region->size != 0U
           && region->offset % TRANSPORT_OTA_QSPI_ERASE_BLOCK_SIZE_BYTES == 0U
           && region->size % TRANSPORT_OTA_QSPI_ERASE_BLOCK_SIZE_BYTES == 0U
           && end <= TRANSPORT_OTA_QSPI_SIZE_BYTES;
}

static int ota_slots_regions_do_not_overlap(const ota_slot_region_t *first,
                                            const ota_slot_region_t *second)
{
    const uint64_t first_end = (uint64_t)first->offset + first->size;
    const uint64_t second_end = (uint64_t)second->offset + second->size;

    return first_end <= second->offset || second_end <= first->offset;
}

bool ota_slots_validate_layout(void)
{
    const ota_slot_region_t metadata = {
        .offset = TRANSPORT_OTA_QSPI_METADATA_OFFSET,
        .size = TRANSPORT_OTA_QSPI_METADATA_SIZE_BYTES,
    };
    uint32_t first;
    uint32_t second;

    if (!ota_slots_region_fits_qspi(&metadata))
    {
        return false;
    }
    for (first = 0U; first < sizeof(ota_slots) / sizeof(ota_slots[0]); first++)
    {
        if (!ota_slots_region_fits_qspi(&ota_slots[first])
            || !ota_slots_regions_do_not_overlap(&metadata, &ota_slots[first]))
        {
            return false;
        }
        for (second = first + 1U; second < sizeof(ota_slots) / sizeof(ota_slots[0]); second++)
        {
            if (!ota_slots_regions_do_not_overlap(&ota_slots[first], &ota_slots[second]))
            {
                return false;
            }
        }
    }
    return true;
}

bool ota_slots_get(ota_slot_id_t slot, ota_slot_region_t *region)
{
    if (region == NULL || slot < OTA_SLOT_CANDIDATE || slot > OTA_SLOT_MODEL_B)
    {
        return false;
    }
    *region = ota_slots[slot];
    return true;
}

bool ota_slot_contains_range(const ota_slot_region_t *region,
                             uint32_t offset, uint32_t length)
{
    const uint64_t region_end = region == NULL ? 0U : (uint64_t)region->offset + region->size;
    const uint64_t range_end = (uint64_t)offset + length;

    return region != NULL && length != 0U && offset >= region->offset && range_end <= region_end;
}

bool ota_slots_get_inactive_model(ota_model_slot_t active,
                                  ota_slot_region_t *inactive_region)
{
    if (active == OTA_MODEL_SLOT_A)
    {
        return ota_slots_get(OTA_SLOT_MODEL_B, inactive_region);
    }
    if (active == OTA_MODEL_SLOT_B)
    {
        return ota_slots_get(OTA_SLOT_MODEL_A, inactive_region);
    }
    return false;
}
