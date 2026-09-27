#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "bootloader_action.h"
#include "bootloader_app.h"
#include "bootloader_copy.h"
#include "bootloader_flash_policy.h"
#include "bootloader_qspi_profile.h"
#include "bootloader_state_store.h"
#include "bootloader_version.h"
#include "memory_layout.h"
#include "ota_crc32.h"
#include "ota_sha256.h"

typedef struct
{
    uint8_t application_flash[512];
    uint8_t primary_state[64];
    uint8_t secondary_state[64];
    uint8_t qspi[512];
    uint32_t erase_calls;
    uint32_t program_calls;
    uint32_t last_erase_address;
    uint32_t last_erase_length;
    uint32_t last_program_address;
    uint32_t last_program_length;
    int saw_unaligned_operation;
    int fail_application_read;
    int corrupt_after_application_read;
} fake_storage_t;

static int fake_flash_read(void *context, uint32_t address, uint8_t *data,
                           uint32_t length)
{
    fake_storage_t *storage = context;

    if (address >= TRANSPORT_OTA_APPLICATION_BASE
        && address - TRANSPORT_OTA_APPLICATION_BASE <= sizeof(storage->application_flash)
        && length <= sizeof(storage->application_flash)
        - (address - TRANSPORT_OTA_APPLICATION_BASE))
    {
        if (storage->fail_application_read)
        {
            return 0;
        }
        memcpy(data, &storage->application_flash[address - TRANSPORT_OTA_APPLICATION_BASE],
               length);
        if (storage->corrupt_after_application_read)
        {
            storage->application_flash[0] ^= 0x01U;
            storage->corrupt_after_application_read = 0;
        }
        return 1;
    }
    if (address == TRANSPORT_OTA_STATE_PRIMARY_BASE && length == OTA_STATE_RECORD_BYTES)
    {
        memcpy(data, storage->primary_state, length);
        return 1;
    }
    if (address == TRANSPORT_OTA_STATE_SECONDARY_BASE && length == OTA_STATE_RECORD_BYTES)
    {
        memcpy(data, storage->secondary_state, length);
        return 1;
    }
    return 0;
}

static uint8_t *fake_state_bytes_at(fake_storage_t *storage, uint32_t address)
{
    if (address >= TRANSPORT_OTA_STATE_PRIMARY_BASE
        && address < TRANSPORT_OTA_STATE_PRIMARY_BASE + sizeof(storage->primary_state))
    {
        return &storage->primary_state[address - TRANSPORT_OTA_STATE_PRIMARY_BASE];
    }
    if (address >= TRANSPORT_OTA_STATE_SECONDARY_BASE
        && address < TRANSPORT_OTA_STATE_SECONDARY_BASE + sizeof(storage->secondary_state))
    {
        return &storage->secondary_state[address - TRANSPORT_OTA_STATE_SECONDARY_BASE];
    }
    return NULL;
}

static int fake_qspi_read(void *context, uint32_t address, uint8_t *data,
                          uint32_t length)
{
    fake_storage_t *storage = context;

    if (address > sizeof(storage->qspi) || length > sizeof(storage->qspi) - address)
    {
        return 0;
    }
    memcpy(data, &storage->qspi[address], length);
    return 1;
}

static int fake_erase(void *context, uint32_t address, uint32_t length)
{
    fake_storage_t *storage = context;

    storage->erase_calls++;
    storage->last_erase_address = address;
    storage->last_erase_length = length;
    if (address % BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES != 0U
        || length != BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES)
    {
        storage->saw_unaligned_operation = 1;
    }
    if (address == TRANSPORT_OTA_STATE_PRIMARY_BASE
        || address == TRANSPORT_OTA_STATE_SECONDARY_BASE)
    {
        memset(fake_state_bytes_at(storage, address), 0xFF,
               address == TRANSPORT_OTA_STATE_PRIMARY_BASE
                   ? sizeof(storage->primary_state)
                   : sizeof(storage->secondary_state));
    }
    if (address == TRANSPORT_OTA_APPLICATION_BASE
        && length == BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES)
    {
        memset(storage->application_flash, 0xFF, sizeof(storage->application_flash));
    }
    return 1;
}

static int fake_program(void *context, uint32_t address, const uint8_t *data,
                        uint32_t length)
{
    fake_storage_t *storage = context;

    storage->program_calls++;
    storage->last_program_address = address;
    storage->last_program_length = length;
    if (address % BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES != 0U
        || length != BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES)
    {
        storage->saw_unaligned_operation = 1;
    }
    if (address >= TRANSPORT_OTA_STATE_PRIMARY_BASE
        && address + length <= TRANSPORT_OTA_STATE_PRIMARY_BASE + sizeof(storage->primary_state))
    {
        memcpy(fake_state_bytes_at(storage, address), data, length);
    }
    if (address >= TRANSPORT_OTA_STATE_SECONDARY_BASE
        && address + length <= TRANSPORT_OTA_STATE_SECONDARY_BASE + sizeof(storage->secondary_state))
    {
        memcpy(fake_state_bytes_at(storage, address), data, length);
    }
    if (address >= TRANSPORT_OTA_APPLICATION_BASE
        && address - TRANSPORT_OTA_APPLICATION_BASE <= sizeof(storage->application_flash)
        && length <= sizeof(storage->application_flash)
        - (address - TRANSPORT_OTA_APPLICATION_BASE))
    {
        memcpy(&storage->application_flash[address - TRANSPORT_OTA_APPLICATION_BASE],
               data, length);
    }
    return 1;
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

static void put_identifier(uint8_t *data, const char *value)
{
    const size_t length = strlen(value);

    assert(length <= OTA_PACKAGE_IDENTIFIER_BYTES);
    memcpy(data, value, length);
}

static void build_valid_qspi_package(fake_storage_t *storage, const char *minimum_version)
{
    static const uint8_t image[] = {0x10U, 0x20U, 0x30U, 0x40U};
    ota_sha256_t sha256;
    uint8_t digest[OTA_SHA256_DIGEST_BYTES];

    memset(storage->qspi, 0, sizeof(storage->qspi));
    memcpy(&storage->qspi[OTA_PACKAGE_OFFSET_MAGIC], "TRFW", 4U);
    put_u16_le(&storage->qspi[OTA_PACKAGE_OFFSET_FORMAT_VERSION], 1U);
    put_u16_le(&storage->qspi[OTA_PACKAGE_OFFSET_HEADER_BYTES],
               OTA_PACKAGE_MANIFEST_BYTES);
    put_identifier(&storage->qspi[OTA_PACKAGE_OFFSET_PRODUCT_ID],
                   OTA_PACKAGE_EXPECTED_PRODUCT_ID);
    put_identifier(&storage->qspi[OTA_PACKAGE_OFFSET_HARDWARE_ID],
                   OTA_PACKAGE_EXPECTED_HARDWARE_ID);
    put_identifier(&storage->qspi[OTA_PACKAGE_OFFSET_FIRMWARE_VERSION], "1.2.3");
    put_identifier(&storage->qspi[OTA_PACKAGE_OFFSET_MINIMUM_BOOTLOADER_VERSION],
                   minimum_version);
    put_u32_le(&storage->qspi[OTA_PACKAGE_OFFSET_TARGET_ADDRESS],
               TRANSPORT_OTA_APPLICATION_BASE);
    put_u32_le(&storage->qspi[OTA_PACKAGE_OFFSET_IMAGE_LENGTH], sizeof(image));
    put_u32_le(&storage->qspi[OTA_PACKAGE_OFFSET_IMAGE_CRC32],
               ota_crc32_compute(image, sizeof(image)));
    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256, image, sizeof(image));
    ota_sha256_final(&sha256, digest);
    memcpy(&storage->qspi[OTA_PACKAGE_OFFSET_IMAGE_SHA256], digest, sizeof(digest));
    memcpy(&storage->qspi[OTA_PACKAGE_MANIFEST_BYTES], image, sizeof(image));
}

static bootloader_storage_t make_storage(fake_storage_t *storage)
{
    const bootloader_storage_ops_t flash = {
        .context = storage,
        .read = fake_flash_read,
        .erase = fake_erase,
        .program = fake_program,
    };
    const bootloader_storage_ops_t qspi = {
        .context = storage,
        .read = fake_qspi_read,
        .erase = fake_erase,
        .program = fake_program,
    };
    const bootloader_storage_t result = {
        .internal_flash = flash,
        .qspi = qspi,
    };

    return result;
}

static ota_state_record_t make_state(ota_state_kind_t state, uint32_t trial_count,
                                     uint32_t copy_offset)
{
    const ota_state_record_t result = {
        .generation = 7U,
        .state = state,
        .trial_count = trial_count,
        .copy_offset = copy_offset,
    };

    return result;
}

static void copy_request_set_integrity(bootloader_copy_request_t *request,
                                       const uint8_t *image, uint32_t image_length)
{
    ota_sha256_t sha256;

    assert(request != NULL);
    request->expected_image_crc32 = ota_crc32_compute(image, image_length);
    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256, image, image_length);
    ota_sha256_final(&sha256, request->expected_image_sha256);
}

static void test_application_vectors_require_aligned_ram_msp_and_thumb_reset(void)
{
    fake_storage_t fake = {0};
    const bootloader_storage_t storage = make_storage(&fake);
    bootloader_application_vectors_t vectors = {0};

    put_u32_le(&fake.application_flash[0], UINT32_C(0x24001000));
    put_u32_le(&fake.application_flash[4], TRANSPORT_OTA_APPLICATION_BASE + 0x101U);
    assert(bootloader_application_vectors_read_and_validate(&storage, &vectors));
    assert(vectors.initial_msp == UINT32_C(0x24001000));
    assert(vectors.reset_handler == TRANSPORT_OTA_APPLICATION_BASE + 0x101U);

    put_u32_le(&fake.application_flash[0], UINT32_C(0x24001004));
    assert(!bootloader_application_vectors_read_and_validate(&storage, &vectors));

    put_u32_le(&fake.application_flash[0], UINT32_C(0x20001000));
    assert(!bootloader_application_vectors_read_and_validate(&storage, &vectors));

    put_u32_le(&fake.application_flash[0], UINT32_C(0x24001000));
    put_u32_le(&fake.application_flash[4], TRANSPORT_OTA_APPLICATION_BASE + 0x100U);
    assert(!bootloader_application_vectors_read_and_validate(&storage, &vectors));

    put_u32_le(&fake.application_flash[4], UINT32_C(0x08010001));
    assert(!bootloader_application_vectors_read_and_validate(&storage, &vectors));
}

static void test_flash_mutation_range_rejects_unsigned_wraparound(void)
{
    assert(bootloader_flash_range_is_mutable(
        TRANSPORT_OTA_APPLICATION_BASE, BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES));
    assert(bootloader_flash_range_is_mutable(
        TRANSPORT_OTA_APPLICATION_BASE + TRANSPORT_OTA_APPLICATION_SIZE_BYTES
            - BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES,
        BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES));
    /* The application end is also the start of the primary state sector. */
    assert(bootloader_flash_range_is_mutable(
        TRANSPORT_OTA_APPLICATION_BASE + TRANSPORT_OTA_APPLICATION_SIZE_BYTES,
        BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES));
    assert(!bootloader_flash_range_is_mutable(
        TRANSPORT_OTA_APPLICATION_BASE + TRANSPORT_OTA_APPLICATION_SIZE_BYTES
            - BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES / 2U,
        BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES));
    assert(!bootloader_flash_range_is_mutable(
        UINT32_C(0xFFFFFFFF), BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES));
    assert(!bootloader_flash_range_is_mutable(
        TRANSPORT_OTA_APPLICATION_BASE - 1U, BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES));
    assert(bootloader_flash_range_is_mutable(
        TRANSPORT_OTA_STATE_PRIMARY_BASE, BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES));
    assert(!bootloader_flash_range_is_mutable(
        TRANSPORT_OTA_STATE_PRIMARY_BASE + TRANSPORT_OTA_STATE_RECORD_SIZE_BYTES
            - BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES / 2U,
        BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES));
    assert(bootloader_flash_range_is_mutable(
        TRANSPORT_OTA_STATE_SECONDARY_BASE, BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES));
    assert(!bootloader_flash_range_is_mutable(
        TRANSPORT_OTA_STATE_SECONDARY_BASE + TRANSPORT_OTA_STATE_RECORD_SIZE_BYTES,
        BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES));
    assert(!bootloader_flash_range_is_mutable(
        TRANSPORT_OTA_APPLICATION_BASE, 0U));
}

static void test_invalid_candidate_manifest_is_rejected_before_install(void)
{
    fake_storage_t fake = {0};
    const bootloader_storage_t storage = make_storage(&fake);
    ota_package_manifest_t manifest = {0};

    assert(!bootloader_package_validate_from_qspi(
        &storage, TRANSPORT_OTA_QSPI_CANDIDATE_OFFSET,
        TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES, &manifest));
}

static void test_candidate_manifest_requires_a_supported_bootloader_version(void)
{
    fake_storage_t fake = {0};
    const bootloader_storage_t storage = make_storage(&fake);
    ota_package_manifest_t manifest = {0};

    build_valid_qspi_package(&fake, "1.0.0");
    assert(bootloader_package_validate_from_qspi(&storage, 0U, sizeof(fake.qspi),
                                                 &manifest));
    build_valid_qspi_package(&fake, "1.0.1");
    assert(!bootloader_package_validate_from_qspi(&storage, 0U, sizeof(fake.qspi),
                                                  &manifest));
    build_valid_qspi_package(&fake, "not-a-version");
    assert(!bootloader_package_validate_from_qspi(&storage, 0U, sizeof(fake.qspi),
                                                  &manifest));
}

static void test_state_selects_only_safe_boot_actions(void)
{
    bootloader_boot_context_t context = {
        .state_selection = OTA_STATE_SELECT_PRIMARY,
        .state = make_state(OTA_STATE_NORMAL, 0U, 0U),
        .application_is_valid = 1,
        .candidate_is_valid = 1,
        .recovery_is_valid = 1,
    };
    bootloader_boot_decision_t decision = {0};

    assert(bootloader_select_action(&context, &decision));
    assert(decision.action == BOOTLOADER_ACTION_BOOT_APPLICATION);
    assert(!decision.persist_state);

    context.state = make_state(OTA_STATE_PENDING_INSTALL, 0U, 0U);
    assert(bootloader_select_action(&context, &decision));
    assert(decision.action == BOOTLOADER_ACTION_INSTALL_CANDIDATE);

    context.state = make_state(OTA_STATE_INSTALLING, 0U, 256U);
    assert(bootloader_select_action(&context, &decision));
    assert(decision.action == BOOTLOADER_ACTION_INSTALL_CANDIDATE);

    context.state = make_state(OTA_STATE_TRIAL, 0U, 0U);
    assert(bootloader_select_action(&context, &decision));
    assert(decision.action == BOOTLOADER_ACTION_BOOT_TRIAL_APPLICATION);
    assert(decision.persist_state);
    assert(decision.next_state.state == OTA_STATE_TRIAL);
    assert(decision.next_state.trial_count == 1U);

    context.state = make_state(OTA_STATE_TRIAL, OTA_STATE_MAX_TRIALS - 1U, 0U);
    assert(bootloader_select_action(&context, &decision));
    assert(decision.action == BOOTLOADER_ACTION_INSTALL_RECOVERY);
    assert(decision.persist_state);
    assert(decision.next_state.state == OTA_STATE_ROLLBACK_REQUIRED);

    context.state = make_state(OTA_STATE_ROLLBACK_REQUIRED, OTA_STATE_MAX_TRIALS, 0U);
    assert(bootloader_select_action(&context, &decision));
    assert(decision.action == BOOTLOADER_ACTION_INSTALL_RECOVERY);

    context.state_selection = OTA_STATE_SELECT_UNINITIALIZED;
    context.application_is_valid = 1;
    assert(bootloader_select_action(&context, &decision));
    assert(decision.action == BOOTLOADER_ACTION_PROVISION_NORMAL);
    assert(decision.persist_state);
    assert(decision.next_state.state == OTA_STATE_NORMAL);
    assert(decision.next_state.generation == 0U);

    context.state_selection = OTA_STATE_SELECT_RECOVERY;
    assert(bootloader_select_action(&context, &decision));
    assert(decision.action == BOOTLOADER_ACTION_RECOVERY);
    assert(!decision.persist_state);

    context.application_is_valid = 0;
    assert(bootloader_select_action(&context, &decision));
    assert(decision.action == BOOTLOADER_ACTION_RECOVERY);
    assert(!decision.persist_state);
}

static void test_copy_engine_is_bounded_and_reports_state_progress(void)
{
    fake_storage_t fake = {0};
    const bootloader_storage_t storage = make_storage(&fake);
    const ota_state_record_t installing = make_state(OTA_STATE_INSTALLING, 0U, 0U);
    bootloader_copy_request_t request = {
        .qspi_image_offset = 0U,
        .image_length = 64U,
        .maximum_copy_bytes = BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES,
    };
    bootloader_copy_result_t result = {0};
    uint8_t scratch[BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES];

    memset(fake.qspi, 0xA5, sizeof(fake.qspi));
    copy_request_set_integrity(&request, fake.qspi, request.image_length);
    assert(bootloader_copy_engine_run(&storage, &request, &installing, scratch,
                                      sizeof(scratch), &result));
    assert(fake.erase_calls == 1U);
    assert(fake.last_erase_address == TRANSPORT_OTA_APPLICATION_BASE);
    assert(fake.last_erase_length == BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES);
    assert(fake.program_calls == 1U);
    assert(fake.last_program_address == TRANSPORT_OTA_APPLICATION_BASE);
    assert(fake.last_program_length == BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES);
    assert(!fake.saw_unaligned_operation);
    assert(result.next_state.state == OTA_STATE_INSTALLING);
    assert(result.next_state.copy_offset == BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES);

    request.maximum_copy_bytes = BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES - 1U;
    assert(!bootloader_copy_engine_run(&storage, &request, &installing, scratch,
                                       sizeof(scratch), &result));
}

static void test_copy_engine_refuses_readback_errors_or_a_final_integrity_mismatch(void)
{
    fake_storage_t fake = {0};
    const bootloader_storage_t storage = make_storage(&fake);
    const ota_state_record_t installing = make_state(OTA_STATE_INSTALLING, 0U, 0U);
    bootloader_copy_request_t request = {
        .qspi_image_offset = 0U,
        .image_length = BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES,
        .maximum_copy_bytes = BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES,
    };
    bootloader_copy_result_t result = {0};
    uint8_t scratch[BOOTLOADER_INTERNAL_FLASH_PROGRAM_BYTES];

    memset(fake.qspi, 0x3CU, sizeof(fake.qspi));
    copy_request_set_integrity(&request, fake.qspi, request.image_length);
    fake.fail_application_read = 1;
    assert(!bootloader_copy_engine_run(&storage, &request, &installing, scratch,
                                       sizeof(scratch), &result));
    assert(result.next_state.state != OTA_STATE_TRIAL);

    fake = (fake_storage_t){0};
    memset(fake.qspi, 0x3CU, sizeof(fake.qspi));
    copy_request_set_integrity(&request, fake.qspi, request.image_length);
    fake.corrupt_after_application_read = 1;
    assert(!bootloader_copy_engine_run(&storage, &request, &installing, scratch,
                                       sizeof(scratch), &result));
    assert(result.next_state.state != OTA_STATE_TRIAL);
}

static void test_bootloader_version_policy_is_packed_and_current(void)
{
    assert(BOOTLOADER_VERSION_PACK(1U, 2U, 3U) == UINT32_C(0x00010203));
    assert(BOOTLOADER_VERSION_CURRENT == BOOTLOADER_VERSION_PACK(1U, 0U, 0U));
}

static void test_qspi_profile_rejects_a_wrong_jedec_before_mutation(void)
{
    assert(bootloader_qspi_profile_is_expected_jedec(BOOTLOADER_QSPI_EXPECTED_JEDEC));
    assert(!bootloader_qspi_profile_is_expected_jedec(UINT32_C(0xEF4018)));
    assert(BOOTLOADER_QSPI_FLASH_SIZE == 22U);
    assert(BOOTLOADER_QSPI_MAX_CLOCK_HZ == UINT32_C(40000000));
    assert(BOOTLOADER_QSPI_CLOCK_PRESCALER == 5U);
    assert(BOOTLOADER_QSPI_READ_COMMAND == 0x03U);
    assert(BOOTLOADER_QSPI_PROGRAM_COMMAND == 0x02U);
    assert(BOOTLOADER_QSPI_ERASE_COMMAND == 0x20U);
}

static void test_state_store_alternates_the_valid_record_sector(void)
{
    fake_storage_t fake = {0};
    const bootloader_storage_t storage = make_storage(&fake);
    const ota_state_record_t first = make_state(OTA_STATE_NORMAL, 0U, 0U);
    ota_state_record_t second = make_state(OTA_STATE_PENDING_INSTALL, 0U, 0U);
    ota_state_record_t loaded = {0};
    ota_state_record_t next = make_state(OTA_STATE_INSTALLING, 0U, 64U);
    ota_state_record_t persisted = {0};
    ota_state_selection_t selection;

    second.generation++;
    next.generation = second.generation + 1U;
    ota_state_record_encode(&first, fake.primary_state);
    ota_state_record_encode(&second, fake.secondary_state);
    selection = bootloader_state_store_load(&storage, &loaded);
    assert(selection == OTA_STATE_SELECT_SECONDARY);
    assert(loaded.generation == second.generation);

    assert(bootloader_state_store_persist(&storage, selection, &next));
    assert(fake.last_erase_address == TRANSPORT_OTA_STATE_PRIMARY_BASE);
    assert(fake.last_erase_length == BOOTLOADER_INTERNAL_FLASH_ERASE_BYTES);
    assert(fake.program_calls == 2U);
    assert(!fake.saw_unaligned_operation);
    assert(ota_state_record_decode(fake.primary_state, &persisted));
    assert(persisted.generation == next.generation);
    assert(persisted.state == OTA_STATE_INSTALLING);
}

int main(void)
{
    test_application_vectors_require_aligned_ram_msp_and_thumb_reset();
    test_flash_mutation_range_rejects_unsigned_wraparound();
    test_invalid_candidate_manifest_is_rejected_before_install();
    test_candidate_manifest_requires_a_supported_bootloader_version();
    test_state_selects_only_safe_boot_actions();
    test_copy_engine_is_bounded_and_reports_state_progress();
    test_copy_engine_refuses_readback_errors_or_a_final_integrity_mismatch();
    test_qspi_profile_rejects_a_wrong_jedec_before_mutation();
    test_state_store_alternates_the_valid_record_sector();
    test_bootloader_version_policy_is_packed_and_current();
    return 0;
}
