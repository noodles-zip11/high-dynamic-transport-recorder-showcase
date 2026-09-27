#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED

#include "crash_record_target.h"

#include <stdint.h>

#include "build_identity.h"
#include "stm32h743xx.h"

extern uint8_t __crash_record_slot_a[];
extern uint8_t __crash_record_slot_b[];
extern uint8_t __crash_record_end[];
extern uint8_t __heap_end[];

#define CRASH_RECORD_TARGET_RAM_BASE UINT32_C(0x24000000)
#define CRASH_RECORD_TARGET_BASIC_FRAME_WORDS 8U
#define CRASH_RECORD_TARGET_FPU_FRAME_WORDS 18U
#define CRASH_RECORD_TARGET_PROBE_OFFSET 132U
#define CRASH_RECORD_TARGET_CACHE_LINE_BYTES 32U
#define CRASH_RECORD_BUILD_ID_BYTES 32U

static const uint8_t crash_record_build_id_source[] =
    TRANSPORT_GIT_REVISION;

_Static_assert(sizeof(((crash_record_v1_t *)0)->build_id)
                   == CRASH_RECORD_BUILD_ID_BYTES,
               "CrashRecord build ID width changed");
_Static_assert(CRASH_RECORD_TARGET_PROBE_OFFSET
                   >= CRASH_RECORD_V1_ACK_OFFSET + sizeof(uint32_t),
               "CrashRecord probe must follow slot A ACK");
_Static_assert(CRASH_RECORD_TARGET_PROBE_OFFSET + sizeof(uint32_t)
                   <= CRASH_RECORD_V1_SLOT_B_OFFSET,
               "CrashRecord probe must remain before slot B");
_Static_assert(CRASH_RECORD_V1_RECORD_SIZE_BYTES % sizeof(uint32_t) == 0U,
               "CrashRecord retention flush requires whole words");
_Static_assert(CRASH_RECORD_V1_RECORD_SIZE_BYTES
                       % CRASH_RECORD_TARGET_CACHE_LINE_BYTES
                   == 0U,
               "CrashRecord retention flush requires whole cache lines");
_Static_assert(CRASH_RECORD_V1_ACK_OFFSET
                       % CRASH_RECORD_TARGET_CACHE_LINE_BYTES
                   == 0U,
               "CrashRecord ACK must start on a cache-line boundary");

static crash_record_storage_t crash_record_target_storage;
static uint8_t crash_record_target_ready;
static crash_record_v1_t crash_record_target_visible_record;
static uint8_t crash_record_target_visible;
static uint8_t crash_record_target_visible_slot;

static void crash_record_target_barrier(void *context)
{
    (void)context;
    __asm volatile("dmb sy\n\tdsb sy" ::: "memory");
}

static void crash_record_target_flush_slot(uint8_t slot_index)
{
    volatile uint32_t *slot_words =
        (volatile uint32_t *)(void *)(slot_index == 0U
                                         ? __crash_record_slot_a
                                         : __crash_record_slot_b);
    uint32_t index;

    for (index = 0U;
         index < CRASH_RECORD_V1_RECORD_SIZE_BYTES / sizeof(uint32_t);
         index++)
    {
        const uint32_t retained_word = slot_words[index];
        slot_words[index] = retained_word;
        (void)slot_words[index];
    }
    for (index = 0U;
         index < CRASH_RECORD_V1_RECORD_SIZE_BYTES;
         index += CRASH_RECORD_TARGET_CACHE_LINE_BYTES)
    {
        SCB->DCCMVAC = (uint32_t)(uintptr_t)slot_words + index;
    }
    crash_record_target_barrier(NULL);
}

static void crash_record_target_flush_ack(uint8_t slot_index)
{
    volatile uint8_t *slot = slot_index == 0U
                                 ? __crash_record_slot_a
                                 : __crash_record_slot_b;
    const uintptr_t ack_address = (uintptr_t)slot
                                  + CRASH_RECORD_V1_ACK_OFFSET;

    SCB->DCCMVAC = (uint32_t)ack_address;
    crash_record_target_barrier(NULL);
}

static int crash_record_target_addresses_valid(void)
{
    const uintptr_t base = (uintptr_t)CRASH_RECORD_V1_RETENTION_BASE;
    const uintptr_t slot_a = (uintptr_t)__crash_record_slot_a;
    const uintptr_t slot_b = (uintptr_t)__crash_record_slot_b;
    const uintptr_t end = (uintptr_t)__crash_record_end;

    return slot_a == base + CRASH_RECORD_V1_SLOT_A_OFFSET
           && slot_b == base + CRASH_RECORD_V1_SLOT_B_OFFSET
           && end == base + CRASH_RECORD_V1_RESERVED_BYTES
           && end <= base + CRASH_RECORD_V1_RETENTION_SIZE_BYTES;
}

static int crash_record_target_probe(void)
{
    volatile uint32_t *probe =
        (volatile uint32_t *)(uintptr_t)(CRASH_RECORD_V1_RETENTION_BASE
                                         + CRASH_RECORD_TARGET_PROBE_OFFSET);
    const uint32_t previous = *probe;
    const uint32_t pattern = UINT32_C(0xA55AA55A);
    uint32_t observed;

    *probe = pattern;
    crash_record_target_barrier(NULL);
    observed = *probe;
    *probe = previous;
    crash_record_target_barrier(NULL);
    return observed == pattern && *probe == previous;
}

int crash_record_target_init(void)
{
    crash_record_target_ready = 0U;
    crash_record_target_visible = 0U;
    crash_record_target_visible_slot = 0U;
    crash_record_target_storage.slot_a = NULL;
    crash_record_target_storage.slot_b = NULL;
    crash_record_target_storage.barrier = NULL;
    crash_record_target_storage.barrier_context = NULL;

    if (!crash_record_target_addresses_valid()
        || !crash_record_target_probe())
    {
        return CRASH_RECORD_TARGET_UNAVAILABLE;
    }

    crash_record_target_storage.slot_a = __crash_record_slot_a;
    crash_record_target_storage.slot_b = __crash_record_slot_b;
    crash_record_target_storage.barrier = crash_record_target_barrier;
    crash_record_target_storage.barrier_context = NULL;
    crash_record_target_ready = 1U;
    return CRASH_RECORD_TARGET_OK;
}

int crash_record_target_is_ready(void)
{
    return crash_record_target_ready != 0U;
}

crash_record_storage_t *crash_record_target_get_storage(void)
{
    return crash_record_target_ready != 0U ? &crash_record_target_storage : NULL;
}

static void crash_record_target_copy_record(
    const crash_record_v1_t *source,
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

crash_record_status_t crash_record_target_recover(void)
{
    crash_record_status_t status;
    crash_record_v1_t record;
    uint8_t slot_index;

    if (crash_record_target_ready == 0U)
    {
        return (crash_record_status_t)CRASH_RECORD_TARGET_UNAVAILABLE;
    }

    status = crash_record_recover(&crash_record_target_storage,
                                  &record,
                                  &slot_index,
                                  NULL);
    if (status != CRASH_RECORD_STATUS_OK)
    {
        crash_record_target_visible = 0U;
        return status;
    }

    crash_record_target_copy_record(&record,
                                    &crash_record_target_visible_record);
    crash_record_target_visible_slot = slot_index;
    crash_record_target_visible = 1U;
    return CRASH_RECORD_STATUS_OK;
}

int crash_record_target_get_visible_record(crash_record_v1_t *record)
{
    if (record == NULL || crash_record_target_visible == 0U)
    {
        return CRASH_RECORD_TARGET_UNAVAILABLE;
    }

    crash_record_target_copy_record(&crash_record_target_visible_record,
                                    record);
    return CRASH_RECORD_TARGET_OK;
}

int crash_record_target_get_visible_slot(uint8_t *slot_index)
{
    if (slot_index == NULL || crash_record_target_visible == 0U)
    {
        return CRASH_RECORD_TARGET_UNAVAILABLE;
    }

    *slot_index = crash_record_target_visible_slot;
    return CRASH_RECORD_TARGET_OK;
}

crash_record_status_t crash_record_target_ack(uint32_t sequence)
{
    crash_record_status_t status;
    uint8_t slot_index;

    if (crash_record_target_ready == 0U)
    {
        return (crash_record_status_t)CRASH_RECORD_TARGET_UNAVAILABLE;
    }

    status = crash_record_recover(&crash_record_target_storage,
                                  NULL,
                                  &slot_index,
                                  NULL);
    if (status != CRASH_RECORD_STATUS_OK)
    {
        return status;
    }
    status = crash_record_ack(&crash_record_target_storage, sequence);
    if (status == CRASH_RECORD_STATUS_OK)
    {
        crash_record_target_flush_ack(slot_index);
    }
    return status;
}

static int crash_record_fault_frame_readable(const uint32_t *stack_pointer,
                                             uint32_t word_count)
{
    const uintptr_t start = (uintptr_t)stack_pointer;
    const uintptr_t end = (uintptr_t)__heap_end;
    const uintptr_t byte_count = (uintptr_t)word_count * sizeof(uint32_t);

    return stack_pointer != NULL
           && (start & (sizeof(uint32_t) - 1U)) == 0U
           && start >= CRASH_RECORD_TARGET_RAM_BASE
           && start <= end
           && byte_count <= end - start;
}

static void crash_record_zero_basic_frame(crash_record_v1_t *record)
{
    record->r0 = 0U;
    record->r1 = 0U;
    record->r2 = 0U;
    record->r3 = 0U;
    record->r12 = 0U;
    record->lr = 0U;
    record->pc = 0U;
    record->xpsr = 0U;
}

static void crash_record_copy_basic_frame(crash_record_v1_t *record,
                                          const uint32_t *frame)
{
    record->r0 = frame[0];
    record->r1 = frame[1];
    record->r2 = frame[2];
    record->r3 = frame[3];
    record->r12 = frame[4];
    record->lr = frame[5];
    record->pc = frame[6];
    record->xpsr = frame[7];
}

static void crash_record_target_request_reset(void) __attribute__((noreturn));

static void crash_record_target_request_reset(void)
{
    const uint32_t priority = SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk;
    const uint32_t reset_value =
        (UINT32_C(0x5FA) << SCB_AIRCR_VECTKEY_Pos)
        | priority
        | SCB_AIRCR_SYSRESETREQ_Msk;

    __asm volatile("dsb sy" ::: "memory");
    SCB->AIRCR = reset_value;
    __asm volatile("dsb sy" ::: "memory");
    for (;;)
    {
        __asm volatile("nop" ::: "memory");
    }
}

void crash_record_fault_capture(const uint32_t *stack_pointer,
                                uint32_t exc_return,
                                uint32_t fault_kind)
{
    crash_record_v1_t record;
    const crash_record_storage_t *storage;
    const uint32_t *basic_frame = stack_pointer;
    uint8_t written_slot = 0U;
    uint32_t capture_flags = 0U;
    uint32_t index;
    uint32_t frame_words = CRASH_RECORD_TARGET_BASIC_FRAME_WORDS;

    record.magic = CRASH_RECORD_V1_MAGIC;
    record.format_version = CRASH_RECORD_V1_FORMAT_VERSION;
    record.header_length = CRASH_RECORD_V1_HEADER_LENGTH_BYTES;
    record.record_length = CRASH_RECORD_V1_RECORD_SIZE_BYTES;
    record.sequence = 0U;
    record.fault_kind = fault_kind;
    record.capture_flags = 0U;
    record.exc_return = exc_return;
    record.sp = (uint32_t)(uintptr_t)stack_pointer;
    crash_record_zero_basic_frame(&record);
    record.cfsr = SCB->CFSR;
    record.hfsr = SCB->HFSR;
    record.shcsr = SCB->SHCSR;
    record.mmfar = SCB->MMFAR;
    record.bfar = SCB->BFAR;
    record.reset_flags = RCC->RSR;
    for (index = 0U; index < CRASH_RECORD_BUILD_ID_BYTES; index++)
    {
        record.build_id[index] =
            index < sizeof(crash_record_build_id_source) - 1U
                ? crash_record_build_id_source[index]
                : 0U;
    }
    record.crc32 = 0U;
    record.commit_marker = CRASH_RECORD_V1_COMMIT_VALID;

    if ((exc_return & 4U) != 0U)
    {
        capture_flags |= CRASH_RECORD_CAPTURE_STACK_PSP;
    }
    if ((exc_return & 0x10U) == 0U)
    {
        capture_flags |= CRASH_RECORD_CAPTURE_FPU_EXTENDED_FRAME;
        frame_words += CRASH_RECORD_TARGET_FPU_FRAME_WORDS;
        basic_frame = (const uint32_t *)((uintptr_t)stack_pointer
                                         + CRASH_RECORD_TARGET_FPU_FRAME_WORDS
                                               * sizeof(uint32_t));
    }
    if (crash_record_fault_frame_readable(stack_pointer, frame_words))
    {
        crash_record_copy_basic_frame(&record, basic_frame);
    }
    else
    {
        capture_flags |= CRASH_RECORD_CAPTURE_FRAME_UNREADABLE;
    }
    record.capture_flags = capture_flags;

    storage = crash_record_target_get_storage();
    if (storage != NULL)
    {
        if (crash_record_write_next((crash_record_storage_t *)(void *)storage,
                                    &record,
                                    NULL,
                                    &written_slot)
            == CRASH_RECORD_STATUS_OK)
        {
            crash_record_target_flush_slot(written_slot);
        }
    }
    crash_record_target_request_reset();
}

#endif
