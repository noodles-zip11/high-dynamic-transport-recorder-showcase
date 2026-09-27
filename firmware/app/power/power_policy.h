#ifndef POWER_POLICY_H
#define POWER_POLICY_H

#include <stdint.h>

#include <rtthread.h>

typedef enum
{
    POWER_MODE_BOOT = 0,
    POWER_MODE_MONITOR,
    POWER_MODE_EVENT_ACTIVE,
    POWER_MODE_MAINTENANCE,
    POWER_MODE_FAULT_FALLBACK,
} power_mode_t;

#define POWER_MODE_COUNT 5U

typedef enum
{
    POWER_BLOCKER_DMA = UINT32_C(1) << 0,
    POWER_BLOCKER_EVENT = UINT32_C(1) << 1,
    POWER_BLOCKER_STORAGE = UINT32_C(1) << 2,
    POWER_BLOCKER_AI = UINT32_C(1) << 3,
    POWER_BLOCKER_TRANSPORT = UINT32_C(1) << 4,
    POWER_BLOCKER_OTA_DIAGNOSTIC = UINT32_C(1) << 5,
} power_blocker_t;

#define POWER_BLOCKER_COUNT 6U

typedef enum
{
    POWER_WAKE_UNKNOWN = 0,
    POWER_WAKE_SYSTICK,
    POWER_WAKE_IMU_INT1,
    POWER_WAKE_DMA,
    POWER_WAKE_UART,
} power_wake_reason_t;

typedef struct
{
    uint32_t hold_count;
    uint32_t acquire_count;
} power_blocker_stats_t;

typedef struct
{
    power_mode_t mode;
    uint32_t blockers;
    uint32_t sleep_attempt_count;
    uint32_t sleep_enter_count;
    uint32_t sleep_skip_count;
    uint32_t wake_count;
    power_wake_reason_t last_wake_reason;
    rt_bool_t stop_compiled;
    rt_bool_t stop_runtime_allowed;
    /*
     * Index order is the POWER_BLOCKER_* declaration order:
     * DMA, EVENT, STORAGE, AI, TRANSPORT, OTA_DIAGNOSTIC.
     */
    power_blocker_stats_t blocker_stats[POWER_BLOCKER_COUNT];
    /*
     * Index order is the POWER_MODE_* declaration order:
     * BOOT, MONITOR, EVENT_ACTIVE, MAINTENANCE, FAULT_FALLBACK.
     */
    uint32_t mode_ticks[POWER_MODE_COUNT];
} power_policy_snapshot_t;

void power_policy_init(void);
rt_err_t power_policy_set_mode(power_mode_t mode);
void power_policy_set_blocker(power_blocker_t blocker, rt_bool_t active);
rt_err_t power_policy_acquire_blocker(power_blocker_t blocker);
void power_policy_release_blocker(power_blocker_t blocker);
uint32_t power_policy_get_blocker_hold_count(power_blocker_t blocker);
uint32_t power_policy_get_blocker_acquire_count(power_blocker_t blocker);
void power_policy_record_mode_ticks(uint32_t ticks);
uint32_t power_policy_get_mode_ticks(power_mode_t mode);
rt_bool_t power_policy_can_cpu_sleep(void);
rt_bool_t power_policy_can_stop(void);
void power_policy_record_sleep_attempt(rt_bool_t entered);
void power_policy_record_wake(power_wake_reason_t reason);
void power_policy_begin_sleep_window(void);
void power_policy_finish_sleep_window(void);
void power_policy_get_snapshot(power_policy_snapshot_t *snapshot);

#endif
