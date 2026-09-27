#ifndef TRANSPORT_RECORDER_CRASH_RECORD_H
#define TRANSPORT_RECORDER_CRASH_RECORD_H

#include <stddef.h>
#include <stdint.h>

#define CRASH_RECORD_V1_MAGIC UINT32_C(0x31565243)
#define CRASH_RECORD_V1_FORMAT_VERSION UINT16_C(1)
#define CRASH_RECORD_V1_HEADER_LENGTH_BYTES UINT16_C(32)
#define CRASH_RECORD_V1_RECORD_SIZE_BYTES UINT32_C(128)
#define CRASH_RECORD_V1_SLOT_SIZE_BYTES UINT32_C(256)
#define CRASH_RECORD_V1_SLOT_COUNT UINT8_C(2)
#define CRASH_RECORD_V1_ACK_OFFSET UINT32_C(128)
#define CRASH_RECORD_V1_ACK_UNACKED UINT32_C(0)
#define CRASH_RECORD_V1_COMMIT_INVALID UINT32_C(0)
#define CRASH_RECORD_V1_COMMIT_VALID UINT32_C(0xA5C35A3C)

#define CRASH_RECORD_V1_RETENTION_BASE UINT32_C(0x3800FE00)
#define CRASH_RECORD_V1_RETENTION_SIZE_BYTES UINT32_C(0x200)
#define CRASH_RECORD_V1_RESERVED_BYTES UINT32_C(512)
#define CRASH_RECORD_V1_SLOT_A_OFFSET UINT32_C(0x000)
#define CRASH_RECORD_V1_SLOT_B_OFFSET UINT32_C(0x100)

typedef enum
{
    CRASH_RECORD_FAULT_HARDFAULT = 1U,
    CRASH_RECORD_FAULT_MEMMANAGE = 2U,
    CRASH_RECORD_FAULT_BUSFAULT = 3U,
    CRASH_RECORD_FAULT_USAGEFAULT = 4U,
} crash_record_fault_kind_t;

typedef enum
{
    CRASH_RECORD_CAPTURE_STACK_PSP = 1U << 0,
    CRASH_RECORD_CAPTURE_FPU_EXTENDED_FRAME = 1U << 1,
    CRASH_RECORD_CAPTURE_FRAME_UNREADABLE = 1U << 2,
} crash_record_capture_flag_t;

typedef struct
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_length;
    uint32_t record_length;
    uint32_t sequence;
    uint32_t fault_kind;
    uint32_t capture_flags;
    uint32_t exc_return;
    uint32_t sp;
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t shcsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t reset_flags;
    uint8_t build_id[32];
    uint32_t crc32;
    uint32_t commit_marker;
} crash_record_v1_t;

_Static_assert(sizeof(crash_record_v1_t) == 128U,
               "CrashRecord v1 must be exactly 128 bytes");
_Static_assert(offsetof(crash_record_v1_t, magic) == 0U,
               "CrashRecord magic offset changed");
_Static_assert(offsetof(crash_record_v1_t, format_version) == 4U,
               "CrashRecord version offset changed");
_Static_assert(offsetof(crash_record_v1_t, header_length) == 6U,
               "CrashRecord header length offset changed");
_Static_assert(offsetof(crash_record_v1_t, record_length) == 8U,
               "CrashRecord record length offset changed");
_Static_assert(offsetof(crash_record_v1_t, sequence) == 12U,
               "CrashRecord sequence offset changed");
_Static_assert(offsetof(crash_record_v1_t, fault_kind) == 16U,
               "CrashRecord fault kind offset changed");
_Static_assert(offsetof(crash_record_v1_t, capture_flags) == 20U,
               "CrashRecord capture flags offset changed");
_Static_assert(offsetof(crash_record_v1_t, exc_return) == 24U,
               "CrashRecord EXC_RETURN offset changed");
_Static_assert(offsetof(crash_record_v1_t, sp) == 28U,
               "CrashRecord SP offset changed");
_Static_assert(offsetof(crash_record_v1_t, r0) == 32U,
               "CrashRecord r0 offset changed");
_Static_assert(offsetof(crash_record_v1_t, r1) == 36U,
               "CrashRecord r1 offset changed");
_Static_assert(offsetof(crash_record_v1_t, r2) == 40U,
               "CrashRecord r2 offset changed");
_Static_assert(offsetof(crash_record_v1_t, r3) == 44U,
               "CrashRecord r3 offset changed");
_Static_assert(offsetof(crash_record_v1_t, r12) == 48U,
               "CrashRecord r12 offset changed");
_Static_assert(offsetof(crash_record_v1_t, lr) == 52U,
               "CrashRecord LR offset changed");
_Static_assert(offsetof(crash_record_v1_t, pc) == 56U,
               "CrashRecord PC offset changed");
_Static_assert(offsetof(crash_record_v1_t, xpsr) == 60U,
               "CrashRecord xPSR offset changed");
_Static_assert(offsetof(crash_record_v1_t, cfsr) == 64U,
               "CrashRecord CFSR offset changed");
_Static_assert(offsetof(crash_record_v1_t, hfsr) == 68U,
               "CrashRecord HFSR offset changed");
_Static_assert(offsetof(crash_record_v1_t, shcsr) == 72U,
               "CrashRecord SHCSR offset changed");
_Static_assert(offsetof(crash_record_v1_t, mmfar) == 76U,
               "CrashRecord MMFAR offset changed");
_Static_assert(offsetof(crash_record_v1_t, bfar) == 80U,
               "CrashRecord BFAR offset changed");
_Static_assert(offsetof(crash_record_v1_t, reset_flags) == 84U,
               "CrashRecord reset flags offset changed");
_Static_assert(offsetof(crash_record_v1_t, build_id) == 88U,
               "CrashRecord build ID offset changed");
_Static_assert(offsetof(crash_record_v1_t, crc32) == 120U,
               "CrashRecord CRC offset changed");
_Static_assert(offsetof(crash_record_v1_t, commit_marker) == 124U,
               "CrashRecord commit offset changed");

typedef void (*crash_record_barrier_fn)(void *context);

#ifdef CRASH_RECORD_TEST_HOOKS
typedef void (*crash_record_write_hook_fn)(void *context,
                                           uint8_t slot_index,
                                           uint16_t offset,
                                           uint32_t value,
                                           uint8_t width);
#endif

typedef struct
{
    volatile uint8_t *slot_a;
    volatile uint8_t *slot_b;
    crash_record_barrier_fn barrier;
    void *barrier_context;
#ifdef CRASH_RECORD_TEST_HOOKS
    crash_record_write_hook_fn write_hook;
    void *write_hook_context;
#endif
} crash_record_storage_t;

typedef enum
{
    CRASH_RECORD_STATUS_OK = 0,
    CRASH_RECORD_STATUS_NO_VALID = 1,
    CRASH_RECORD_STATUS_SEQUENCE_AMBIGUOUS = 2,
    CRASH_RECORD_STATUS_STALE = 3,
    CRASH_RECORD_STATUS_INVALID_ARGUMENT = -1,
} crash_record_status_t;

uint32_t crash_record_v1_crc32(const volatile uint8_t *data,
                               uint32_t length);

void crash_record_v1_encode(const crash_record_v1_t *record,
                            volatile uint8_t *slot);

int crash_record_v1_validate(const volatile uint8_t *slot,
                             crash_record_v1_t *record);

crash_record_status_t crash_record_recover(
    const crash_record_storage_t *storage,
    crash_record_v1_t *record,
    uint8_t *slot_index,
    uint32_t *ack_marker);

crash_record_status_t crash_record_write_next(
    crash_record_storage_t *storage,
    const crash_record_v1_t *record_template,
    crash_record_v1_t *written_record,
    uint8_t *slot_index);

crash_record_status_t crash_record_ack(crash_record_storage_t *storage,
                                       uint32_t sequence);

#endif
