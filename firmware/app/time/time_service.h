#ifndef TRANSPORT_RECORDER_TIME_SERVICE_H
#define TRANSPORT_RECORDER_TIME_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIME_SERVICE_BACKUP_MARKER UINT32_C(0x54494D45)
#define TIME_SERVICE_BACKUP_INVALID_MARKER UINT32_C(0x00000000)

typedef int (*time_service_rtc_get_utc_fn)(int64_t *utc_unix_seconds,
                                           void *context);
typedef int (*time_service_rtc_set_utc_fn)(int64_t utc_unix_seconds,
                                           void *context);
typedef int (*time_service_rtc_calendar_valid_fn)(bool *calendar_valid,
                                                  void *context);
typedef int (*time_service_backup_read_fn)(uint32_t *marker, void *context);
typedef int (*time_service_backup_write_fn)(uint32_t marker, void *context);
typedef uint64_t (*time_service_monotonic_us_fn)(void *context);

typedef struct
{
    time_service_rtc_get_utc_fn rtc_get_utc;
    time_service_rtc_set_utc_fn rtc_set_utc;
    time_service_rtc_calendar_valid_fn rtc_calendar_valid;
    time_service_backup_read_fn backup_read;
    time_service_backup_write_fn backup_write;
    time_service_monotonic_us_fn monotonic_us;
    void *context;
} time_service_ops_t;

typedef struct
{
    bool utc_valid;
    bool rtc_calendar_valid;
    int64_t utc_unix_seconds;
    uint32_t epoch_id;
    uint64_t monotonic_us;
} time_service_status_t;

typedef struct
{
    time_service_ops_t ops;
    bool initialized;
    bool utc_valid;
    bool rtc_calendar_valid;
    uint32_t epoch_id;
} time_service_t;

int time_service_init(time_service_t *service, const time_service_ops_t *ops);
int time_service_get_status(time_service_t *service,
                            time_service_status_t *status);
int time_service_get_utc(time_service_t *service, int64_t *utc_unix_seconds);
int time_service_set_utc(time_service_t *service, int64_t utc_unix_seconds);
int time_service_invalidate(time_service_t *service);

#ifdef __cplusplus
}
#endif

#endif
