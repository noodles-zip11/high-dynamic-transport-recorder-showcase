#include "storage_service.h"

#include <stdlib.h>

#include <rtdevice.h>

#include "nor_flash_w25q.h"
#include "power_runtime.h"
#include "storage_format_confirmation.h"
#include "u8_nor_spi.h"

static nor_flash_w25q_t storage_flash;
static event_log_t storage_log;
static rt_bool_t storage_started;
static uint8_t storage_jedec_id[3];
static struct rt_mutex storage_mutex;
static rt_bool_t storage_mutex_initialized;
static storage_format_confirmation_t storage_format_confirmation;

static rt_err_t storage_service_lock(void)
{
    if (!storage_mutex_initialized
        || rt_mutex_take(&storage_mutex, RT_WAITING_FOREVER) != RT_EOK)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

static void storage_service_unlock(void)
{
    if (storage_mutex_initialized)
    {
        (void)rt_mutex_release(&storage_mutex);
    }
}

static rt_bool_t storage_service_is_ready_locked(void)
{
    return storage_started && storage_log.status.state == EVENT_LOG_READY;
}

static rt_err_t storage_service_transfer(void *context,
                                         const uint8_t *tx,
                                         uint8_t *rx,
                                         uint32_t length)
{
    (void)context;
    return board_u8_nor_spi_transfer(tx, rx, (rt_size_t)length);
}

static rt_err_t storage_service_sink_begin(void *context,
                                           uint32_t event_id,
                                           uint32_t ev01_length)
{
    (void)context;
    return storage_service_event_begin(event_id, ev01_length);
}

static rt_bool_t storage_service_sink_is_ready(void *context)
{
    (void)context;
    return storage_service_is_ready();
}

static rt_err_t storage_service_sink_abort(void *context)
{
    (void)context;
    return storage_service_event_abort();
}

static rt_err_t storage_service_sink_get_status(void *context,
                                                 event_export_sink_status_t *sink_status)
{
    event_log_status_t status;

    (void)context;
    if (sink_status == RT_NULL || storage_service_get_status(&status) != RT_EOK)
    {
        return -RT_ERROR;
    }

    sink_status->next_event_id = status.next_event_id;
    sink_status->state = (status.state == EVENT_LOG_READY)
                             ? EVENT_EXPORT_SINK_READY
                             : (status.state == EVENT_LOG_ERROR)
                                   ? EVENT_EXPORT_SINK_ERROR
                                   : EVENT_EXPORT_SINK_UNAVAILABLE;
    return RT_EOK;
}

#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
static rt_err_t storage_service_sink_verify(void *context, uint32_t event_id)
{
    (void)context;
    return storage_service_verify_event(event_id);
}

static rt_err_t storage_service_sink_read(void *context,
                                          uint32_t event_id,
                                          uint32_t ev01_offset,
                                          uint8_t *data,
                                          uint32_t length,
                                          uint32_t *read_length)
{
    (void)context;
    return storage_service_read_event(event_id, ev01_offset, data, length,
                                      read_length);
}
#endif

rt_err_t storage_service_start(void)
{
    rt_err_t result;

    if (!storage_mutex_initialized)
    {
        if (rt_mutex_init(&storage_mutex, "storage", RT_IPC_FLAG_PRIO) != RT_EOK)
        {
            return -RT_ERROR;
        }
        storage_mutex_initialized = RT_TRUE;
        storage_format_confirmation_init(&storage_format_confirmation);
    }
    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (storage_started)
    {
        storage_service_unlock();
        return RT_EOK;
    }
    if (board_u8_nor_spi_init() != RT_EOK)
    {
        storage_service_unlock();
        return -RT_ERROR;
    }

    nor_flash_w25q_init(&storage_flash, storage_service_transfer, RT_NULL);
    if (nor_flash_w25q_probe(&storage_flash, storage_jedec_id) != RT_EOK)
    {
        storage_service_unlock();
        return -RT_ERROR;
    }

    result = event_log_mount(&storage_log, &storage_flash.device);
    storage_started = RT_TRUE;
    if (result != RT_EOK && storage_log.status.state != EVENT_LOG_UNFORMATTED)
    {
        storage_service_unlock();
        return -RT_ERROR;
    }
    storage_service_unlock();
    return RT_EOK;
}

void storage_service_get_event_sink(event_export_sink_t *sink)
{
    if (sink == RT_NULL)
    {
        return;
    }

    sink->is_ready = storage_service_sink_is_ready;
    sink->begin = storage_service_sink_begin;
    sink->write = storage_service_event_write;
    sink->abort = storage_service_sink_abort;
    sink->get_status = storage_service_sink_get_status;
#ifdef TRANSPORT_RELIABILITY_EVIDENCE_ENABLED
    sink->verify = storage_service_sink_verify;
    sink->read = storage_service_sink_read;
#endif
    sink->context = RT_NULL;
}

rt_bool_t storage_service_is_ready(void)
{
    rt_bool_t ready;

    if (storage_service_lock() != RT_EOK)
    {
        return RT_FALSE;
    }
    ready = storage_service_is_ready_locked();
    storage_service_unlock();
    return ready;
}

rt_err_t storage_service_event_begin(uint32_t event_id, uint32_t ev01_length)
{
    rt_err_t result;
    rt_bool_t storage_blocker_held = RT_FALSE;

    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (!storage_service_is_ready_locked())
    {
        storage_service_unlock();
        return -RT_ERROR;
    }
    storage_blocker_held =
        power_runtime_acquire_blocker(POWER_BLOCKER_STORAGE) == RT_EOK;
    result = event_log_append_begin(&storage_log, event_id, ev01_length);
    if (storage_blocker_held)
    {
        power_runtime_release_blocker(POWER_BLOCKER_STORAGE);
    }
    storage_service_unlock();
    return result;
}

rt_err_t storage_service_event_write(const uint8_t *data,
                                     rt_size_t length,
                                     void *context)
{
    rt_err_t result;
    rt_bool_t storage_blocker_held = RT_FALSE;

    (void)context;
    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    storage_blocker_held =
        power_runtime_acquire_blocker(POWER_BLOCKER_STORAGE) == RT_EOK;
    result = event_log_append_write(&storage_log, data, (uint32_t)length);
    if (storage_blocker_held)
    {
        power_runtime_release_blocker(POWER_BLOCKER_STORAGE);
    }
    storage_service_unlock();
    return result;
}

rt_err_t storage_service_event_abort(void)
{
    rt_err_t result;
    rt_bool_t storage_blocker_held = RT_FALSE;

    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    storage_blocker_held =
        power_runtime_acquire_blocker(POWER_BLOCKER_STORAGE) == RT_EOK;
    event_log_append_abort(&storage_log);
    result = event_log_mount(&storage_log, &storage_flash.device);
    if (storage_blocker_held)
    {
        power_runtime_release_blocker(POWER_BLOCKER_STORAGE);
    }
    storage_service_unlock();
    return result;
}

rt_err_t storage_service_get_status(event_log_status_t *status)
{
    rt_err_t result;

    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (!storage_started)
    {
        storage_service_unlock();
        return -RT_ERROR;
    }
    result = event_log_get_status(&storage_log, status);
    storage_service_unlock();
    return result;
}

rt_err_t storage_service_get_recovery_info(event_log_recovery_info_t *info)
{
    rt_err_t result;

    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (!storage_started)
    {
        storage_service_unlock();
        return -RT_ERROR;
    }
    result = event_log_get_recovery_info(&storage_log, info);
    storage_service_unlock();
    return result;
}

rt_err_t storage_service_get_event_info(uint32_t event_id,
                                        event_log_event_info_t *info)
{
    rt_err_t result;

    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (!storage_service_is_ready_locked())
    {
        storage_service_unlock();
        return -RT_ERROR;
    }
    result = event_log_get_event_info(&storage_log, event_id, info);
    storage_service_unlock();
    return result;
}

rt_err_t storage_service_list_events(uint32_t after_event_id,
                                     uint16_t maximum_count,
                                     event_log_event_info_t *events,
                                     uint16_t events_capacity,
                                     uint16_t *event_count,
                                     uint32_t *next_event_id)
{
    rt_err_t result;

    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (!storage_service_is_ready_locked())
    {
        storage_service_unlock();
        return -RT_ERROR;
    }
    result = event_log_list_events(&storage_log, after_event_id, maximum_count,
                                   events, events_capacity, event_count,
                                   next_event_id);
    storage_service_unlock();
    return result;
}

rt_err_t storage_service_verify_event(uint32_t event_id)
{
    rt_err_t result;

    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (!storage_service_is_ready_locked())
    {
        storage_service_unlock();
        return -RT_ERROR;
    }
    result = event_log_verify_event(&storage_log, event_id);
    storage_service_unlock();
    return result;
}

rt_err_t storage_service_read_event(uint32_t event_id,
                                    uint32_t ev01_offset,
                                    uint8_t *data,
                                    uint32_t length,
                                    uint32_t *read_length)
{
    rt_err_t result;

    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (!storage_service_is_ready_locked())
    {
        storage_service_unlock();
        return -RT_ERROR;
    }
    result = event_log_read_event(&storage_log, event_id, ev01_offset,
                                  data, length, read_length);
    storage_service_unlock();
    return result;
}

static rt_err_t storage_service_format_locked(void)
{
    rt_err_t result;
    rt_bool_t storage_blocker_held = RT_FALSE;
    rt_bool_t ota_blocker_held = RT_FALSE;

    storage_blocker_held =
        power_runtime_acquire_blocker(POWER_BLOCKER_STORAGE) == RT_EOK;
    if (!storage_blocker_held)
    {
        return -RT_ERROR;
    }

    ota_blocker_held =
        power_runtime_acquire_blocker(POWER_BLOCKER_OTA_DIAGNOSTIC) == RT_EOK;
    if (!ota_blocker_held)
    {
        power_runtime_release_blocker(POWER_BLOCKER_STORAGE);
        return -RT_ERROR;
    }

    result = event_log_format(&storage_log, &storage_flash.device);
    power_runtime_release_blocker(POWER_BLOCKER_OTA_DIAGNOSTIC);
    power_runtime_release_blocker(POWER_BLOCKER_STORAGE);
    return result;
}

rt_err_t storage_service_request_format(rt_tick_t now)
{
    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (!storage_started)
    {
        storage_service_unlock();
        return -RT_ERROR;
    }
    storage_format_confirmation_request(&storage_format_confirmation, now);
    storage_service_unlock();
    return RT_EOK;
}

rt_err_t storage_service_confirm_format(rt_tick_t now)
{
    rt_err_t result = -RT_ERROR;
    rt_bool_t confirmed;
    rt_bool_t maintenance_entered = RT_FALSE;
    rt_err_t maintenance_result;

    if (storage_service_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    confirmed = storage_format_confirmation_confirm(&storage_format_confirmation, now);
    if (storage_started && confirmed)
    {
        maintenance_result =
            power_runtime_enter_maintenance_if_monitor(&maintenance_entered);
        if (maintenance_result == RT_EOK)
        {
            result = storage_service_format_locked();
        }
        else
        {
            result = maintenance_result;
        }
        if (maintenance_entered)
        {
            rt_err_t exit_result = power_runtime_exit_maintenance_if_owned();

            if (result == RT_EOK && exit_result != RT_EOK)
            {
                result = exit_result;
            }
        }
    }
    storage_service_unlock();
    return result;
}

static const char *storage_recovery_source_name(event_log_recovery_source_t source)
{
    switch (source)
    {
    case EVENT_LOG_RECOVERY_SUPERBLOCK_A:
        return "superblock_a";
    case EVENT_LOG_RECOVERY_SUPERBLOCK_B:
        return "superblock_b";
    case EVENT_LOG_RECOVERY_DATA_SCAN:
        return "data_scan";
    case EVENT_LOG_RECOVERY_NONE:
    default:
        return "none";
    }
}

static rt_err_t storage_parse_event_id(const char *text, uint32_t *event_id)
{
    char *end;
    unsigned long value;

    if (text == RT_NULL || event_id == RT_NULL)
    {
        return -RT_ERROR;
    }
    value = strtoul(text, &end, 0);
    if (text == end || *end != '\0' || value == 0UL || value > UINT32_MAX)
    {
        return -RT_ERROR;
    }
    *event_id = (uint32_t)value;
    return RT_EOK;
}

static int storage_log_command(int argc, char **argv)
{
    event_log_status_t status;
    event_log_recovery_info_t recovery;
    event_log_event_info_t event;
    uint32_t event_id;

    if (argc == 2 && rt_strcmp(argv[1], "status") == 0)
    {
        if (storage_service_get_status(&status) != RT_EOK)
        {
            return -RT_ERROR;
        }
        rt_kprintf("log state=%u events=%lu next_id=%lu next_offset=0x%08lx jedec=%02x%02x%02x\n",
                   (unsigned int)status.state,
                   (unsigned long)status.committed_event_count,
                   (unsigned long)status.next_event_id,
                   (unsigned long)status.next_write_offset,
                   storage_jedec_id[0], storage_jedec_id[1], storage_jedec_id[2]);
        return RT_EOK;
    }
    if (argc == 2 && rt_strcmp(argv[1], "list") == 0)
    {
        if (storage_service_get_status(&status) != RT_EOK)
        {
            return -RT_ERROR;
        }
        for (event_id = 1U; event_id < status.next_event_id; event_id++)
        {
            if (storage_service_get_event_info(event_id, &event) != RT_EOK)
            {
                return -RT_ERROR;
            }
            rt_kprintf("event=%lu offset=0x%08lx span=%lu ev01=%lu\n",
                       (unsigned long)event.event_id,
                       (unsigned long)event.record_offset,
                       (unsigned long)event.record_span,
                       (unsigned long)event.ev01_length);
        }
        return RT_EOK;
    }
    if (argc == 2 && rt_strcmp(argv[1], "inspect") == 0)
    {
        if (storage_service_get_recovery_info(&recovery) != RT_EOK)
        {
            return -RT_ERROR;
        }
        rt_kprintf("recovery source=%s generation=%lu scanned=%lu discarded=%lu ticks=%lu\n",
                   storage_recovery_source_name(recovery.source),
                   (unsigned long)recovery.selected_generation,
                   (unsigned long)recovery.scanned_record_count,
                   (unsigned long)recovery.discarded_incomplete_record_count,
                   (unsigned long)recovery.scan_ticks);
        return RT_EOK;
    }
    if (argc == 3 && rt_strcmp(argv[1], "verify") == 0
        && storage_parse_event_id(argv[2], &event_id) == RT_EOK)
    {
        if (storage_service_verify_event(event_id) != RT_EOK)
        {
            rt_kprintf("event=%lu verify=FAIL\n", (unsigned long)event_id);
            return -RT_ERROR;
        }
        rt_kprintf("event=%lu verify=OK\n", (unsigned long)event_id);
        return RT_EOK;
    }
    if (argc == 2 && rt_strcmp(argv[1], "format") == 0)
    {
        if (storage_service_request_format(rt_tick_get()) != RT_EOK)
        {
            rt_kprintf("format request rejected; storage is unavailable\n");
            return -RT_ERROR;
        }
        rt_kprintf("format pending; run log format --confirm within 10 seconds\n");
        return RT_EOK;
    }
    if (argc == 3 && rt_strcmp(argv[1], "format") == 0
        && rt_strcmp(argv[2], "--confirm") == 0)
    {
        if (storage_service_confirm_format(rt_tick_get()) != RT_EOK)
        {
            rt_kprintf("format not confirmed; run log format first\n");
            return -RT_ERROR;
        }
        return RT_EOK;
    }

    rt_kprintf("usage: log status|list|inspect|verify <event_id>|format|format --confirm\n");
    return -RT_ERROR;
}
MSH_CMD_EXPORT_ALIAS(storage_log_command, log,
                     event log status and explicit format);
