#ifndef POWER_STATUS_FORMAT_H
#define POWER_STATUS_FORMAT_H

#include "power_policy.h"

int power_status_format(char *buffer,
                        rt_size_t capacity,
                        const power_policy_snapshot_t *snapshot);

#endif
