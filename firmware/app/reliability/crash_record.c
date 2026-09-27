#include "crash_record.h"

#include <stdbool.h>

typedef struct
{
    bool valid;
    crash_record_v1_t record;
    uint32_t ack_marker;
} crash_record_slot_view_t;

static uint16_t read_u16(const volatile uint8_t *slot, uint32_t offset)
{
    return (uint16_t)slot[offset]
           | ((uint16_t)slot[offset + 1U] << 8U);
}

static uint32_t read_u32(const volatile uint8_t *slot, uint32_t offset)
{
    return (uint32_t)slot[offset]
           | ((uint32_t)slot[offset + 1U] << 8U)
           | ((uint32_t)slot[offset + 2U] << 16U)
           | ((uint32_t)slot[offset + 3U] << 24U);
}

static void observe_write(const crash_record_storage_t *storage,
                          uint8_t slot_index,
                          uint32_t offset,
                          uint32_t value,
                          uint8_t width)
{
#ifdef CRASH_RECORD_TEST_HOOKS
    if (storage != NULL && storage->write_hook != NULL)
    {
        storage->write_hook(storage->write_hook_context,
                            slot_index,
                            (uint16_t)offset,
                            value,
                            width);
    }
#else
    (void)storage;
    (void)slot_index;
    (void)offset;
    (void)value;
    (void)width;
#endif
}

static void write_u16(volatile uint8_t *slot,
                      uint32_t offset,
                      uint16_t value)
{
    slot[offset] = (uint8_t)value;
    slot[offset + 1U] = (uint8_t)(value >> 8U);
}

static void write_u32(const crash_record_storage_t *storage,
                      uint8_t slot_index,
                      volatile uint8_t *slot,
                      uint32_t offset,
                      uint32_t value)
{
    volatile uint32_t *word =
        (volatile uint32_t *)(void *)(slot + offset);

    *word = value;
    observe_write(storage, slot_index, offset, value, 4U);
}

static void write_build_id(const crash_record_storage_t *storage,
                           uint8_t slot_index,
                           volatile uint8_t *slot,
                           const uint8_t *build_id)
{
    uint32_t index;

    for (index = 0U; index < 32U; index++)
    {
        slot[88U + index] = build_id[index];
        observe_write(storage, slot_index, 88U + index,
                      build_id[index], 1U);
    }
}

static bool fault_kind_is_valid(uint32_t fault_kind)
{
    return fault_kind >= CRASH_RECORD_FAULT_HARDFAULT
           && fault_kind <= CRASH_RECORD_FAULT_USAGEFAULT;
}

static void decode_record(const volatile uint8_t *slot,
                          crash_record_v1_t *record)
{
    uint32_t index;

    record->magic = read_u32(slot, 0U);
    record->format_version = read_u16(slot, 4U);
    record->header_length = read_u16(slot, 6U);
    record->record_length = read_u32(slot, 8U);
    record->sequence = read_u32(slot, 12U);
    record->fault_kind = read_u32(slot, 16U);
    record->capture_flags = read_u32(slot, 20U);
    record->exc_return = read_u32(slot, 24U);
    record->sp = read_u32(slot, 28U);
    record->r0 = read_u32(slot, 32U);
    record->r1 = read_u32(slot, 36U);
    record->r2 = read_u32(slot, 40U);
    record->r3 = read_u32(slot, 44U);
    record->r12 = read_u32(slot, 48U);
    record->lr = read_u32(slot, 52U);
    record->pc = read_u32(slot, 56U);
    record->xpsr = read_u32(slot, 60U);
    record->cfsr = read_u32(slot, 64U);
    record->hfsr = read_u32(slot, 68U);
    record->shcsr = read_u32(slot, 72U);
    record->mmfar = read_u32(slot, 76U);
    record->bfar = read_u32(slot, 80U);
    record->reset_flags = read_u32(slot, 84U);
    for (index = 0U; index < 32U; index++)
    {
        record->build_id[index] = slot[88U + index];
    }
    record->crc32 = read_u32(slot, 120U);
    record->commit_marker = read_u32(slot, 124U);
}

uint32_t crash_record_v1_crc32(const volatile uint8_t *data,
                               uint32_t length)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    uint32_t index;

    if (data == NULL)
    {
        return 0U;
    }

    for (index = 0U; index < length; index++)
    {
        uint32_t bit;

        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (crc >> 1U)
                  ^ ((crc & 1U) != 0U
                         ? UINT32_C(0xEDB88320)
                         : 0U);
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

void crash_record_v1_encode(const crash_record_v1_t *record,
                            volatile uint8_t *slot)
{
    uint32_t index;

    if (record == NULL || slot == NULL)
    {
        return;
    }

    write_u32(NULL, 0U, slot, 0U, record->magic);
    write_u16(slot, 4U, record->format_version);
    write_u16(slot, 6U, record->header_length);
    write_u32(NULL, 0U, slot, 8U, record->record_length);
    write_u32(NULL, 0U, slot, 12U, record->sequence);
    write_u32(NULL, 0U, slot, 16U, record->fault_kind);
    write_u32(NULL, 0U, slot, 20U, record->capture_flags);
    write_u32(NULL, 0U, slot, 24U, record->exc_return);
    write_u32(NULL, 0U, slot, 28U, record->sp);
    write_u32(NULL, 0U, slot, 32U, record->r0);
    write_u32(NULL, 0U, slot, 36U, record->r1);
    write_u32(NULL, 0U, slot, 40U, record->r2);
    write_u32(NULL, 0U, slot, 44U, record->r3);
    write_u32(NULL, 0U, slot, 48U, record->r12);
    write_u32(NULL, 0U, slot, 52U, record->lr);
    write_u32(NULL, 0U, slot, 56U, record->pc);
    write_u32(NULL, 0U, slot, 60U, record->xpsr);
    write_u32(NULL, 0U, slot, 64U, record->cfsr);
    write_u32(NULL, 0U, slot, 68U, record->hfsr);
    write_u32(NULL, 0U, slot, 72U, record->shcsr);
    write_u32(NULL, 0U, slot, 76U, record->mmfar);
    write_u32(NULL, 0U, slot, 80U, record->bfar);
    write_u32(NULL, 0U, slot, 84U, record->reset_flags);
    for (index = 0U; index < 32U; index++)
    {
        slot[88U + index] = record->build_id[index];
    }
    write_u32(NULL, 0U, slot, 120U, record->crc32);
    write_u32(NULL, 0U, slot, 124U, record->commit_marker);
}

int crash_record_v1_validate(const volatile uint8_t *slot,
                             crash_record_v1_t *record)
{
    uint32_t expected_crc;

    if (slot == NULL
        || read_u32(slot, 0U) != CRASH_RECORD_V1_MAGIC
        || read_u16(slot, 4U) != CRASH_RECORD_V1_FORMAT_VERSION
        || read_u16(slot, 6U) != CRASH_RECORD_V1_HEADER_LENGTH_BYTES
        || read_u32(slot, 8U) != CRASH_RECORD_V1_RECORD_SIZE_BYTES
        || read_u32(slot, 12U) == 0U
        || !fault_kind_is_valid(read_u32(slot, 16U))
        || read_u32(slot, 124U) != CRASH_RECORD_V1_COMMIT_VALID)
    {
        return 0;
    }

    expected_crc = crash_record_v1_crc32(slot, 120U);
    if (read_u32(slot, 120U) != expected_crc)
    {
        return 0;
    }

    if (record != NULL)
    {
        decode_record(slot, record);
    }
    return 1;
}

static volatile uint8_t *storage_slot(crash_record_storage_t *storage,
                                      uint8_t slot_index)
{
    return slot_index == 0U ? storage->slot_a : storage->slot_b;
}

static const volatile uint8_t *storage_slot_const(
    const crash_record_storage_t *storage,
    uint8_t slot_index)
{
    return slot_index == 0U ? storage->slot_a : storage->slot_b;
}

static bool sequence_is_newer(uint32_t left, uint32_t right)
{
    uint32_t delta = left - right;

    return delta != 0U && delta < UINT32_C(0x80000000);
}

static bool sequences_are_ambiguous(uint32_t left, uint32_t right)
{
    return (left - right) == UINT32_C(0x80000000);
}

static bool view_is_acked(const crash_record_slot_view_t *view)
{
    return view->valid && view->ack_marker == view->record.sequence;
}

static crash_record_status_t load_views(
    const crash_record_storage_t *storage,
    crash_record_slot_view_t views[CRASH_RECORD_V1_SLOT_COUNT])
{
    uint8_t index;

    if (storage == NULL || storage->slot_a == NULL || storage->slot_b == NULL
        || views == NULL)
    {
        return CRASH_RECORD_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < CRASH_RECORD_V1_SLOT_COUNT; index++)
    {
        const volatile uint8_t *slot = storage_slot_const(storage, index);

        views[index].valid = crash_record_v1_validate(slot,
                                                       &views[index].record)
                             != 0;
        views[index].ack_marker = read_u32(slot,
                                           CRASH_RECORD_V1_ACK_OFFSET);
    }
    return CRASH_RECORD_STATUS_OK;
}

static crash_record_status_t newest_index(
    const crash_record_slot_view_t views[CRASH_RECORD_V1_SLOT_COUNT],
    uint8_t *index)
{
    if (!views[0].valid && !views[1].valid)
    {
        return CRASH_RECORD_STATUS_NO_VALID;
    }
    if (views[0].valid && !views[1].valid)
    {
        *index = 0U;
        return CRASH_RECORD_STATUS_OK;
    }
    if (!views[0].valid && views[1].valid)
    {
        *index = 1U;
        return CRASH_RECORD_STATUS_OK;
    }
    if (sequences_are_ambiguous(views[0].record.sequence,
                                views[1].record.sequence))
    {
        return CRASH_RECORD_STATUS_SEQUENCE_AMBIGUOUS;
    }
    *index = sequence_is_newer(views[1].record.sequence,
                               views[0].record.sequence)
                ? 1U
                : 0U;
    return CRASH_RECORD_STATUS_OK;
}

static uint8_t oldest_index(const crash_record_slot_view_t views[2],
                            uint8_t first,
                            uint8_t second)
{
    return sequence_is_newer(views[first].record.sequence,
                             views[second].record.sequence)
               ? second
               : first;
}

static uint8_t select_target_index(
    const crash_record_slot_view_t views[CRASH_RECORD_V1_SLOT_COUNT],
    uint8_t newest)
{
    bool acked_a = view_is_acked(&views[0]) && newest != 0U;
    bool acked_b = view_is_acked(&views[1]) && newest != 1U;

    if (!views[0].valid)
    {
        return 0U;
    }
    if (!views[1].valid)
    {
        return 1U;
    }
    if (acked_a && acked_b)
    {
        return oldest_index(views, 0U, 1U);
    }
    if (acked_a)
    {
        return 0U;
    }
    if (acked_b)
    {
        return 1U;
    }
    return newest == 0U ? 1U : 0U;
}

static void copy_record(const crash_record_v1_t *source,
                        crash_record_v1_t *destination)
{
    uint32_t index;

    destination->magic = source->magic;
    destination->format_version = source->format_version;
    destination->header_length = source->header_length;
    destination->record_length = source->record_length;
    destination->sequence = source->sequence;
    destination->fault_kind = source->fault_kind;
    destination->capture_flags = source->capture_flags;
    destination->exc_return = source->exc_return;
    destination->sp = source->sp;
    destination->r0 = source->r0;
    destination->r1 = source->r1;
    destination->r2 = source->r2;
    destination->r3 = source->r3;
    destination->r12 = source->r12;
    destination->lr = source->lr;
    destination->pc = source->pc;
    destination->xpsr = source->xpsr;
    destination->cfsr = source->cfsr;
    destination->hfsr = source->hfsr;
    destination->shcsr = source->shcsr;
    destination->mmfar = source->mmfar;
    destination->bfar = source->bfar;
    destination->reset_flags = source->reset_flags;
    for (index = 0U; index < 32U; index++)
    {
        destination->build_id[index] = source->build_id[index];
    }
    destination->crc32 = source->crc32;
    destination->commit_marker = source->commit_marker;
}

static void write_record_content(const crash_record_storage_t *storage,
                                 uint8_t slot_index,
                                 volatile uint8_t *slot,
                                 const crash_record_v1_t *record)
{
    write_u32(storage, slot_index, slot, 0U, record->magic);
    write_u16(slot, 4U, record->format_version);
    write_u16(slot, 6U, record->header_length);
    write_u32(storage, slot_index, slot, 8U, record->record_length);
    write_u32(storage, slot_index, slot, 12U, record->sequence);
    write_u32(storage, slot_index, slot, 16U, record->fault_kind);
    write_u32(storage, slot_index, slot, 20U, record->capture_flags);
    write_u32(storage, slot_index, slot, 24U, record->exc_return);
    write_u32(storage, slot_index, slot, 28U, record->sp);
    write_u32(storage, slot_index, slot, 32U, record->r0);
    write_u32(storage, slot_index, slot, 36U, record->r1);
    write_u32(storage, slot_index, slot, 40U, record->r2);
    write_u32(storage, slot_index, slot, 44U, record->r3);
    write_u32(storage, slot_index, slot, 48U, record->r12);
    write_u32(storage, slot_index, slot, 52U, record->lr);
    write_u32(storage, slot_index, slot, 56U, record->pc);
    write_u32(storage, slot_index, slot, 60U, record->xpsr);
    write_u32(storage, slot_index, slot, 64U, record->cfsr);
    write_u32(storage, slot_index, slot, 68U, record->hfsr);
    write_u32(storage, slot_index, slot, 72U, record->shcsr);
    write_u32(storage, slot_index, slot, 76U, record->mmfar);
    write_u32(storage, slot_index, slot, 80U, record->bfar);
    write_u32(storage, slot_index, slot, 84U, record->reset_flags);
    write_build_id(storage, slot_index, slot, record->build_id);
}

static uint32_t next_sequence(const crash_record_slot_view_t views[2],
                              uint8_t newest)
{
    uint32_t sequence;

    if (!views[0].valid && !views[1].valid)
    {
        return 1U;
    }
    sequence = views[newest].record.sequence + 1U;
    return sequence == 0U ? 1U : sequence;
}

crash_record_status_t crash_record_recover(
    const crash_record_storage_t *storage,
    crash_record_v1_t *record,
    uint8_t *slot_index,
    uint32_t *ack_marker)
{
    crash_record_slot_view_t views[CRASH_RECORD_V1_SLOT_COUNT];
    crash_record_status_t status;
    uint8_t newest;

    status = load_views(storage, views);
    if (status != CRASH_RECORD_STATUS_OK)
    {
        return status;
    }
    status = newest_index(views, &newest);
    if (status != CRASH_RECORD_STATUS_OK)
    {
        return status;
    }
    if (record != NULL)
    {
        copy_record(&views[newest].record, record);
    }
    if (slot_index != NULL)
    {
        *slot_index = newest;
    }
    if (ack_marker != NULL)
    {
        *ack_marker = views[newest].ack_marker;
    }
    return CRASH_RECORD_STATUS_OK;
}

crash_record_status_t crash_record_write_next(
    crash_record_storage_t *storage,
    const crash_record_v1_t *record_template,
    crash_record_v1_t *written_record,
    uint8_t *slot_index)
{
    crash_record_slot_view_t views[CRASH_RECORD_V1_SLOT_COUNT];
    crash_record_v1_t record;
    crash_record_status_t status;
    uint8_t newest = 0U;
    uint8_t target;
    volatile uint8_t *slot;

    if (record_template == NULL || storage == NULL
        || storage->barrier == NULL)
    {
        return CRASH_RECORD_STATUS_INVALID_ARGUMENT;
    }
    status = load_views(storage, views);
    if (status != CRASH_RECORD_STATUS_OK)
    {
        return status;
    }
    if ((views[0].valid || views[1].valid)
        && newest_index(views, &newest)
               == CRASH_RECORD_STATUS_SEQUENCE_AMBIGUOUS)
    {
        return CRASH_RECORD_STATUS_SEQUENCE_AMBIGUOUS;
    }
    target = (views[0].valid || views[1].valid)
                 ? select_target_index(views, newest)
                 : 0U;
    slot = storage_slot(storage, target);

    copy_record(record_template, &record);
    record.magic = CRASH_RECORD_V1_MAGIC;
    record.format_version = CRASH_RECORD_V1_FORMAT_VERSION;
    record.header_length = CRASH_RECORD_V1_HEADER_LENGTH_BYTES;
    record.record_length = CRASH_RECORD_V1_RECORD_SIZE_BYTES;
    record.sequence = next_sequence(views, newest);
    record.crc32 = 0U;
    record.commit_marker = CRASH_RECORD_V1_COMMIT_VALID;

    write_u32(storage, target, slot, 124U, CRASH_RECORD_V1_COMMIT_INVALID);
    write_u32(storage, target, slot, CRASH_RECORD_V1_ACK_OFFSET,
              CRASH_RECORD_V1_ACK_UNACKED);
    write_record_content(storage, target, slot, &record);
    record.crc32 = crash_record_v1_crc32(slot, 120U);
    write_u32(storage, target, slot, 120U, record.crc32);
    storage->barrier(storage->barrier_context);
    write_u32(storage, target, slot, 124U, CRASH_RECORD_V1_COMMIT_VALID);

    if (written_record != NULL)
    {
        copy_record(&record, written_record);
    }
    if (slot_index != NULL)
    {
        *slot_index = target;
    }
    return CRASH_RECORD_STATUS_OK;
}

crash_record_status_t crash_record_ack(crash_record_storage_t *storage,
                                       uint32_t sequence)
{
    crash_record_slot_view_t views[CRASH_RECORD_V1_SLOT_COUNT];
    crash_record_status_t status;
    uint8_t newest;
    volatile uint8_t *slot;

    status = load_views(storage, views);
    if (status != CRASH_RECORD_STATUS_OK)
    {
        return status;
    }
    status = newest_index(views, &newest);
    if (status != CRASH_RECORD_STATUS_OK)
    {
        return status;
    }
    if (views[newest].record.sequence != sequence)
    {
        return CRASH_RECORD_STATUS_STALE;
    }
    if (views[newest].ack_marker == sequence)
    {
        return CRASH_RECORD_STATUS_OK;
    }

    slot = storage_slot(storage, newest);
    write_u32(storage, newest, slot, CRASH_RECORD_V1_ACK_OFFSET, sequence);
    if (storage->barrier != NULL)
    {
        storage->barrier(storage->barrier_context);
    }
    return CRASH_RECORD_STATUS_OK;
}
