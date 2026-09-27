#include "fault_injection.h"

#ifndef TRANSPORT_FAULT_INJECTION_ENABLED
#error "fault_injection.c is FaultInjection-profile only"
#endif

#include <rtthread.h>

#ifdef RT_USING_TIMER_ALL_SOFT
#error "handler-context injection requires RT-Thread hard timers"
#endif

#include "event_service.h"
#include "event_quality.h"
#include "stm32h743xx.h"

static volatile uint32_t fault_injection_armed;
static volatile uint32_t fault_injection_fault_kind;
static volatile uint32_t fault_injection_context;
static volatile uint32_t fault_injection_event_case;
static volatile uint32_t fault_injection_handler_armed;
static struct rt_timer fault_injection_handler_timer;
static uint32_t fault_injection_used;
static uint32_t fault_injection_event_used;
static uint32_t fault_injection_event_hook_registered;
static fault_injection_event_hook_fn fault_injection_event_hook;
static void *fault_injection_event_context;

typedef enum
{
    FAULT_INJECTION_CONTEXT_PSP_BASIC = 0U,
    FAULT_INJECTION_CONTEXT_MSP_BASIC = 1U,
    FAULT_INJECTION_CONTEXT_PSP_FPU = 2U,
    FAULT_INJECTION_CONTEXT_HANDLER_BASIC = 3U,
} fault_injection_context_t;

static int parse_fault_kind(const char *name,
                            fault_injection_fault_t *fault_kind)
{
    if (name == RT_NULL || fault_kind == RT_NULL)
    {
        return -RT_ERROR;
    }
    if (rt_strcmp(name, "hardfault") == 0)
    {
        *fault_kind = FAULT_INJECTION_HARDFAULT;
    }
    else if (rt_strcmp(name, "memmanage") == 0)
    {
        *fault_kind = FAULT_INJECTION_MEMMANAGE;
    }
    else if (rt_strcmp(name, "busfault") == 0)
    {
        *fault_kind = FAULT_INJECTION_BUSFAULT;
    }
    else if (rt_strcmp(name, "usagefault") == 0)
    {
        *fault_kind = FAULT_INJECTION_USAGEFAULT;
    }
    else
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

static int parse_event_case(const char *name,
                            fault_injection_event_case_t *event_case)
{
    if (name == RT_NULL || event_case == RT_NULL)
    {
        return -RT_ERROR;
    }
    if (rt_strcmp(name, "sequence-gap") == 0)
    {
        *event_case = FAULT_INJECTION_EVENT_SEQUENCE_GAP;
    }
    else if (rt_strcmp(name, "pretrigger-short") == 0)
    {
        *event_case = FAULT_INJECTION_EVENT_PRETRIGGER_SHORT;
    }
    else if (rt_strcmp(name, "duration-cap") == 0)
    {
        *event_case = FAULT_INJECTION_EVENT_DURATION_CAP;
    }
    else if (rt_strcmp(name, "pool-pressure") == 0)
    {
        *event_case = FAULT_INJECTION_EVENT_POOL_PRESSURE;
    }
    else if (rt_strcmp(name, "queue-pressure") == 0)
    {
        *event_case = FAULT_INJECTION_EVENT_QUEUE_PRESSURE;
    }
    else
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

static int parse_context(const char *name,
                         fault_injection_context_t *context)
{
    if (name == RT_NULL || context == RT_NULL)
    {
        return -RT_ERROR;
    }
    if (rt_strcmp(name, "psp-basic") == 0)
    {
        *context = FAULT_INJECTION_CONTEXT_PSP_BASIC;
    }
    else if (rt_strcmp(name, "msp-basic") == 0)
    {
        *context = FAULT_INJECTION_CONTEXT_MSP_BASIC;
    }
    else if (rt_strcmp(name, "psp-fpu") == 0)
    {
        *context = FAULT_INJECTION_CONTEXT_PSP_FPU;
    }
    else if (rt_strcmp(name, "handler-basic") == 0)
    {
        *context = FAULT_INJECTION_CONTEXT_HANDLER_BASIC;
    }
    else
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

static void fault_injection_barrier(void)
{
    __asm volatile("dsb sy\n\tisb sy" ::: "memory");
}

static void fault_injection_prepare(fault_injection_fault_t fault_kind)
{
    switch (fault_kind)
    {
    case FAULT_INJECTION_HARDFAULT:
        SCB->SHCSR &= ~SCB_SHCSR_USGFAULTENA_Msk;
        break;
    case FAULT_INJECTION_MEMMANAGE:
        SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
        MPU->CTRL = 0U;
        MPU->RNR = 0U;
        MPU->RBAR = 0U;
        MPU->RASR = MPU_RASR_XN_Msk
                    | (0U << MPU_RASR_AP_Pos)
                    | (4U << MPU_RASR_SIZE_Pos)
                    | MPU_RASR_ENABLE_Msk;
        MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
        break;
    case FAULT_INJECTION_BUSFAULT:
        SCB->SHCSR |= SCB_SHCSR_BUSFAULTENA_Msk;
        break;
    case FAULT_INJECTION_USAGEFAULT:
        SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk;
        break;
    default:
        break;
    }
    fault_injection_barrier();
}

static void fault_injection_trigger(
    fault_injection_fault_t fault_kind) __attribute__((noreturn));

static void fault_injection_trigger(
    fault_injection_fault_t fault_kind)
{
    volatile uint32_t *fault_address;

    switch (fault_kind)
    {
    case FAULT_INJECTION_HARDFAULT:
    case FAULT_INJECTION_USAGEFAULT:
        __asm volatile("udf #0" ::: "memory");
        break;
    case FAULT_INJECTION_MEMMANAGE:
        fault_address = (volatile uint32_t *)(uintptr_t)0x00000000U;
        (void)*fault_address;
        break;
    case FAULT_INJECTION_BUSFAULT:
        fault_address = (volatile uint32_t *)(uintptr_t)0xDEADBEE0U;
        (void)*fault_address;
        break;
    default:
        break;
    }
    for (;;)
    {
    }
}

static void fault_injection_trigger_msp(void)
    __attribute__((naked, noreturn));

static void fault_injection_trigger_msp(void)
{
    __asm volatile(
        "mrs r0, control\n\t"
        "bic r0, r0, #2\n\t"
        "msr control, r0\n\t"
        "isb\n\t"
        "udf #0\n\t"
        "b .\n\t");
}

static void fault_injection_activate_fpu(void)
{
    const uint32_t marker = UINT32_C(0x3F800000);

    __asm volatile("vmov s0, %0" : : "r"(marker) : "s0", "memory");
    fault_injection_barrier();
}

static void fault_injection_handler_callback(void *context)
{
    (void)context;
    if (fault_injection_handler_armed == 0U)
    {
        return;
    }
    fault_injection_handler_armed = 0U;
    fault_injection_trigger(FAULT_INJECTION_HARDFAULT);
}

static int fault_injection_trigger_handler(void)
{
    rt_timer_init(&fault_injection_handler_timer,
                  "fi_hdlr",
                  fault_injection_handler_callback,
                  RT_NULL,
                  1U,
                  RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_HARD_TIMER);
    fault_injection_handler_armed = 1U;
    if (rt_timer_start(&fault_injection_handler_timer) != RT_EOK)
    {
        fault_injection_handler_armed = 0U;
        rt_kprintf("handler-context setup failed\n");
        rt_timer_detach(&fault_injection_handler_timer);
        return -RT_ERROR;
    }
    fault_injection_barrier();
    for (;;)
    {
    }
}

void fault_injection_set_event_hook(fault_injection_event_hook_fn hook,
                                    void *context)
{
    fault_injection_event_hook = hook;
    fault_injection_event_context = context;
}

static int fault_injection_event_quality_hook(
    fault_injection_event_case_t event_case,
    void *context)
{
    event_quality_result_t result;

    (void)context;
    if (event_case == FAULT_INJECTION_EVENT_SEQUENCE_GAP
        || event_case == FAULT_INJECTION_EVENT_PRETRIGGER_SHORT
        || event_case == FAULT_INJECTION_EVENT_DURATION_CAP)
    {
        return event_service_inject_quality_case((uint32_t)event_case);
    }
    if (event_case == FAULT_INJECTION_EVENT_POOL_PRESSURE)
    {
        return event_service_inject_quality_case(
            EVENT_QUALITY_FAULT_POOL_PRESSURE);
    }
    if (event_case == FAULT_INJECTION_EVENT_QUEUE_PRESSURE)
    {
        return event_service_inject_queue_pressure();
    }
    return event_quality_assess_fault_injection_case(
        (uint32_t)event_case,
        &result);
}

void fault_injection_register_event_quality_hook(void)
{
    if (fault_injection_event_hook_registered == 0U)
    {
        fault_injection_set_event_hook(
            fault_injection_event_quality_hook,
            RT_NULL);
        fault_injection_event_hook_registered = 1U;
    }
}

int reliability_inject(int argc, char **argv)
{
    fault_injection_fault_t fault_kind;
    fault_injection_context_t context = FAULT_INJECTION_CONTEXT_PSP_BASIC;
    const char *confirmation;

    if (argv == RT_NULL || (argc != 3 && argc != 4)
        || parse_fault_kind(argv[1], &fault_kind) != RT_EOK)
    {
        rt_kprintf("usage: reliability_inject "
                   "<hardfault|memmanage|busfault|usagefault> "
                   "[psp-basic|msp-basic|psp-fpu|handler-basic] "
                   "CONFIRM_RESET\n");
        return -RT_ERROR;
    }
    confirmation = argv[argc - 1];
    if (rt_strcmp(confirmation, "CONFIRM_RESET") != 0
        || (argc == 4 && parse_context(argv[2], &context) != RT_EOK)
        || (context != FAULT_INJECTION_CONTEXT_PSP_BASIC
            && fault_kind != FAULT_INJECTION_HARDFAULT))
    {
        rt_kprintf("special contexts require hardfault and CONFIRM_RESET\n");
        return -RT_ERROR;
    }
    if (fault_injection_used != 0U)
    {
        return -RT_EBUSY;
    }

    fault_injection_prepare(fault_kind);
    rt_kprintf("reliability_inject armed kind=%s evidence_level=hil_injection\n",
               argv[1]);
    fault_injection_fault_kind = fault_kind;
    fault_injection_context = context;
    fault_injection_used = 1U;
    fault_injection_armed = 1U;
    fault_injection_barrier();
    if (context == FAULT_INJECTION_CONTEXT_MSP_BASIC)
    {
        fault_injection_trigger_msp();
    }
    if (context == FAULT_INJECTION_CONTEXT_PSP_FPU)
    {
        fault_injection_activate_fpu();
        fault_injection_trigger(fault_kind);
    }
    if (context == FAULT_INJECTION_CONTEXT_HANDLER_BASIC)
    {
        if (fault_injection_trigger_handler() != RT_EOK)
        {
            fault_injection_armed = 0U;
            fault_injection_used = 0U;
            return -RT_ERROR;
        }
    }
    fault_injection_trigger(fault_kind);
}

int reliability_inject_status(int argc, char **argv)
{
    if (argc != 1 || argv == RT_NULL)
    {
        return -RT_ERROR;
    }
    rt_kprintf("reliability_inject status armed=%lu fault=%lu context=%lu "
               "event=%lu "
               "evidence_level=hil_injection\n",
               (unsigned long)fault_injection_armed,
               (unsigned long)fault_injection_fault_kind,
               (unsigned long)fault_injection_context,
               (unsigned long)fault_injection_event_case);
    return RT_EOK;
}

int reliability_inject_event(int argc, char **argv)
{
    fault_injection_event_case_t event_case;

    if (argc != 3 || argv == RT_NULL
        || parse_event_case(argv[1], &event_case) != RT_EOK
        || rt_strcmp(argv[2], "CONFIRM_EVENT") != 0)
    {
        rt_kprintf("usage: reliability_inject_event "
                   "<sequence-gap|pretrigger-short|duration-cap|"
                   "pool-pressure|queue-pressure> CONFIRM_EVENT\n");
        return -RT_ERROR;
    }
    if (fault_injection_event_used != 0U
        || fault_injection_event_hook == RT_NULL)
    {
        return -RT_EBUSY;
    }
    fault_injection_event_used = 1U;
    fault_injection_event_case = event_case;
    rt_kprintf("reliability_inject_event armed case=%s "
               "evidence_level=hil_injection\n",
               argv[1]);
    return fault_injection_event_hook(event_case,
                                      fault_injection_event_context);
}

MSH_CMD_EXPORT(reliability_inject,
               reliability processor-fault injection; CONFIRM_RESET required);
MSH_CMD_EXPORT(reliability_inject_status,
               reliability injection status readback);
MSH_CMD_EXPORT(reliability_inject_event,
               reliability event injection; CONFIRM_EVENT required);
