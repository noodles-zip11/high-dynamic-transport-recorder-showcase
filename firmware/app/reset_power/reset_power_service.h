#ifndef TRANSPORT_RECORDER_RESET_POWER_SERVICE_H
#define TRANSPORT_RECORDER_RESET_POWER_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RESET_POWER_RAW_BROWN_OUT UINT32_C(1) << 0
#define RESET_POWER_RAW_POWER_ON UINT32_C(1) << 1
#define RESET_POWER_RAW_IWDG UINT32_C(1) << 2
#define RESET_POWER_RAW_WWDG UINT32_C(1) << 3
#define RESET_POWER_RAW_SOFTWARE UINT32_C(1) << 4
#define RESET_POWER_RAW_PIN UINT32_C(1) << 5

typedef enum
{
    RESET_POWER_CAUSE_UNKNOWN = 0,
    RESET_POWER_CAUSE_BOR_OR_POR,
    RESET_POWER_CAUSE_IWDG,
    RESET_POWER_CAUSE_WWDG,
    RESET_POWER_CAUSE_SOFTWARE,
    RESET_POWER_CAUSE_PIN,
} reset_power_cause_t;

typedef int (*reset_power_read_raw_flags_fn)(uint32_t *raw_flags,
                                             void *context);
typedef int (*reset_power_clear_raw_flags_fn)(void *context);

typedef struct
{
    reset_power_read_raw_flags_fn read_raw_flags;
    reset_power_clear_raw_flags_fn clear_raw_flags;
    void *context;
} reset_power_ops_t;

typedef struct
{
    bool captured;
    uint32_t raw_flags;
    reset_power_cause_t cause;
} reset_power_status_t;

typedef struct
{
    reset_power_ops_t ops;
    bool initialized;
    reset_power_status_t status;
} reset_power_service_t;

reset_power_cause_t reset_power_normalize(uint32_t raw_flags);
int reset_power_service_init(reset_power_service_t *service,
                             const reset_power_ops_t *ops);
int reset_power_service_capture(reset_power_service_t *service);
int reset_power_service_get_status(const reset_power_service_t *service,
                                   reset_power_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
