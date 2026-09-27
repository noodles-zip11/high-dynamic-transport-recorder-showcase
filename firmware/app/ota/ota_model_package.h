#ifndef TRANSPORT_RECORDER_OTA_MODEL_PACKAGE_H
#define TRANSPORT_RECORDER_OTA_MODEL_PACKAGE_H

#include <stdint.h>

#include "ai_runtime.h"

#define OTA_MODEL_PACKAGE_MAGIC UINT32_C(0x444D5254) /* "TRMD" */
#define OTA_MODEL_PACKAGE_FORMAT_VERSION UINT16_C(1)
#define OTA_MODEL_PACKAGE_HEADER_BYTES UINT32_C(160)
#define OTA_MODEL_PACKAGE_QUANTIZATION_INT8 UINT8_C(1)
#define OTA_MODEL_PACKAGE_GOLDEN_RECORD_BYTES UINT32_C(40)
#define OTA_MODEL_PACKAGE_MAX_MODEL_BYTES UINT32_C(2048)
#define OTA_MODEL_PACKAGE_MAX_GOLDEN_VECTORS UINT16_C(8)
#define OTA_MODEL_PACKAGE_MAX_GOLDEN_BYTES \
    (OTA_MODEL_PACKAGE_GOLDEN_RECORD_BYTES * OTA_MODEL_PACKAGE_MAX_GOLDEN_VECTORS)
#define OTA_MODEL_PACKAGE_MAX_BYTES \
    (OTA_MODEL_PACKAGE_HEADER_BYTES + OTA_MODEL_PACKAGE_MAX_MODEL_BYTES \
     + OTA_MODEL_PACKAGE_MAX_GOLDEN_BYTES)

/* Fixed offsets are part of the on-QSPI contract; do not use C struct packing. */
#define OTA_MODEL_PACKAGE_OFFSET_MAGIC UINT32_C(0)
#define OTA_MODEL_PACKAGE_OFFSET_FORMAT_VERSION UINT32_C(4)
#define OTA_MODEL_PACKAGE_OFFSET_HEADER_BYTES UINT32_C(6)
#define OTA_MODEL_PACKAGE_OFFSET_PACKAGE_LENGTH UINT32_C(8)
#define OTA_MODEL_PACKAGE_OFFSET_MODEL_VERSION UINT32_C(12)
#define OTA_MODEL_PACKAGE_OFFSET_RUNTIME_VERSION UINT32_C(14)
#define OTA_MODEL_PACKAGE_OFFSET_FEATURE_VERSION UINT32_C(16)
#define OTA_MODEL_PACKAGE_OFFSET_FEATURE_COUNT UINT32_C(18)
#define OTA_MODEL_PACKAGE_OFFSET_HIDDEN_UNITS UINT32_C(19)
#define OTA_MODEL_PACKAGE_OFFSET_CLASS_COUNT UINT32_C(20)
#define OTA_MODEL_PACKAGE_OFFSET_QUANTIZATION UINT32_C(21)
#define OTA_MODEL_PACKAGE_OFFSET_INPUT_SHAPE_RANK UINT32_C(22)
#define OTA_MODEL_PACKAGE_OFFSET_INPUT_SHAPE UINT32_C(24)
#define OTA_MODEL_PACKAGE_OFFSET_GOLDEN_COUNT UINT32_C(26)
#define OTA_MODEL_PACKAGE_OFFSET_MODEL_OFFSET UINT32_C(28)
#define OTA_MODEL_PACKAGE_OFFSET_MODEL_LENGTH UINT32_C(32)
#define OTA_MODEL_PACKAGE_OFFSET_GOLDEN_OFFSET UINT32_C(36)
#define OTA_MODEL_PACKAGE_OFFSET_GOLDEN_LENGTH UINT32_C(40)
#define OTA_MODEL_PACKAGE_OFFSET_MODEL_PAYLOAD_CRC32 UINT32_C(44)
#define OTA_MODEL_PACKAGE_OFFSET_MODEL_SHA256 UINT32_C(48)
#define OTA_MODEL_PACKAGE_OFFSET_GOLDEN_INPUT_SHA256 UINT32_C(80)
#define OTA_MODEL_PACKAGE_OFFSET_GOLDEN_OUTPUT_SHA256 UINT32_C(112)
#define OTA_MODEL_PACKAGE_OFFSET_HEADER_CRC32 UINT32_C(144)

typedef int (*ota_model_package_read_fn)(void *context, uint32_t offset,
                                         uint8_t *data, uint32_t length);

typedef enum
{
    OTA_MODEL_PACKAGE_STATUS_OK = 0,
    OTA_MODEL_PACKAGE_STATUS_INVALID_ARGUMENT,
    OTA_MODEL_PACKAGE_STATUS_STORAGE_ERROR,
    OTA_MODEL_PACKAGE_STATUS_INVALID_HEADER,
    OTA_MODEL_PACKAGE_STATUS_INVALID_CONTRACT,
    OTA_MODEL_PACKAGE_STATUS_INVALID_MODEL,
    OTA_MODEL_PACKAGE_STATUS_INVALID_GOLDEN,
} ota_model_package_status_t;

typedef struct
{
    uint16_t model_version;
    uint16_t runtime_version;
    uint16_t feature_version;
    uint8_t feature_count;
    uint8_t hidden_units;
    uint8_t class_count;
    uint8_t quantization;
    uint16_t input_shape;
    uint16_t golden_vector_count;
    uint32_t package_length;
    uint32_t model_length;
    uint32_t golden_length;
    /* CRC32 of the serialized model payload, distinct from ai_model_t CRC. */
    uint32_t model_payload_crc32;
    uint8_t model_sha256[32];
    uint8_t golden_input_sha256[32];
    uint8_t golden_output_sha256[32];
} ota_model_package_info_t;

typedef struct
{
    uint8_t model_bytes[OTA_MODEL_PACKAGE_MAX_MODEL_BYTES];
    uint8_t golden_bytes[OTA_MODEL_PACKAGE_MAX_GOLDEN_BYTES];
    /* Kept by the caller so validation does not put model tensors on a task stack. */
    ai_model_storage_t model_storage;
    ai_model_t model;
} ota_model_package_scratch_t;

ota_model_package_status_t ota_model_package_validate(
    ota_model_package_read_fn read,
    void *context,
    uint32_t package_length,
    ota_model_package_scratch_t *scratch,
    ota_model_package_info_t *info);

#endif
