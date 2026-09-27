#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <stm32h7xx.h>

#include "power_runtime.h"

extern int power(int argc, char **argv);
extern rt_err_t power_runtime_acquire_blocker(power_blocker_t blocker);
extern void power_runtime_release_blocker(power_blocker_t blocker);
extern uint32_t power_policy_get_blocker_hold_count(power_blocker_t blocker);
extern rt_err_t power_runtime_enter_maintenance(void);
extern rt_err_t power_runtime_enter_maintenance_if_monitor(rt_bool_t *entered);
extern rt_err_t power_runtime_exit_maintenance(void);
extern rt_err_t power_runtime_exit_maintenance_if_owned(void);
extern rt_err_t power_runtime_event_active(rt_bool_t active);

SCB_Type native_scb;

static char native_trace[8];
static size_t native_trace_length;
static void (*native_idle_hook)(void);
static uint32_t native_idle_registration_count;
static rt_err_t native_idle_registration_result;
static rt_bool_t native_interrupts_enabled;
static rt_tick_t native_tick;
static uint32_t native_tick_reads_with_interrupts_enabled;
static uint8_t native_wake_selector;
static void (*native_wfi_wake_hook)(void);
static uint32_t native_interrupt_disable_count;
static uint32_t native_outer_enable_count;
static void (*native_after_outer_enable_hook)(void);
static uint32_t native_after_outer_enable_target;
static void (*native_critical_enable_hook)(void);
static uint32_t native_critical_hook_disable_target;
static uint32_t native_scheduler_critical_depth;
static uint32_t native_scheduler_enter_count;
static uint32_t native_scheduler_exit_count;
static rt_bool_t native_wfi_interrupts_enabled;
static uint32_t native_wfi_scheduler_depth;
static rt_err_t native_mode_switch_result;
static rt_err_t native_nested_maintenance_result;
static rt_bool_t native_nested_maintenance_entered;

rt_base_t native_rt_hw_interrupt_disable(void)
{
    const rt_base_t previous = native_interrupts_enabled ? 1 : 0;

    native_interrupts_enabled = RT_FALSE;
    ++native_interrupt_disable_count;
    return previous;
}

void native_rt_hw_interrupt_enable(rt_base_t level)
{
    native_interrupts_enabled = level != 0 ? RT_TRUE : RT_FALSE;
    if (level != 0)
    {
        ++native_outer_enable_count;
        if (native_after_outer_enable_hook != RT_NULL
            && native_outer_enable_count >= native_after_outer_enable_target)
        {
            void (*hook)(void) = native_after_outer_enable_hook;

            native_after_outer_enable_hook = RT_NULL;
            hook();
        }
        if (native_critical_enable_hook != RT_NULL
            && native_interrupt_disable_count
                   >= native_critical_hook_disable_target)
        {
            void (*hook)(void) = native_critical_enable_hook;

            native_critical_enable_hook = RT_NULL;
            hook();
        }
    }
}

rt_tick_t native_rt_tick_get(void)
{
    if (native_interrupts_enabled)
    {
        ++native_tick_reads_with_interrupts_enabled;
    }
    return native_tick;
}

void native_rt_enter_critical(void)
{
    ++native_scheduler_critical_depth;
    ++native_scheduler_enter_count;
}

void native_rt_exit_critical(void)
{
    if (native_scheduler_critical_depth != 0U)
    {
        --native_scheduler_critical_depth;
    }
    ++native_scheduler_exit_count;
}

void native_power_trace(char marker)
{
    if (marker == 'W' && !native_interrupts_enabled
        && native_trace_length < sizeof(native_trace))
    {
        native_trace[native_trace_length] = '!';
        ++native_trace_length;
    }
    if (native_trace_length < sizeof(native_trace))
    {
        native_trace[native_trace_length] = marker;
        ++native_trace_length;
    }
    if (marker == 'W')
    {
        native_wfi_interrupts_enabled = native_interrupts_enabled;
        native_wfi_scheduler_depth = native_scheduler_critical_depth;
    }
    if (marker == 'W' && native_wfi_wake_hook != RT_NULL)
    {
        native_wfi_wake_hook();
    }
}

static void native_record_wake_during_wfi(void)
{
    switch (native_wake_selector)
    {
    case 1U:
        power_runtime_record_wake(POWER_WAKE_IMU_INT1);
        break;
    case 2U:
        power_runtime_record_wake(POWER_WAKE_DMA);
        break;
    case 3U:
        power_runtime_record_wake(POWER_WAKE_UART);
        break;
    case 4U:
        power_runtime_record_wake(POWER_WAKE_IMU_INT1);
        power_runtime_record_wake(POWER_WAKE_UART);
        break;
    default:
        break;
    }
}

rt_err_t native_rt_thread_idle_sethook(void (*hook)(void))
{
    if (native_idle_registration_result != RT_EOK)
    {
        return native_idle_registration_result;
    }
    native_idle_hook = hook;
    ++native_idle_registration_count;
    return RT_EOK;
}

static int expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "power runtime: %s\n", message);
        return 1;
    }

    return 0;
}

static void native_switch_to_event(void)
{
    native_mode_switch_result =
        power_runtime_set_mode(POWER_MODE_EVENT_ACTIVE);
}

static void native_nested_maintenance_call(void)
{
    native_nested_maintenance_entered = RT_FALSE;
    native_nested_maintenance_result =
        power_runtime_enter_maintenance_if_monitor(
            &native_nested_maintenance_entered);
}

int main(void)
{
    power_policy_snapshot_t snapshot;

    native_scb.SCR = UINT32_C(0xFFFFFFFF);
    native_trace_length = 0U;
    memset(native_trace, 0, sizeof(native_trace));
    native_idle_hook = RT_NULL;
    native_idle_registration_count = 0U;
    native_idle_registration_result = -RT_ERROR;
    native_interrupts_enabled = RT_TRUE;
    native_tick = 100U;
    native_tick_reads_with_interrupts_enabled = 0U;
    native_wake_selector = 0U;
    native_wfi_wake_hook = RT_NULL;
    native_interrupt_disable_count = 0U;
    native_outer_enable_count = 0U;
    native_after_outer_enable_hook = RT_NULL;
    native_after_outer_enable_target = 0U;
    native_critical_enable_hook = RT_NULL;
    native_critical_hook_disable_target = 0U;
    native_scheduler_critical_depth = 0U;
    native_scheduler_enter_count = 0U;
    native_scheduler_exit_count = 0U;
    native_wfi_interrupts_enabled = RT_FALSE;
    native_wfi_scheduler_depth = 0U;
    native_mode_switch_result = -RT_ERROR;
    native_nested_maintenance_result = -RT_ERROR;
    native_nested_maintenance_entered = RT_FALSE;

    if (expect_true(power_runtime_start() == native_idle_registration_result,
                    "registration failure must be returned")
        || expect_true(power_runtime_start() == native_idle_registration_result,
                       "persistent registration failure must not pseudo-succeed")
        || expect_true(native_idle_registration_count == 0U,
                       "failed registration must not count as installed"))
    {
        return 1;
    }

    native_idle_registration_result = RT_EOK;
    if (expect_true(power_runtime_start() == RT_EOK,
                    "runtime start retry failed")
        || expect_true(power_runtime_start() == RT_EOK,
                       "runtime start must be idempotent")
        || expect_true(native_idle_registration_count == 1U,
                       "idle hook must be registered exactly once")
        || expect_true(native_idle_hook != RT_NULL, "idle hook was not installed"))
    {
        return 1;
    }

    power_runtime_get_snapshot(&snapshot);
    if (expect_true(snapshot.mode == POWER_MODE_BOOT,
                    "runtime must start in BOOT"))
    {
        return 1;
    }

    if (expect_true(native_tick_reads_with_interrupts_enabled == 0U,
                    "tick reads must occur inside the runtime critical section")
        || expect_true(power_runtime_enter_maintenance() == -RT_EINVAL,
                       "manual maintenance must reject BOOT"))
    {
        return 1;
    }

    if (expect_true(power_runtime_set_mode(POWER_MODE_MONITOR) == RT_EOK,
                    "MONITOR transition failed")
        || expect_true(power_runtime_set_mode((power_mode_t)99) == -RT_EINVAL,
                       "invalid mode must be rejected"))
    {
        return 1;
    }

    native_tick = 125U;
    power_runtime_get_snapshot(&snapshot);
    if (expect_true(native_tick_reads_with_interrupts_enabled == 0U,
                    "later tick reads must remain inside the critical section")
        || expect_true(snapshot.mode_ticks[POWER_MODE_MONITOR] == 25U,
                       "mode ticks must use an ordered elapsed interval"))
    {
        return 1;
    }

    native_tick = 150U;
    native_mode_switch_result = -RT_ERROR;
    native_after_outer_enable_target = native_outer_enable_count + 2U;
    native_after_outer_enable_hook = native_switch_to_event;
    power_runtime_get_snapshot(&snapshot);
    if (expect_true(native_mode_switch_result == RT_EOK,
                    "ordered tick test mode switch did not run")
        || expect_true(snapshot.mode_ticks[POWER_MODE_MONITOR] == 50U,
                       "elapsed ticks must be attributed before mode switch")
        || expect_true(snapshot.mode_ticks[POWER_MODE_EVENT_ACTIVE] == 0U,
                       "mode switch must not receive the prior interval")
        || expect_true(power_runtime_event_active(RT_FALSE) == RT_EOK,
                       "event test cleanup failed"))
    {
        return 1;
    }

    {
        rt_bool_t owner_entered = RT_FALSE;

        native_nested_maintenance_result = -RT_ERROR;
        native_nested_maintenance_entered = RT_FALSE;
        native_critical_hook_disable_target = native_interrupt_disable_count + 4U;
        native_critical_enable_hook = native_nested_maintenance_call;
        if (expect_true(power_runtime_enter_maintenance_if_monitor(&owner_entered)
                            == RT_EOK
                        && owner_entered,
                        "maintenance owner entry failed")
            || expect_true(native_nested_maintenance_result == RT_EOK
                           && native_nested_maintenance_entered,
                           "concurrent maintenance call must own a lease")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_MAINTENANCE,
                            "maintenance owner must retain mode"))
            || expect_true(power_policy_get_blocker_hold_count(
                               POWER_BLOCKER_OTA_DIAGNOSTIC) == 2U,
                           "each maintenance owner must hold its blocker"))
        {
            return 1;
        }
        if (expect_true(power_runtime_exit_maintenance_if_owned() == RT_EOK,
                        "first maintenance owner exit failed")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_MAINTENANCE,
                            "first owner exit must retain maintenance mode"))
            || expect_true(power_policy_get_blocker_hold_count(
                               POWER_BLOCKER_OTA_DIAGNOSTIC) == 1U,
                           "first owner exit cleared the other blocker hold")
            || expect_true(power_runtime_exit_maintenance_if_owned() == RT_EOK,
                           "second maintenance owner exit failed")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_MONITOR,
                            "last owner exit must return to MONITOR"))
            || expect_true(power_policy_get_blocker_hold_count(
                               POWER_BLOCKER_OTA_DIAGNOSTIC) == 0U,
                           "last owner exit leaked maintenance blocker"))
        {
            return 1;
        }
    }

    {
        rt_bool_t entered = RT_FALSE;

        if (expect_true(power_runtime_event_active(RT_TRUE) == RT_EOK,
                        "EVENT_ACTIVE transition failed")
            || expect_true(power_runtime_enter_maintenance_if_monitor(&entered)
                               == -RT_EINVAL
                           && entered == RT_FALSE,
                           "event activity must not be overwritten by maintenance")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_EVENT_ACTIVE,
                            "event activity mode was overwritten"))
            || expect_true(power_runtime_event_active(RT_FALSE) == RT_EOK,
                           "EVENT_ACTIVE must return to MONITOR"))
        {
            return 1;
        }
    }

    {
        const uint32_t wake_count_before = snapshot.wake_count;

        power_runtime_record_wake(POWER_WAKE_IMU_INT1);
        power_runtime_get_snapshot(&snapshot);
        if (expect_true(snapshot.wake_count == wake_count_before,
                        "wake callbacks outside Sleep must be ignored"))
        {
            return 1;
        }
    }

    native_trace_length = 0U;
    native_idle_hook();
    power_runtime_get_snapshot(&snapshot);
    if (expect_true(snapshot.mode == POWER_MODE_MONITOR,
                    "invalid mode must not change MONITOR")
        || expect_true(snapshot.sleep_attempt_count == 1U,
                       "idle hook must record an attempt")
        || expect_true(snapshot.sleep_enter_count == 1U,
                       "idle hook must enter CPU Sleep")
        || expect_true(snapshot.wake_count == 1U,
                       "idle hook must record a wake")
        || expect_true(native_wfi_interrupts_enabled == RT_TRUE,
                       "WFI must run with interrupts enabled")
        || expect_true(native_wfi_scheduler_depth == 1U,
                       "WFI must run under scheduler critical")
        || expect_true(native_scheduler_enter_count
                           == native_scheduler_exit_count,
                       "scheduler critical must be balanced")
        || expect_true(memcmp(native_trace, "DWI", 3U) == 0,
                       "idle hook must use the BSP Sleep sequence"))
    {
        return 1;
    }

    native_trace_length = 0U;
    native_wfi_wake_hook = RT_NULL;
    power_runtime_record_wake(POWER_WAKE_IMU_INT1);
    native_idle_hook();
    power_runtime_get_snapshot(&snapshot);
    if (expect_true(snapshot.last_wake_reason == POWER_WAKE_UNKNOWN,
                    "wake before sleep window must not be reused"))
    {
        return 1;
    }

    native_trace_length = 0U;
    native_wake_selector = 1U;
    native_wfi_wake_hook = native_record_wake_during_wfi;
    native_idle_hook();
    power_runtime_get_snapshot(&snapshot);
    if (expect_true(snapshot.last_wake_reason == POWER_WAKE_IMU_INT1,
                    "IMU wake reason was not retained")
        || expect_true(memcmp(native_trace, "DWI", 3U) == 0,
                       "WFI must execute with interrupts enabled"))
    {
        return 1;
    }

    native_wake_selector = 2U;
    native_idle_hook();
    power_runtime_get_snapshot(&snapshot);
    if (expect_true(snapshot.last_wake_reason == POWER_WAKE_DMA,
                    "DMA wake reason was not retained"))
    {
        return 1;
    }

    native_wake_selector = 3U;
    native_idle_hook();
    power_runtime_get_snapshot(&snapshot);
    if (expect_true(snapshot.last_wake_reason == POWER_WAKE_UART,
                    "UART wake reason was not retained"))
    {
        return 1;
    }
    {
        const uint32_t wake_count_before = snapshot.wake_count;

        native_wake_selector = 4U;
        native_idle_hook();
        power_runtime_get_snapshot(&snapshot);
        if (expect_true(snapshot.wake_count == wake_count_before + 1U,
                        "one WFI must count one wake despite tail IRQs")
            || expect_true(snapshot.last_wake_reason == POWER_WAKE_IMU_INT1,
                            "first concrete wake reason must win"))
        {
            return 1;
        }
    }
    native_wake_selector = 0U;
    native_wfi_wake_hook = RT_NULL;

    if (expect_true(power_runtime_acquire_blocker(POWER_BLOCKER_TRANSPORT)
                       == RT_EOK,
                    "runtime blocker acquire failed")
        || expect_true(power_runtime_acquire_blocker(POWER_BLOCKER_TRANSPORT)
                           == RT_EOK,
                       "runtime nested blocker acquire failed"))
    {
        return 1;
    }
    power_runtime_release_blocker(POWER_BLOCKER_TRANSPORT);
    power_runtime_get_snapshot(&snapshot);
    if (expect_true((snapshot.blockers & POWER_BLOCKER_TRANSPORT) != 0U,
                    "nested runtime release cleared outer hold"))
    {
        return 1;
    }
    power_runtime_release_blocker(POWER_BLOCKER_TRANSPORT);
    power_runtime_release_blocker(POWER_BLOCKER_TRANSPORT);

    if (expect_true(power_runtime_enter_maintenance() == RT_EOK,
                    "maintenance entry failed")
        || expect_true(power_runtime_enter_maintenance() == RT_EOK,
                       "manual maintenance on must be idempotent")
        || expect_true(power_runtime_set_mode(POWER_MODE_MONITOR) == -RT_EINVAL,
                       "generic mode set must not exit maintenance")
        || expect_true(power_runtime_exit_maintenance() == RT_EOK,
                       "maintenance exit failed")
        || expect_true(power_runtime_exit_maintenance() == RT_EOK,
                       "manual maintenance off must be idempotent")
        || expect_true(power_policy_get_blocker_hold_count(
                           POWER_BLOCKER_OTA_DIAGNOSTIC) == 0U,
                       "manual maintenance off leaked its own lease"))
    {
        return 1;
    }

    {
        rt_bool_t entered = RT_FALSE;

        if (expect_true(power_runtime_enter_maintenance_if_monitor(&entered)
                            == RT_EOK
                        && entered,
                        "monitor activity must enter maintenance automatically")
            || expect_true(power_runtime_enter_maintenance_if_monitor(&entered)
                               == RT_EOK
                           && entered,
                           "second maintenance activity must own a lease")
            || expect_true(power_policy_get_blocker_hold_count(
                               POWER_BLOCKER_OTA_DIAGNOSTIC) == 2U,
                           "automatic maintenance owners did not compose")
            || expect_true(power_runtime_exit_maintenance_if_owned() == RT_EOK,
                           "first automatic maintenance owner exit failed")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_MAINTENANCE,
                            "first automatic exit must retain maintenance mode"))
            || expect_true(power_policy_get_blocker_hold_count(
                               POWER_BLOCKER_OTA_DIAGNOSTIC) == 1U,
                           "first automatic exit cleared the other blocker hold")
            || expect_true(power_runtime_exit_maintenance_if_owned() == RT_EOK,
                           "second automatic maintenance owner exit failed")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_MONITOR,
                            "last automatic exit must return to MONITOR"))
            || expect_true(power_policy_get_blocker_hold_count(
                               POWER_BLOCKER_OTA_DIAGNOSTIC) == 0U,
                           "automatic maintenance owners leaked a blocker"))
        {
            return 1;
        }
    }

    {
        rt_bool_t entered = RT_FALSE;

        if (expect_true(power_runtime_enter_maintenance_if_monitor(&entered)
                            == RT_EOK
                        && entered,
                        "maintenance/event ordering entry failed")
            || expect_true(power_runtime_event_active(RT_TRUE) == RT_EOK,
                           "event must override maintenance")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_EVENT_ACTIVE,
                            "event did not override maintenance mode"))
            || expect_true(power_runtime_exit_maintenance_if_owned() == RT_EOK,
                           "maintenance-first exit failed")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_EVENT_ACTIVE,
                            "event mode was lost while event remained active"))
            || expect_true(power_runtime_event_active(RT_FALSE) == RT_EOK,
                           "event completion after maintenance failed")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_MONITOR,
                            "event completion did not return to MONITOR")))
        {
            return 1;
        }

        if (expect_true(power_runtime_enter_maintenance_if_monitor(&entered)
                            == RT_EOK
                        && entered,
                        "second maintenance/event ordering entry failed")
            || expect_true(power_runtime_event_active(RT_TRUE) == RT_EOK,
                           "second event must override maintenance")
            || expect_true(power_runtime_event_active(RT_FALSE) == RT_EOK,
                           "event-first completion failed")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_MAINTENANCE,
                            "event completion must retain maintenance lease"))
            || expect_true(power_runtime_exit_maintenance_if_owned() == RT_EOK,
                           "event-first maintenance exit failed")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_MONITOR,
                            "final maintenance exit did not return to MONITOR"))
            || expect_true(power_policy_get_blocker_hold_count(
                               POWER_BLOCKER_OTA_DIAGNOSTIC) == 0U,
                           "maintenance/event ordering leaked OTA blocker"))
        {
            return 1;
        }
    }

    {
        rt_bool_t entered = RT_FALSE;

        if (expect_true(power_runtime_enter_maintenance_if_monitor(&entered)
                            == RT_EOK
                        && entered,
                        "mixed maintenance lease entry failed")
            || expect_true(power_runtime_enter_maintenance() == RT_EOK,
                           "manual lease entry during automatic lease failed")
            || expect_true(power_policy_get_blocker_hold_count(
                               POWER_BLOCKER_OTA_DIAGNOSTIC) == 2U,
                           "manual and automatic leases did not compose")
            || expect_true(power_runtime_exit_maintenance() == RT_EOK,
                           "manual lease release failed")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_MAINTENANCE,
                            "manual release prematurely exited automatic lease"))
            || expect_true(power_policy_get_blocker_hold_count(
                               POWER_BLOCKER_OTA_DIAGNOSTIC) == 1U,
                           "manual release cleared automatic blocker hold")
            || expect_true(power_runtime_exit_maintenance_if_owned() == RT_EOK,
                           "automatic lease release after manual lease failed")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_MONITOR,
                            "automatic lease release did not return to MONITOR"))
            || expect_true(power_policy_get_blocker_hold_count(
                               POWER_BLOCKER_OTA_DIAGNOSTIC) == 0U,
                           "mixed maintenance leases leaked blocker"))
        {
            return 1;
        }
    }

    {
        char *sleep_off[] = {"power", "sleep", "off"};
        char *sleep_on[] = {"power", "sleep", "on"};
        char *status[] = {"power", "status"};
        char *invalid[] = {"power", "sleep", "invalid"};

        native_trace_length = 0U;
        if (expect_true(power(3, sleep_off) == RT_EOK,
                        "Sleep off command failed"))
        {
            return 1;
        }
        native_idle_hook();
        if (expect_true(native_trace_length == 0U,
                           "Sleep off must skip the BSP primitive")
            || expect_true(power(3, sleep_on) == RT_EOK,
                           "Sleep on command failed")
            || expect_true(power(2, status) == RT_EOK,
                           "status command failed")
            || expect_true(power(3, invalid) == -RT_EINVAL,
                           "invalid Sleep command must fail"))
        {
            return 1;
        }
    }

    power_runtime_set_blocker(POWER_BLOCKER_TRANSPORT, RT_TRUE);
    power_runtime_record_wake(POWER_WAKE_UART);
    power_runtime_get_snapshot(&snapshot);
    if (expect_true((snapshot.blockers & POWER_BLOCKER_TRANSPORT) != 0U,
                    "runtime blocker wrapper failed")
        || expect_true(snapshot.last_wake_reason == POWER_WAKE_IMU_INT1,
                       "wake callback outside Sleep must be ignored"))
    {
        return 1;
    }

    if (expect_true(power_runtime_enter_maintenance() == RT_EOK,
                    "maintenance re-entry failed")
        || expect_true(power_runtime_set_mode(POWER_MODE_FAULT_FALLBACK) == RT_EOK,
                       "MAINTENANCE must be able to enter FAULT_FALLBACK")
        || (power_runtime_get_snapshot(&snapshot),
            expect_true(snapshot.mode == POWER_MODE_FAULT_FALLBACK,
                        "maintenance fault transition changed the wrong mode"))
        || expect_true(power_policy_get_blocker_hold_count(
                           POWER_BLOCKER_OTA_DIAGNOSTIC) == 0U,
                       "maintenance fault transition leaked its blocker"))
    {
        return 1;
    }

    {
        rt_bool_t entered = RT_FALSE;

        if (expect_true(power_runtime_enter_maintenance_if_monitor(&entered)
                            == -RT_EINVAL
                        && entered == RT_FALSE,
                        "fault mode must not be overwritten by maintenance")
            || (power_runtime_get_snapshot(&snapshot),
                expect_true(snapshot.mode == POWER_MODE_FAULT_FALLBACK,
                            "fault mode was overwritten by maintenance")))
        {
            return 1;
        }
    }

    puts("power runtime: PASS");
    return 0;
}
