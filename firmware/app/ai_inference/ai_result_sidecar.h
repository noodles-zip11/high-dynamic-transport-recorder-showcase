#ifndef TRANSPORT_RECORDER_AI_RESULT_SIDECAR_H
#define TRANSPORT_RECORDER_AI_RESULT_SIDECAR_H

#include <stdint.h>

#include "ai_result.h"

#define AI_RESULT_SIDECAR_RECORD_BYTES UINT32_C(128)
#define AI_RESULT_SIDECAR_FORMAT_VERSION UINT16_C(2)
#define AI_RESULT_SIDECAR_HEADER_BYTES UINT16_C(60)
#define AI_RESULT_SIDECAR_COMMIT_MARKER UINT32_C(0)

#define AI_RESULT_SIDECAR_OK 0
#define AI_RESULT_SIDECAR_INVALID_ARGUMENT (-1)
#define AI_RESULT_SIDECAR_NOT_FOUND (-2)
#define AI_RESULT_SIDECAR_STORAGE_ERROR (-3)

typedef int (*ai_result_sidecar_read_fn)(void *context,
                                         uint32_t offset,
                                         uint8_t *data,
                                         uint32_t length);
typedef int (*ai_result_sidecar_write_fn)(void *context,
                                          uint32_t offset,
                                          const uint8_t *data,
                                          uint32_t length);
typedef int (*ai_result_sidecar_erase_fn)(void *context,
                                          uint32_t offset,
                                          uint32_t length);

typedef struct
{
    ai_result_sidecar_read_fn read;
    ai_result_sidecar_write_fn write;
    ai_result_sidecar_erase_fn erase;
    void *context;
    uint32_t capacity_bytes;
    uint32_t erase_sector_bytes;
} ai_result_sidecar_storage_t;

typedef struct
{
    ai_result_sidecar_storage_t storage;
    uint32_t slot_count;
    uint32_t slots_per_sector;
    uint32_t next_slot;
    uint32_t next_sequence;
    uint32_t next_generation;
    uint32_t latest_write_generation;
    uint32_t latest_write_slot;
    uint32_t latest_result_slot;
    ai_result_t latest;
    int latest_valid;
} ai_result_sidecar_t;

int ai_result_sidecar_mount(ai_result_sidecar_t *sidecar,
                            const ai_result_sidecar_storage_t *storage);
int ai_result_sidecar_append(ai_result_sidecar_t *sidecar,
                             const ai_result_t *result);
int ai_result_sidecar_get_latest(const ai_result_sidecar_t *sidecar,
                                 ai_result_t *result);
int ai_result_sidecar_get_event(const ai_result_sidecar_t *sidecar,
                                uint32_t event_id,
                                ai_result_t *result);

#endif
