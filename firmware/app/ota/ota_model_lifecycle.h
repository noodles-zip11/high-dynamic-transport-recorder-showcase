#ifndef TRANSPORT_RECORDER_OTA_MODEL_LIFECYCLE_H
#define TRANSPORT_RECORDER_OTA_MODEL_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

#include "ai_runtime.h"
#include "ota_download_service.h"
#include "ota_model_package.h"
#include "ota_slots.h"

#define OTA_MODEL_STATE_RECORD_BYTES UINT32_C(160)
#define OTA_MODEL_STATE_COMMIT_MARKER UINT32_C(0x4C444F4D) /* "MODL" */
#define OTA_MODEL_STATE_MAGIC UINT32_C(0x53544D4D) /* "MMTS" */
#define OTA_MODEL_STATE_FORMAT_VERSION UINT16_C(1)
#define OTA_MODEL_STATE_MAX_BANKS UINT8_C(2)

typedef int (*ota_model_slot_erase_fn)(void *context, ota_model_slot_t slot,
                                       uint32_t offset, uint32_t length);
typedef int (*ota_model_slot_write_fn)(void *context, ota_model_slot_t slot,
                                       uint32_t offset, const uint8_t *data,
                                       uint32_t length);
typedef int (*ota_model_slot_read_fn)(void *context, ota_model_slot_t slot,
                                      uint32_t offset, uint8_t *data,
                                      uint32_t length);
typedef int (*ota_model_state_erase_fn)(void *context, uint32_t offset,
                                        uint32_t length);
typedef int (*ota_model_state_write_fn)(void *context, uint32_t offset,
                                        const uint8_t *data, uint32_t length);
typedef int (*ota_model_state_read_fn)(void *context, uint32_t offset,
                                       uint8_t *data, uint32_t length);
typedef int (*ota_model_state_bank_erase_fn)(void *context, uint8_t bank,
                                             uint32_t offset, uint32_t length);
typedef int (*ota_model_state_bank_write_fn)(void *context, uint8_t bank,
                                             uint32_t offset,
                                             const uint8_t *data,
                                             uint32_t length);
typedef int (*ota_model_state_bank_read_fn)(void *context, uint8_t bank,
                                            uint32_t offset, uint8_t *data,
                                            uint32_t length);
typedef int (*ota_model_runtime_prepare_fn)(const ai_model_t *model,
                                            void *context);
typedef int (*ota_model_runtime_publish_fn)(const ai_model_t *model,
                                            void *context);
/* A prepare callback must not retain or alter a transition gate when it
 * returns an error; only a successful prepare may later be aborted. */
typedef void (*ota_model_runtime_abort_fn)(void *context);
typedef void (*ota_model_runtime_quarantine_fn)(void *context);

typedef struct
{
    ota_model_slot_erase_fn erase_slot;
    ota_model_slot_write_fn write_slot;
    ota_model_slot_read_fn read_slot;
    ota_model_state_erase_fn erase_state;
    ota_model_state_write_fn write_state;
    ota_model_state_read_fn read_state;
    ota_model_state_bank_erase_fn erase_state_bank;
    ota_model_state_bank_write_fn write_state_bank;
    ota_model_state_bank_read_fn read_state_bank;
    void *context;
    uint32_t slot_capacity_bytes;
    /* Capacity of each append-only state bank. Zero state_bank_count means
     * the legacy single-bank layout; two banks enable power-loss-safe GC. */
    uint32_t state_capacity_bytes;
    uint32_t erase_block_bytes;
    uint8_t state_bank_count;
} ota_model_storage_t;

typedef enum
{
    OTA_MODEL_LIFECYCLE_OK = 0,
    OTA_MODEL_LIFECYCLE_INVALID_ARGUMENT,
    OTA_MODEL_LIFECYCLE_STORAGE_ERROR,
    OTA_MODEL_LIFECYCLE_STATE_FULL,
    OTA_MODEL_LIFECYCLE_NO_VALID_MODEL,
} ota_model_lifecycle_status_t;

typedef struct
{
    ota_model_storage_t storage;
    ota_download_storage_t download_storage;
    ota_download_service_t download;
    ota_model_slot_t active_slot;
    ota_model_slot_t target_slot;
    uint8_t state_bank;
    uint8_t authoritative_state_bank;
    uint8_t authoritative_state_valid;
    uint32_t active_generation;
    uint32_t next_state_offset;
    uint32_t state_bank_next_offset[OTA_MODEL_STATE_MAX_BANKS];
    uint8_t mounted;
    uint8_t active_valid;
    uint8_t state_full;
    uint8_t activation_uncertain;
    uint8_t last_package_status;
    ota_model_package_info_t active_info;
    ota_model_package_info_t pending_info;
    ota_model_package_scratch_t package_scratch;
    ota_model_runtime_prepare_fn prepare_runtime_model;
    ota_model_runtime_publish_fn publish_runtime_model;
    ota_model_runtime_abort_fn abort_runtime_model;
    ota_model_runtime_quarantine_fn quarantine_runtime_model;
    void *runtime_activation_context;
    /* One immutable runtime buffer per physical slot. When a live consumer
     * holds a model pointer, install the prepare/publish/abort/quarantine
     * callbacks before accepting updates so inactive-slot reuse is serialized
     * with inference and ambiguous commits can quarantine the runtime. */
    ai_model_storage_t slot_model_storage[2];
    ai_model_t slot_models[2];
} ota_model_lifecycle_t;

bool ota_model_lifecycle_init(ota_model_lifecycle_t *lifecycle,
                              const ota_model_storage_t *storage);
ota_download_status_t ota_model_lifecycle_begin(ota_model_lifecycle_t *lifecycle,
                                                uint32_t package_length);
ota_download_status_t ota_model_lifecycle_write(ota_model_lifecycle_t *lifecycle,
                                                uint32_t offset,
                                                const uint8_t *data,
                                                uint32_t length,
                                                uint32_t crc32);
void ota_model_lifecycle_query(const ota_model_lifecycle_t *lifecycle,
                               ota_download_progress_t *progress);
ota_download_status_t ota_model_lifecycle_finalize(
    ota_model_lifecycle_t *lifecycle);
ota_download_status_t ota_model_lifecycle_cancel(
    ota_model_lifecycle_t *lifecycle);
ota_model_slot_t ota_model_lifecycle_get_active_slot(
    const ota_model_lifecycle_t *lifecycle);
ota_model_slot_t ota_model_lifecycle_get_target_slot(
    const ota_model_lifecycle_t *lifecycle);
bool ota_model_lifecycle_has_valid_model(
    const ota_model_lifecycle_t *lifecycle);
const ai_model_t *ota_model_lifecycle_get_active_model(
    const ota_model_lifecycle_t *lifecycle);
/* Returns the validated OTA model, or the firmware rule/fallback model;
 * returns NULL while activation is uncertain and the runtime is quarantined. */
const ai_model_t *ota_model_lifecycle_get_runtime_model(
    const ota_model_lifecycle_t *lifecycle);
void ota_model_lifecycle_set_runtime_activation(
    ota_model_lifecycle_t *lifecycle,
    ota_model_runtime_prepare_fn prepare_runtime_model,
    ota_model_runtime_publish_fn publish_runtime_model,
    ota_model_runtime_abort_fn abort_runtime_model,
    ota_model_runtime_quarantine_fn quarantine_runtime_model,
    void *context);
bool ota_model_lifecycle_is_activation_uncertain(
    const ota_model_lifecycle_t *lifecycle);
const ota_model_package_info_t *ota_model_lifecycle_get_active_info(
    const ota_model_lifecycle_t *lifecycle);

#endif
