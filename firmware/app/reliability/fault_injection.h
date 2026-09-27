#ifndef TRANSPORT_RECORDER_FAULT_INJECTION_H
#define TRANSPORT_RECORDER_FAULT_INJECTION_H

#include <stdint.h>

#ifdef TRANSPORT_FAULT_INJECTION_ENABLED

typedef enum
{
    FAULT_INJECTION_HARDFAULT = 1U,
    FAULT_INJECTION_MEMMANAGE = 2U,
    FAULT_INJECTION_BUSFAULT = 3U,
    FAULT_INJECTION_USAGEFAULT = 4U,
} fault_injection_fault_t;

typedef enum
{
    FAULT_INJECTION_EVENT_SEQUENCE_GAP = 1U,
    FAULT_INJECTION_EVENT_PRETRIGGER_SHORT = 2U,
    FAULT_INJECTION_EVENT_DURATION_CAP = 3U,
    FAULT_INJECTION_EVENT_POOL_PRESSURE = 4U,
    FAULT_INJECTION_EVENT_QUEUE_PRESSURE = 5U,
} fault_injection_event_case_t;

typedef int (*fault_injection_event_hook_fn)(
    fault_injection_event_case_t event_case,
    void *context);

void fault_injection_set_event_hook(fault_injection_event_hook_fn hook,
                                    void *context);

void fault_injection_register_event_quality_hook(void);

int reliability_inject(int argc, char **argv);
int reliability_inject_status(int argc, char **argv);
int reliability_inject_event(int argc, char **argv);

#endif

#endif
