#ifndef TRANSPORT_RECORDER_IMU_ACQUISITION_STATS_H
#define TRANSPORT_RECORDER_IMU_ACQUISITION_STATS_H

#include <stddef.h>

#include "imu_acquisition.h"

int imu_acquisition_stats_format(char *buffer,
                                 size_t buffer_size,
                                 const imu_acquisition_stats_t *stats);

#endif
