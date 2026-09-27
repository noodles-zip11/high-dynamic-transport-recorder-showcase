#ifndef TRANSPORT_RECORDER_CRASH_RECORD_TARGET_H
#define TRANSPORT_RECORDER_CRASH_RECORD_TARGET_H

#include <stdint.h>

#include "crash_record.h"

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED

#define CRASH_RECORD_TARGET_OK 0
#define CRASH_RECORD_TARGET_UNAVAILABLE (-1)

int crash_record_target_init(void);

int crash_record_target_is_ready(void);

crash_record_storage_t *crash_record_target_get_storage(void);

crash_record_status_t crash_record_target_recover(void);

int crash_record_target_get_visible_record(crash_record_v1_t *record);

int crash_record_target_get_visible_slot(uint8_t *slot_index);

crash_record_status_t crash_record_target_ack(uint32_t sequence);

void crash_record_fault_capture(const uint32_t *stack_pointer,
                                uint32_t exc_return,
                                uint32_t fault_kind)
    __attribute__((noreturn));

#endif

#endif
