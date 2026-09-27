#ifndef TRANSPORT_RECORDER_OTA_SLOTS_H
#define TRANSPORT_RECORDER_OTA_SLOTS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    OTA_SLOT_CANDIDATE = 0,
    OTA_SLOT_RECOVERY,
    OTA_SLOT_MODEL_A,
    OTA_SLOT_MODEL_B,
} ota_slot_id_t;

typedef enum
{
    OTA_MODEL_SLOT_A = 0,
    OTA_MODEL_SLOT_B,
    OTA_MODEL_SLOT_INVALID,
} ota_model_slot_t;

typedef struct
{
    uint32_t offset;
    uint32_t size;
} ota_slot_region_t;

bool ota_slots_validate_layout(void);
bool ota_slots_get(ota_slot_id_t slot, ota_slot_region_t *region);
bool ota_slot_contains_range(const ota_slot_region_t *region,
                             uint32_t offset, uint32_t length);
bool ota_slots_get_inactive_model(ota_model_slot_t active,
                                  ota_slot_region_t *inactive_region);

#endif
