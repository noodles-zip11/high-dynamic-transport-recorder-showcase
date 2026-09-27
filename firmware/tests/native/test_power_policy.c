#include <stdint.h>
#include <stdio.h>

#include "power_policy.h"

extern rt_err_t power_policy_acquire_blocker(power_blocker_t blocker);
extern void power_policy_release_blocker(power_blocker_t blocker);
extern uint32_t power_policy_get_blocker_hold_count(power_blocker_t blocker);
extern uint32_t power_policy_get_blocker_acquire_count(power_blocker_t blocker);
extern void power_policy_record_mode_ticks(uint32_t ticks);
extern uint32_t power_policy_get_mode_ticks(power_mode_t mode);

static rt_bool_t native_interrupts_enabled = RT_TRUE;

rt_base_t native_rt_hw_interrupt_disable(void)
{
    const rt_base_t previous = native_interrupts_enabled ? 1 : 0;

    native_interrupts_enabled = RT_FALSE;
    return previous;
}

void native_rt_hw_interrupt_enable(rt_base_t level)
{
    native_interrupts_enabled = level != 0 ? RT_TRUE : RT_FALSE;
}

static int expect_true(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "power policy: %s\n", message);
        return 1;
    }

    return 0;
}

static int expect_snapshot(const power_policy_snapshot_t *snapshot,
                           power_mode_t mode,
                           uint32_t blockers,
                           uint32_t attempts,
                           uint32_t enters,
                           uint32_t skips,
                           uint32_t wakes,
                           power_wake_reason_t last_wake)
{
    return expect_true(snapshot->mode == mode, "mode mismatch")
        || expect_true(snapshot->blockers == blockers, "blocker mask mismatch")
        || expect_true(snapshot->sleep_attempt_count == attempts, "attempt count mismatch")
        || expect_true(snapshot->sleep_enter_count == enters, "enter count mismatch")
        || expect_true(snapshot->sleep_skip_count == skips, "skip count mismatch")
        || expect_true(snapshot->wake_count == wakes, "wake count mismatch")
        || expect_true(snapshot->last_wake_reason == last_wake, "wake reason mismatch");
}

int main(void)
{
    power_policy_snapshot_t snapshot;
    const power_blocker_t blockers[] = {
        POWER_BLOCKER_DMA,
        POWER_BLOCKER_EVENT,
        POWER_BLOCKER_STORAGE,
        POWER_BLOCKER_AI,
        POWER_BLOCKER_TRANSPORT,
        POWER_BLOCKER_OTA_DIAGNOSTIC,
    };

    power_policy_init();
    power_policy_get_snapshot(&snapshot);
    if (expect_snapshot(&snapshot,
                        POWER_MODE_BOOT,
                        0U,
                        0U,
                        0U,
                        0U,
                        0U,
                        POWER_WAKE_UNKNOWN)
        || expect_true(snapshot.stop_compiled == RT_FALSE, "Stop must not be compiled")
        || expect_true(snapshot.stop_runtime_allowed == RT_FALSE, "Stop must be disabled")
        || expect_true(power_policy_can_cpu_sleep() == RT_FALSE,
                       "CPU Sleep must be disabled during BOOT")
        || expect_true(power_policy_can_stop() == RT_FALSE,
                       "Stop must always be disabled"))
    {
        return 1;
    }

    if (expect_true(power_policy_set_mode((power_mode_t)99) == -RT_EINVAL,
                    "invalid mode must be rejected"))
    {
        return 1;
    }

    power_policy_get_snapshot(&snapshot);
    if (expect_true(snapshot.mode == POWER_MODE_BOOT,
                    "invalid mode must not change the current mode"))
    {
        return 1;
    }

    if (expect_true(power_policy_set_mode(POWER_MODE_EVENT_ACTIVE) == -RT_EINVAL,
                    "BOOT must reject EVENT_ACTIVE")
        || expect_true(power_policy_set_mode(POWER_MODE_MONITOR) == RT_EOK,
                       "BOOT must allow MONITOR")
        || expect_true(power_policy_set_mode(POWER_MODE_EVENT_ACTIVE) == RT_EOK,
                       "MONITOR must allow EVENT_ACTIVE")
        || expect_true(power_policy_set_mode(POWER_MODE_MONITOR) == RT_EOK,
                       "EVENT_ACTIVE must return to MONITOR")
        || expect_true(power_policy_set_mode(POWER_MODE_MAINTENANCE) == RT_EOK,
                       "MONITOR must allow MAINTENANCE")
        || expect_true(power_policy_set_mode(POWER_MODE_EVENT_ACTIVE) == RT_EOK,
                       "MAINTENANCE must allow EVENT_ACTIVE override")
        || expect_true(power_policy_set_mode(POWER_MODE_MAINTENANCE) == RT_EOK,
                       "EVENT_ACTIVE must return to MAINTENANCE")
        || expect_true(power_policy_set_mode(POWER_MODE_MONITOR) == RT_EOK,
                       "MAINTENANCE must exit only to MONITOR")
        || expect_true(power_policy_set_mode(POWER_MODE_FAULT_FALLBACK) == RT_EOK,
                       "MONITOR must allow FAULT_FALLBACK")
        || expect_true(power_policy_set_mode(POWER_MODE_MONITOR) == -RT_EBUSY,
                       "FAULT_FALLBACK must be sticky")
        || expect_true(power_policy_set_mode(POWER_MODE_MAINTENANCE) == -RT_EBUSY,
                       "FAULT_FALLBACK must reject maintenance")
        || expect_true(power_policy_set_mode(POWER_MODE_FAULT_FALLBACK) == RT_EOK,
                       "FAULT_FALLBACK must allow idempotent set"))
    {
        return 1;
    }

    power_policy_init();
    if (expect_true(power_policy_set_mode(POWER_MODE_MONITOR) == RT_EOK,
                    "blocker test MONITOR transition failed")
        || expect_true(power_policy_acquire_blocker(POWER_BLOCKER_TRANSPORT)
                           == RT_EOK,
                       "first blocker acquire failed")
        || expect_true(power_policy_acquire_blocker(POWER_BLOCKER_TRANSPORT)
                           == RT_EOK,
                       "nested blocker acquire failed")
        || expect_true(power_policy_get_blocker_hold_count(
                           POWER_BLOCKER_TRANSPORT) == 2U,
                       "nested blocker hold count mismatch")
        || expect_true(power_policy_get_blocker_acquire_count(
                           POWER_BLOCKER_TRANSPORT) == 2U,
                       "blocker acquire count mismatch"))
    {
        return 1;
    }

    power_policy_release_blocker(POWER_BLOCKER_TRANSPORT);
    power_policy_get_snapshot(&snapshot);
    if (expect_true((snapshot.blockers & POWER_BLOCKER_TRANSPORT) != 0U,
                    "inner blocker release cleared outer hold")
        || expect_true(power_policy_get_blocker_hold_count(
                           POWER_BLOCKER_TRANSPORT) == 1U,
                       "inner blocker release count mismatch"))
    {
        return 1;
    }
    power_policy_release_blocker(POWER_BLOCKER_TRANSPORT);
    power_policy_release_blocker(POWER_BLOCKER_TRANSPORT);
    power_policy_get_snapshot(&snapshot);
    if (expect_true((snapshot.blockers & POWER_BLOCKER_TRANSPORT) == 0U,
                    "final blocker release did not clear bit")
        || expect_true(power_policy_get_blocker_hold_count(
                           POWER_BLOCKER_TRANSPORT) == 0U,
                       "blocker hold count underflowed")
        || expect_true(power_policy_get_blocker_acquire_count(
                           POWER_BLOCKER_TRANSPORT) == 2U,
                       "blocker acquire total changed on release"))
    {
        return 1;
    }

    power_policy_record_mode_ticks(UINT32_MAX);
    power_policy_record_mode_ticks(1U);
    if (expect_true(power_policy_get_mode_ticks(POWER_MODE_MONITOR)
                       == UINT32_MAX,
                    "mode tick counter must saturate"))
    {
        return 1;
    }

    if (expect_true(power_policy_set_mode(POWER_MODE_MONITOR) == RT_EOK,
                    "MONITOR transition failed")
        || expect_true(power_policy_can_cpu_sleep() == RT_TRUE,
                       "CPU Sleep must be allowed in MONITOR"))
    {
        return 1;
    }

    for (size_t index = 0U; index < sizeof(blockers) / sizeof(blockers[0]); ++index)
    {
        power_policy_set_blocker(blockers[index], RT_TRUE);
        power_policy_set_blocker(blockers[index], RT_TRUE);
        power_policy_get_snapshot(&snapshot);
        if (expect_true((snapshot.blockers & blockers[index]) != 0U,
                        "setting a blocker must set its bit"))
        {
            return 1;
        }

        power_policy_set_blocker(blockers[index], RT_FALSE);
        power_policy_set_blocker(blockers[index], RT_FALSE);
        power_policy_get_snapshot(&snapshot);
        if (expect_true((snapshot.blockers & blockers[index]) == 0U,
                        "clearing a blocker must clear its bit"))
        {
            return 1;
        }
    }

    power_policy_set_blocker(POWER_BLOCKER_DMA, RT_TRUE);
    power_policy_set_blocker(POWER_BLOCKER_EVENT, RT_TRUE);
    power_policy_get_snapshot(&snapshot);
    if (expect_true(snapshot.blockers == (POWER_BLOCKER_DMA | POWER_BLOCKER_EVENT),
                    "nested blockers must coexist")
        || expect_true(power_policy_can_cpu_sleep() == RT_TRUE,
                       "blockers must not disable ordinary CPU Sleep"))
    {
        return 1;
    }

    power_policy_set_blocker(POWER_BLOCKER_DMA, RT_FALSE);
    power_policy_set_blocker(POWER_BLOCKER_EVENT, RT_FALSE);

    if (expect_true(power_policy_set_mode(POWER_MODE_EVENT_ACTIVE) == RT_EOK,
                    "EVENT_ACTIVE transition failed")
        || expect_true(power_policy_can_cpu_sleep() == RT_TRUE,
                       "CPU Sleep must remain available in EVENT_ACTIVE")
        || expect_true(power_policy_can_stop() == RT_FALSE,
                       "Stop must remain disabled in EVENT_ACTIVE")
        || expect_true(power_policy_set_mode(POWER_MODE_MAINTENANCE) == RT_EOK,
                       "MAINTENANCE transition failed")
        || expect_true(power_policy_can_cpu_sleep() == RT_TRUE,
                       "CPU Sleep must remain available in MAINTENANCE")
        || expect_true(power_policy_set_mode(POWER_MODE_FAULT_FALLBACK) == RT_EOK,
                       "FAULT_FALLBACK transition failed")
        || expect_true(power_policy_can_stop() == RT_FALSE,
                       "FAULT_FALLBACK must prevent Stop"))
    {
        return 1;
    }

    power_policy_record_sleep_attempt(RT_FALSE);
    power_policy_record_sleep_attempt(RT_TRUE);
    power_policy_get_snapshot(&snapshot);
    {
        const uint32_t wake_count_before = snapshot.wake_count;

        power_policy_record_wake(POWER_WAKE_IMU_INT1);
        power_policy_get_snapshot(&snapshot);
        if (expect_true(snapshot.wake_count == wake_count_before,
                        "wake outside a Sleep window must not be counted"))
        {
            return 1;
        }
    }
    power_policy_begin_sleep_window();
    power_policy_record_wake(POWER_WAKE_IMU_INT1);
    power_policy_record_wake(POWER_WAKE_UART);
    power_policy_finish_sleep_window();
    power_policy_get_snapshot(&snapshot);
    if (expect_snapshot(&snapshot,
                        POWER_MODE_FAULT_FALLBACK,
                        0U,
                        2U,
                        1U,
                        1U,
                        1U,
                        POWER_WAKE_IMU_INT1))
    {
        return 1;
    }

    puts("power policy: PASS");
    return 0;
}
