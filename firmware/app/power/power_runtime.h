#ifndef POWER_RUNTIME_H
#define POWER_RUNTIME_H

#include "power_policy.h"

rt_err_t power_runtime_start(void);
rt_err_t power_runtime_set_mode(power_mode_t mode);
void power_runtime_set_blocker(power_blocker_t blocker, rt_bool_t active);
rt_err_t power_runtime_acquire_blocker(power_blocker_t blocker);
void power_runtime_release_blocker(power_blocker_t blocker);
rt_err_t power_runtime_enter_maintenance(void);
rt_err_t power_runtime_enter_maintenance_if_monitor(rt_bool_t *entered);
rt_err_t power_runtime_exit_maintenance(void);
rt_err_t power_runtime_exit_maintenance_if_owned(void);
rt_err_t power_runtime_event_active(rt_bool_t active);
void power_runtime_record_wake(power_wake_reason_t reason);
void power_runtime_get_snapshot(power_policy_snapshot_t *snapshot);

#endif
