#ifndef TRANSPORT_RECORDER_EVENT_LOG_H
#define TRANSPORT_RECORDER_EVENT_LOG_H

#include <stdint.h>

#include <rtthread.h>

#include "nor_flash.h"

#define EVENT_LOG_SPARSE_INDEX_INTERVAL 16U
#define EVENT_LOG_SPARSE_INDEX_CAPACITY 128U

typedef enum
{
    EVENT_LOG_UNFORMATTED = 0,
    EVENT_LOG_READY,
    EVENT_LOG_FULL,
    EVENT_LOG_ERROR,
} event_log_state_t;

typedef struct
{
    event_log_state_t state;
    uint32_t next_write_offset;
    uint32_t next_event_id;
    uint32_t committed_event_count;
} event_log_status_t;

typedef enum
{
    EVENT_LOG_RECOVERY_NONE = 0,
    EVENT_LOG_RECOVERY_SUPERBLOCK_A,
    EVENT_LOG_RECOVERY_SUPERBLOCK_B,
    EVENT_LOG_RECOVERY_DATA_SCAN,
} event_log_recovery_source_t;

typedef struct
{
    event_log_recovery_source_t source;
    uint32_t selected_generation;
    uint32_t scanned_record_count;
    uint32_t discarded_incomplete_record_count;
    uint32_t scan_ticks;
} event_log_recovery_info_t;

typedef struct
{
    uint32_t event_id;
    uint32_t record_offset;
    uint32_t record_span;
    uint32_t ev01_length;
    uint32_t ev01_crc32;
} event_log_event_info_t;

typedef struct
{
    uint32_t event_id;
    uint32_t record_offset;
} event_log_sparse_index_entry_t;

typedef struct
{
    nor_flash_t *flash;
    event_log_status_t status;
    event_log_recovery_info_t recovery;
    uint32_t generation;
    uint32_t active_record_offset;
    uint32_t active_record_span;
    uint32_t active_event_id;
    uint32_t active_ev01_length;
    uint32_t active_written;
    uint32_t active_crc32;
    event_log_sparse_index_entry_t sparse_index[EVENT_LOG_SPARSE_INDEX_CAPACITY];
    uint16_t sparse_index_count;
    event_log_event_info_t cached_event_info;
    rt_bool_t cached_event_valid;
    rt_bool_t append_active;
    /* Keep nested read/CRC work buffers off FinSH and TERP thread stacks. */
    uint8_t read_bytes[256];
    uint8_t read_event_header[160];
    uint8_t read_payload_bytes[256];
} event_log_t;

rt_err_t event_log_format(event_log_t *log, nor_flash_t *flash);
rt_err_t event_log_mount(event_log_t *log, nor_flash_t *flash);
rt_err_t event_log_append_begin(event_log_t *log,
                                uint32_t event_id,
                                uint32_t ev01_length);
rt_err_t event_log_append_write(event_log_t *log,
                                const uint8_t *data,
                                uint32_t length);
void event_log_append_abort(event_log_t *log);
rt_err_t event_log_get_status(const event_log_t *log,
                              event_log_status_t *status);
rt_err_t event_log_get_recovery_info(const event_log_t *log,
                                     event_log_recovery_info_t *info);
rt_err_t event_log_get_event_info(event_log_t *log,
                                  uint32_t event_id,
                                  event_log_event_info_t *info);
rt_err_t event_log_list_events(event_log_t *log,
                               uint32_t after_event_id,
                               uint16_t maximum_count,
                               event_log_event_info_t *events,
                               uint16_t events_capacity,
                               uint16_t *event_count,
                               uint32_t *next_event_id);
rt_err_t event_log_verify_event(event_log_t *log, uint32_t event_id);
rt_err_t event_log_read_event(event_log_t *log,
                              uint32_t event_id,
                              uint32_t ev01_offset,
                              uint8_t *data,
                              uint32_t length,
                              uint32_t *read_length);

#endif
