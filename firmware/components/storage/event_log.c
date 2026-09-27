#include <string.h>

#include "event_log.h"
#include "event_log_format.h"

static uint16_t get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
           | ((uint32_t)data[1] << 8U)
           | ((uint32_t)data[2] << 16U)
           | ((uint32_t)data[3] << 24U);
}

static void put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    uint32_t index;
    uint32_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? UINT32_C(0xEDB88320) : 0U);
        }
    }

    return crc;
}

static uint32_t crc32_finish(uint32_t crc)
{
    return crc ^ UINT32_C(0xFFFFFFFF);
}

static uint32_t align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

static rt_bool_t range_is_valid(const nor_flash_t *flash,
                                uint32_t offset,
                                uint32_t length)
{
    return flash != RT_NULL && offset <= flash->capacity_bytes
           && length <= flash->capacity_bytes - offset;
}

static rt_err_t program_bytes(nor_flash_t *flash,
                              uint32_t offset,
                              const uint8_t *data,
                              uint32_t length)
{
    uint32_t chunk;
    uint32_t page_remaining;

    while (length != 0U)
    {
        page_remaining = flash->page_bytes - offset % flash->page_bytes;
        chunk = length < page_remaining ? length : page_remaining;
        if (flash->program(flash, offset, data, chunk) != RT_EOK)
        {
            return -RT_ERROR;
        }
        offset += chunk;
        data += chunk;
        length -= chunk;
    }

    return RT_EOK;
}

static rt_err_t erase_range(nor_flash_t *flash, uint32_t offset, uint32_t length)
{
    while (length != 0U)
    {
        if (flash->erase_sector(flash, offset) != RT_EOK)
        {
            return -RT_ERROR;
        }
        offset += flash->erase_sector_bytes;
        length -= flash->erase_sector_bytes;
    }

    return RT_EOK;
}

static void build_superblock(uint8_t *data,
                             uint32_t generation,
                             uint32_t next_write_offset,
                             uint32_t next_event_id)
{
    memset(data, 0xFF, EVENT_LOG_SUPERBLOCK_HEADER_BYTES);
    memcpy(data, "ELS1", 4U);
    put_u16_le(&data[4], EVENT_LOG_FORMAT_VERSION);
    put_u16_le(&data[6], EVENT_LOG_SUPERBLOCK_HEADER_BYTES);
    put_u32_le(&data[8], generation);
    put_u32_le(&data[12], next_write_offset);
    put_u32_le(&data[16], next_event_id);
    put_u32_le(&data[20], crc32_finish(crc32_update(UINT32_C(0xFFFFFFFF), data, 20U)));
}

static rt_bool_t superblock_is_valid(const nor_flash_t *flash,
                                     const uint8_t *data)
{
    uint32_t next_write_offset;

    if (memcmp(data, "ELS1", 4U) != 0
        || get_u16_le(&data[4]) != EVENT_LOG_FORMAT_VERSION
        || get_u16_le(&data[6]) != EVENT_LOG_SUPERBLOCK_HEADER_BYTES
        || get_u32_le(&data[16]) == 0U
        || get_u32_le(&data[20])
           != crc32_finish(crc32_update(UINT32_C(0xFFFFFFFF), data, 20U))
        || get_u32_le(&data[24]) != EVENT_LOG_COMMIT_VALID)
    {
        return RT_FALSE;
    }

    next_write_offset = get_u32_le(&data[12]);
    return next_write_offset >= EVENT_LOG_DATA_OFFSET
           && next_write_offset <= flash->capacity_bytes
           && next_write_offset % flash->erase_sector_bytes == 0U;
}

static rt_err_t write_superblock(event_log_t *log,
                                 uint32_t generation,
                                 uint32_t next_write_offset,
                                 uint32_t next_event_id)
{
    uint8_t data[EVENT_LOG_SUPERBLOCK_HEADER_BYTES];
    uint8_t readback[EVENT_LOG_SUPERBLOCK_HEADER_BYTES];
    uint32_t offset = generation % 2U == 0U
                      ? EVENT_LOG_SUPERBLOCK_B_OFFSET
                      : EVENT_LOG_SUPERBLOCK_A_OFFSET;
    uint8_t commit[4];

    build_superblock(data, generation, next_write_offset, next_event_id);
    put_u32_le(commit, EVENT_LOG_COMMIT_VALID);
    if (log->flash->erase_sector(log->flash, offset) != RT_EOK
        || program_bytes(log->flash, offset, data, 24U) != RT_EOK
        || program_bytes(log->flash, offset + 24U, commit, sizeof(commit)) != RT_EOK
        || log->flash->read(log->flash, offset, readback, sizeof(readback)) != RT_EOK
        || !superblock_is_valid(log->flash, readback)
        || get_u32_le(&readback[8]) != generation
        || get_u32_le(&readback[12]) != next_write_offset
        || get_u32_le(&readback[16]) != next_event_id)
    {
        return -RT_ERROR;
    }

    log->generation = generation;
    return RT_EOK;
}

static rt_bool_t event_header_is_valid(const uint8_t *header,
                                       uint32_t event_length,
                                       uint32_t expected_event_id,
                                       uint16_t *header_length_out,
                                       uint32_t *payload_crc_out)
{
    uint32_t pretrigger_samples;
    uint32_t posttrigger_samples;
    uint32_t payload_length;
    uint16_t header_length;
    uint32_t payload_crc;

    if (header == RT_NULL || header_length_out == RT_NULL || payload_crc_out == RT_NULL
        || get_u32_le(&header[8]) != expected_event_id)
    {
        return RT_FALSE;
    }

    if (memcmp(header, "EV01", 4U) == 0
        && get_u16_le(&header[4]) == 1U
        && get_u16_le(&header[6]) == 64U)
    {
        header_length = 64U;
        pretrigger_samples = get_u32_le(&header[28]);
        posttrigger_samples = get_u32_le(&header[32]);
        payload_length = get_u32_le(&header[48]);
        payload_crc = get_u32_le(&header[52]);
    }
    else if (memcmp(header, "EV02", 4U) == 0
             && get_u16_le(&header[4]) == 2U
             && get_u16_le(&header[6]) == 128U)
    {
        header_length = 128U;
        pretrigger_samples = get_u32_le(&header[40]);
        posttrigger_samples = get_u32_le(&header[44]);
        payload_length = get_u32_le(&header[60]);
        payload_crc = get_u32_le(&header[64]);
    }
    else if (memcmp(header, "EV03", 4U) == 0
             && get_u16_le(&header[4]) == 3U
             && get_u16_le(&header[6]) == 160U)
    {
        header_length = 160U;
        pretrigger_samples = get_u32_le(&header[40]);
        posttrigger_samples = get_u32_le(&header[44]);
        payload_length = get_u32_le(&header[60]);
        payload_crc = get_u32_le(&header[64]);
    }
    else
    {
        return RT_FALSE;
    }

    if (event_length < header_length
        || payload_length != event_length - header_length
        || pretrigger_samples > UINT32_MAX - posttrigger_samples
        || pretrigger_samples + posttrigger_samples > UINT32_MAX / 16U
        || payload_length != (pretrigger_samples + posttrigger_samples) * 16U)
    {
        return RT_FALSE;
    }

    *header_length_out = header_length;
    *payload_crc_out = payload_crc;
    return RT_TRUE;
}

static rt_bool_t event_payload_crc_is_valid(event_log_t *log,
                                            uint32_t event_offset,
                                            uint32_t event_length,
                                            uint16_t header_length,
                                            uint32_t expected_crc)
{
    uint32_t cursor = event_offset + header_length;
    uint32_t remaining = event_length - header_length;
    uint32_t chunk;
    uint32_t crc = UINT32_C(0xFFFFFFFF);

    while (remaining != 0U)
    {
        chunk = remaining < sizeof(log->read_payload_bytes)
                    ? remaining : sizeof(log->read_payload_bytes);
        if (log->flash->read(log->flash, cursor,
                             log->read_payload_bytes, chunk) != RT_EOK)
        {
            return RT_FALSE;
        }
        crc = crc32_update(crc, log->read_payload_bytes, chunk);
        cursor += chunk;
        remaining -= chunk;
    }

    return crc32_finish(crc) == expected_crc;
}

static rt_err_t read_record(event_log_t *log,
                            uint32_t offset,
                            event_log_event_info_t *info_out,
                            rt_bool_t *valid_out,
                            rt_bool_t *erased_out)
{
    uint8_t header[EVENT_LOG_RECORD_HEADER_BYTES];
    uint8_t footer[EVENT_LOG_RECORD_FOOTER_BYTES];
    uint32_t span;
    uint32_t event_length;
    uint32_t payload_crc;
    uint16_t event_header_length;
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    uint32_t cursor;
    uint32_t remaining;
    uint32_t chunk;
    uint32_t index;

    *valid_out = RT_FALSE;
    *erased_out = RT_FALSE;
    if (log->flash->read(log->flash, offset, header, sizeof(header)) != RT_EOK)
    {
        return -RT_ERROR;
    }

    for (index = 0U; index < sizeof(header); index++)
    {
        if (header[index] != 0xFFU)
        {
            break;
        }
    }
    if (index == sizeof(header))
    {
        *erased_out = RT_TRUE;
        return RT_EOK;
    }

    if (memcmp(header, "EL01", 4U) != 0
        || get_u16_le(&header[4]) != EVENT_LOG_FORMAT_VERSION
        || get_u16_le(&header[6]) != EVENT_LOG_RECORD_HEADER_BYTES
        || get_u32_le(&header[24])
           != crc32_finish(crc32_update(UINT32_C(0xFFFFFFFF), header, 24U)))
    {
        return RT_EOK;
    }

    span = get_u32_le(&header[8]);
    event_length = get_u32_le(&header[16]);
    if (span == 0U || span % log->flash->erase_sector_bytes != 0U
        || event_length < 64U || event_length > span - 48U
        || !range_is_valid(log->flash, offset, span)
        || log->flash->read(log->flash, offset + 32U,
                            log->read_event_header, 64U) != RT_EOK)
    {
        return RT_EOK;
    }
    if ((memcmp(log->read_event_header, "EV02", 4U) == 0
         || memcmp(log->read_event_header, "EV03", 4U) == 0)
        && (event_length < get_u16_le(&log->read_event_header[6])
            || get_u16_le(&log->read_event_header[6])
               > sizeof(log->read_event_header)
            || log->flash->read(log->flash, offset + 32U + 64U,
                                &log->read_event_header[64],
                                get_u16_le(&log->read_event_header[6]) - 64U)
               != RT_EOK))
    {
        return RT_EOK;
    }
    if (!event_header_is_valid(log->read_event_header, event_length,
                               get_u32_le(&header[12]),
                               &event_header_length, &payload_crc)
        || log->flash->read(log->flash, offset + 32U + event_length,
                            footer, sizeof(footer)) != RT_EOK
        || memcmp(footer, "ELC1", 4U) != 0
        || get_u32_le(&footer[4]) != get_u32_le(&header[20])
        || get_u32_le(&footer[8]) != EVENT_LOG_COMMIT_VALID)
    {
        return RT_EOK;
    }

    cursor = offset + 32U;
    remaining = event_length;
    while (remaining != 0U)
    {
        chunk = remaining < sizeof(log->read_bytes)
                    ? remaining : sizeof(log->read_bytes);
        if (log->flash->read(log->flash, cursor, log->read_bytes, chunk)
            != RT_EOK)
        {
            return -RT_ERROR;
        }
        crc = crc32_update(crc, log->read_bytes, chunk);
        cursor += chunk;
        remaining -= chunk;
    }
    if (crc32_finish(crc) != get_u32_le(&header[20]))
    {
        return RT_EOK;
    }
    if (!event_payload_crc_is_valid(log, offset + 32U, event_length,
                                    event_header_length, payload_crc))
    {
        return RT_EOK;
    }

    if (info_out != RT_NULL)
    {
        info_out->event_id = get_u32_le(&header[12]);
        info_out->record_offset = offset;
        info_out->record_span = span;
        info_out->ev01_length = event_length;
        info_out->ev01_crc32 = get_u32_le(&header[20]);
    }
    *valid_out = RT_TRUE;
    return RT_EOK;
}

static rt_bool_t active_event_is_valid(event_log_t *log)
{
    uint16_t event_header_length;
    uint32_t payload_crc;

    if (log->flash->read(log->flash, log->active_record_offset + 32U,
                         log->read_event_header, 64U) != RT_EOK)
    {
        return RT_FALSE;
    }
    if ((memcmp(log->read_event_header, "EV02", 4U) == 0
         || memcmp(log->read_event_header, "EV03", 4U) == 0)
        && (log->active_ev01_length < get_u16_le(&log->read_event_header[6])
            || get_u16_le(&log->read_event_header[6])
               > sizeof(log->read_event_header)
            || log->flash->read(log->flash,
                                log->active_record_offset + 32U + 64U,
                                &log->read_event_header[64],
                                get_u16_le(&log->read_event_header[6]) - 64U)
               != RT_EOK))
    {
        return RT_FALSE;
    }

    return event_header_is_valid(log->read_event_header,
                                 log->active_ev01_length,
                                 log->active_event_id, &event_header_length,
                                 &payload_crc)
           && event_payload_crc_is_valid(log, log->active_record_offset + 32U,
                                         log->active_ev01_length,
                                         event_header_length, payload_crc);
}

static rt_err_t recover_from(event_log_t *log, uint32_t offset, uint32_t next_event_id)
{
    event_log_event_info_t info;
    rt_bool_t valid;
    rt_bool_t erased;

    log->status.next_write_offset = offset;
    log->status.next_event_id = next_event_id;
    log->status.committed_event_count = next_event_id - 1U;
    while (offset + EVENT_LOG_RECORD_HEADER_BYTES <= log->flash->capacity_bytes)
    {
        if (read_record(log, offset, &info, &valid, &erased) != RT_EOK)
        {
            return -RT_ERROR;
        }
        log->recovery.scanned_record_count++;
        if (erased)
        {
            break;
        }
        if (!valid)
        {
            log->recovery.discarded_incomplete_record_count++;
            break;
        }
        if (info.event_id != log->status.next_event_id)
        {
            return -RT_ERROR;
        }
        offset += info.record_span;
        log->status.next_write_offset = offset;
        log->status.next_event_id = info.event_id + 1U;
        log->status.committed_event_count++;
    }

    log->status.state = EVENT_LOG_READY;
    return RT_EOK;
}

static rt_err_t read_committed_metadata(event_log_t *log,
                                        uint32_t offset,
                                        event_log_event_info_t *info)
{
    uint8_t header[EVENT_LOG_RECORD_HEADER_BYTES];
    uint8_t footer[EVENT_LOG_RECORD_FOOTER_BYTES];
    uint32_t span;
    uint32_t event_length;
    uint32_t event_crc;

    if (log == RT_NULL || log->flash == RT_NULL || info == RT_NULL
        || log->flash->read(log->flash, offset, header, sizeof(header)) != RT_EOK
        || memcmp(header, "EL01", 4U) != 0
        || get_u16_le(&header[4]) != EVENT_LOG_FORMAT_VERSION
        || get_u16_le(&header[6]) != EVENT_LOG_RECORD_HEADER_BYTES
        || crc32_finish(crc32_update(UINT32_C(0xFFFFFFFF), header, 24U))
           != get_u32_le(&header[24]))
    {
        return -RT_ERROR;
    }

    span = get_u32_le(&header[8]);
    event_length = get_u32_le(&header[16]);
    event_crc = get_u32_le(&header[20]);
    if (span == 0U || span % log->flash->erase_sector_bytes != 0U
        || event_length < 64U || event_length > span - 48U
        || !range_is_valid(log->flash, offset, span)
        || log->flash->read(log->flash, offset + 32U + event_length,
                            footer, sizeof(footer)) != RT_EOK
        || memcmp(footer, "ELC1", 4U) != 0
        || get_u32_le(&footer[4]) != event_crc
        || get_u32_le(&footer[8]) != EVENT_LOG_COMMIT_VALID)
    {
        return -RT_ERROR;
    }

    info->event_id = get_u32_le(&header[12]);
    info->record_offset = offset;
    info->record_span = span;
    info->ev01_length = event_length;
    info->ev01_crc32 = event_crc;
    return RT_EOK;
}

static rt_err_t rebuild_sparse_index(event_log_t *log)
{
    event_log_event_info_t info;
    uint32_t offset = EVENT_LOG_DATA_OFFSET;
    uint32_t expected_event_id = 1U;

    log->sparse_index_count = 0U;
    log->cached_event_valid = RT_FALSE;
    while (offset < log->status.next_write_offset)
    {
        if (read_committed_metadata(log, offset, &info) != RT_EOK
            || info.event_id != expected_event_id)
        {
            return -RT_ERROR;
        }
        if ((info.event_id - 1U) % EVENT_LOG_SPARSE_INDEX_INTERVAL == 0U)
        {
            if (log->sparse_index_count >= EVENT_LOG_SPARSE_INDEX_CAPACITY)
            {
                return -RT_ERROR;
            }
            log->sparse_index[log->sparse_index_count].event_id = info.event_id;
            log->sparse_index[log->sparse_index_count].record_offset = offset;
            log->sparse_index_count++;
        }
        offset += info.record_span;
        expected_event_id++;
    }

    return expected_event_id == log->status.next_event_id ? RT_EOK : -RT_ERROR;
}

rt_err_t event_log_format(event_log_t *log, nor_flash_t *flash)
{
    if (log == RT_NULL || flash == RT_NULL || flash->read == RT_NULL
        || flash->program == RT_NULL || flash->erase_sector == RT_NULL
        || flash->erase_sector_bytes != EVENT_LOG_ERASE_SECTOR_BYTES
        || flash->page_bytes == 0U || flash->capacity_bytes < EVENT_LOG_DATA_OFFSET + 4096U)
    {
        return -RT_ERROR;
    }
    if (log->append_active)
    {
        return -RT_ERROR;
    }

    memset(log, 0, sizeof(*log));
    log->flash = flash;
    if (erase_range(flash, 0U, flash->capacity_bytes) != RT_EOK
        || write_superblock(log, 1U, EVENT_LOG_DATA_OFFSET, 1U) != RT_EOK
        || write_superblock(log, 2U, EVENT_LOG_DATA_OFFSET, 1U) != RT_EOK)
    {
        log->status.state = EVENT_LOG_ERROR;
        return -RT_ERROR;
    }

    log->status.state = EVENT_LOG_READY;
    log->status.next_write_offset = EVENT_LOG_DATA_OFFSET;
    log->status.next_event_id = 1U;
    return RT_EOK;
}

rt_err_t event_log_mount(event_log_t *log, nor_flash_t *flash)
{
    uint8_t a[EVENT_LOG_SUPERBLOCK_HEADER_BYTES];
    uint8_t b[EVENT_LOG_SUPERBLOCK_HEADER_BYTES];
    const uint8_t *selected;
    rt_tick_t scan_start;
    rt_err_t result;

    if (log == RT_NULL)
    {
        return -RT_ERROR;
    }

    memset(log, 0, sizeof(*log));
    log->flash = flash;
    log->status.state = EVENT_LOG_ERROR;
    if (flash == RT_NULL || flash->read == RT_NULL
        || flash->erase_sector_bytes != EVENT_LOG_ERASE_SECTOR_BYTES
        || flash->capacity_bytes < EVENT_LOG_DATA_OFFSET + 4096U
        || flash->read(flash, EVENT_LOG_SUPERBLOCK_A_OFFSET, a, sizeof(a)) != RT_EOK
        || flash->read(flash, EVENT_LOG_SUPERBLOCK_B_OFFSET, b, sizeof(b)) != RT_EOK)
    {
        return -RT_ERROR;
    }

    scan_start = rt_tick_get();
    if (superblock_is_valid(flash, a) && superblock_is_valid(flash, b))
    {
        selected = (int32_t)(get_u32_le(&a[8]) - get_u32_le(&b[8])) > 0 ? a : b;
        log->recovery.source = selected == a ? EVENT_LOG_RECOVERY_SUPERBLOCK_A
                                             : EVENT_LOG_RECOVERY_SUPERBLOCK_B;
    }
    else if (superblock_is_valid(flash, a))
    {
        selected = a;
        log->recovery.source = EVENT_LOG_RECOVERY_SUPERBLOCK_A;
    }
    else if (superblock_is_valid(flash, b))
    {
        selected = b;
        log->recovery.source = EVENT_LOG_RECOVERY_SUPERBLOCK_B;
    }
    else
    {
        log->recovery.source = EVENT_LOG_RECOVERY_DATA_SCAN;
        result = recover_from(log, EVENT_LOG_DATA_OFFSET, 1U);
        log->recovery.scan_ticks = (uint32_t)(rt_tick_get() - scan_start);
        if (result != RT_EOK)
        {
            return -RT_ERROR;
        }
        if (log->status.committed_event_count == 0U)
        {
            log->status.state = EVENT_LOG_UNFORMATTED;
            return -RT_ERROR;
        }
        return rebuild_sparse_index(log);
    }

    log->generation = get_u32_le(&selected[8]);
    log->recovery.selected_generation = log->generation;
    result = recover_from(log, get_u32_le(&selected[12]), get_u32_le(&selected[16]));
    log->recovery.scan_ticks = (uint32_t)(rt_tick_get() - scan_start);
    if (result != RT_EOK)
    {
        return result;
    }
    return rebuild_sparse_index(log);
}

rt_err_t event_log_append_begin(event_log_t *log,
                                uint32_t event_id,
                                uint32_t ev01_length)
{
    uint8_t header[EVENT_LOG_RECORD_HEADER_BYTES];
    uint32_t span;

    if (log == RT_NULL || log->flash == RT_NULL || log->append_active
        || log->status.state != EVENT_LOG_READY || event_id != log->status.next_event_id
        || ev01_length < 64U)
    {
        return -RT_ERROR;
    }

    span = align_up(EVENT_LOG_RECORD_HEADER_BYTES + ev01_length
                    + EVENT_LOG_RECORD_FOOTER_BYTES,
                    log->flash->erase_sector_bytes);
    if (!range_is_valid(log->flash, log->status.next_write_offset, span))
    {
        log->status.state = EVENT_LOG_FULL;
        return -RT_ERROR;
    }

    if (erase_range(log->flash, log->status.next_write_offset, span) != RT_EOK)
    {
        log->status.state = EVENT_LOG_ERROR;
        return -RT_ERROR;
    }

    memset(header, 0xFF, sizeof(header));
    memcpy(header, "EL01", 4U);
    put_u16_le(&header[4], EVENT_LOG_FORMAT_VERSION);
    put_u16_le(&header[6], EVENT_LOG_RECORD_HEADER_BYTES);
    put_u32_le(&header[8], span);
    put_u32_le(&header[12], event_id);
    put_u32_le(&header[16], ev01_length);
    if (program_bytes(log->flash, log->status.next_write_offset, header, 20U) != RT_EOK)
    {
        log->status.state = EVENT_LOG_ERROR;
        return -RT_ERROR;
    }

    log->active_record_offset = log->status.next_write_offset;
    log->active_record_span = span;
    log->active_event_id = event_id;
    log->active_ev01_length = ev01_length;
    log->active_written = 0U;
    log->active_crc32 = UINT32_C(0xFFFFFFFF);
    log->append_active = RT_TRUE;
    return RT_EOK;
}

rt_err_t event_log_append_write(event_log_t *log,
                                const uint8_t *data,
                                uint32_t length)
{
    uint8_t header[EVENT_LOG_RECORD_HEADER_BYTES];
    uint8_t footer[EVENT_LOG_RECORD_FOOTER_BYTES];
    uint32_t crc;
    uint8_t commit[4];

    if (log == RT_NULL || data == RT_NULL || !log->append_active
        || length == 0U || length > log->active_ev01_length - log->active_written)
    {
        return -RT_ERROR;
    }

    if (program_bytes(log->flash, log->active_record_offset + 32U + log->active_written,
                      data, length) != RT_EOK)
    {
        log->status.state = EVENT_LOG_ERROR;
        return -RT_ERROR;
    }
    log->active_crc32 = crc32_update(log->active_crc32, data, length);
    log->active_written += length;
    if (log->active_written != log->active_ev01_length)
    {
        return RT_EOK;
    }

    if (!active_event_is_valid(log))
    {
        log->append_active = RT_FALSE;
        return -RT_ERROR;
    }

    crc = crc32_finish(log->active_crc32);
    put_u32_le(commit, EVENT_LOG_COMMIT_VALID);
    if (log->flash->read(log->flash, log->active_record_offset, header, sizeof(header)) != RT_EOK)
    {
        return -RT_ERROR;
    }
    put_u32_le(&header[20], crc);
    put_u32_le(&header[24], crc32_finish(crc32_update(UINT32_C(0xFFFFFFFF), header, 24U)));
    memset(footer, 0xFF, sizeof(footer));
    memcpy(footer, "ELC1", 4U);
    put_u32_le(&footer[4], crc);
    if (program_bytes(log->flash, log->active_record_offset + 20U, &header[20], 8U) != RT_EOK
        || program_bytes(log->flash, log->active_record_offset + 32U + log->active_ev01_length,
                         footer, 8U) != RT_EOK
        || program_bytes(log->flash, log->active_record_offset + 32U
                         + log->active_ev01_length + 8U,
                         commit, sizeof(commit)) != RT_EOK)
    {
        log->status.state = EVENT_LOG_ERROR;
        return -RT_ERROR;
    }

    log->status.next_write_offset += log->active_record_span;
    log->status.next_event_id++;
    log->status.committed_event_count++;
    if ((log->active_event_id - 1U) % EVENT_LOG_SPARSE_INDEX_INTERVAL == 0U)
    {
        if (log->sparse_index_count >= EVENT_LOG_SPARSE_INDEX_CAPACITY)
        {
            log->status.state = EVENT_LOG_ERROR;
            log->append_active = RT_FALSE;
            return -RT_ERROR;
        }
        log->sparse_index[log->sparse_index_count].event_id = log->active_event_id;
        log->sparse_index[log->sparse_index_count].record_offset = log->active_record_offset;
        log->sparse_index_count++;
    }
    log->cached_event_info.event_id = log->active_event_id;
    log->cached_event_info.record_offset = log->active_record_offset;
    log->cached_event_info.record_span = log->active_record_span;
    log->cached_event_info.ev01_length = log->active_ev01_length;
    log->cached_event_info.ev01_crc32 = crc;
    log->cached_event_valid = RT_TRUE;
    log->append_active = RT_FALSE;
    if (log->status.committed_event_count % EVENT_LOG_CHECKPOINT_INTERVAL == 0U
        && write_superblock(log, log->generation + 1U,
                            log->status.next_write_offset,
                            log->status.next_event_id) != RT_EOK)
    {
        log->status.state = EVENT_LOG_ERROR;
        return -RT_ERROR;
    }
    return RT_EOK;
}

void event_log_append_abort(event_log_t *log)
{
    if (log != RT_NULL)
    {
        log->append_active = RT_FALSE;
    }
}

rt_err_t event_log_get_status(const event_log_t *log,
                              event_log_status_t *status)
{
    if (log == RT_NULL || status == RT_NULL)
    {
        return -RT_ERROR;
    }

    *status = log->status;
    return RT_EOK;
}

rt_err_t event_log_get_recovery_info(const event_log_t *log,
                                     event_log_recovery_info_t *info)
{
    if (log == RT_NULL || info == RT_NULL)
    {
        return -RT_ERROR;
    }

    *info = log->recovery;
    return RT_EOK;
}

static rt_err_t find_event_metadata(event_log_t *log,
                                    uint32_t event_id,
                                    event_log_event_info_t *info)
{
    event_log_event_info_t candidate;
    uint32_t offset;
    uint32_t index;

    if (log == RT_NULL || log->flash == RT_NULL || info == RT_NULL
        || event_id == 0U || event_id >= log->status.next_event_id)
    {
        return -RT_ERROR;
    }

    if (log->cached_event_valid && log->cached_event_info.event_id == event_id)
    {
        *info = log->cached_event_info;
        return RT_EOK;
    }

    index = (event_id - 1U) / EVENT_LOG_SPARSE_INDEX_INTERVAL;
    if (index >= log->sparse_index_count)
    {
        return -RT_ERROR;
    }
    offset = log->sparse_index[index].record_offset;

    while (offset < log->status.next_write_offset)
    {
        if (read_committed_metadata(log, offset, &candidate) != RT_EOK)
        {
            return -RT_ERROR;
        }
        if (candidate.event_id == event_id)
        {
            log->cached_event_info = candidate;
            log->cached_event_valid = RT_TRUE;
            *info = candidate;
            return RT_EOK;
        }
        if (candidate.event_id > event_id)
        {
            return -RT_ERROR;
        }
        offset += candidate.record_span;
    }

    return -RT_ERROR;
}

rt_err_t event_log_get_event_info(event_log_t *log,
                                  uint32_t event_id,
                                  event_log_event_info_t *info)
{
    return find_event_metadata(log, event_id, info);
}

rt_err_t event_log_list_events(event_log_t *log,
                               uint32_t after_event_id,
                               uint16_t maximum_count,
                               event_log_event_info_t *events,
                               uint16_t events_capacity,
                               uint16_t *event_count,
                               uint32_t *next_event_id)
{
    event_log_event_info_t info;
    uint32_t event_id;
    uint16_t count = 0U;

    if (log == RT_NULL || events == RT_NULL || event_count == RT_NULL
        || next_event_id == RT_NULL || maximum_count == 0U
        || events_capacity == 0U || after_event_id == UINT32_MAX)
    {
        return -RT_ERROR;
    }
    event_id = after_event_id + 1U;
    if (event_id >= log->status.next_event_id)
    {
        *event_count = 0U;
        *next_event_id = 0U;
        return RT_EOK;
    }
    if (find_event_metadata(log, event_id, &info) != RT_EOK)
    {
        return -RT_ERROR;
    }
    while (event_id < log->status.next_event_id
           && count < maximum_count && count < events_capacity)
    {
        if (info.event_id != event_id)
        {
            return -RT_ERROR;
        }
        events[count++] = info;
        event_id++;
        if (event_id < log->status.next_event_id
            && count < maximum_count && count < events_capacity
            && read_committed_metadata(log, info.record_offset + info.record_span,
                                       &info) != RT_EOK)
        {
            return -RT_ERROR;
        }
    }
    if (count != 0U)
    {
        log->cached_event_info = events[count - 1U];
        log->cached_event_valid = RT_TRUE;
    }
    *event_count = count;
    *next_event_id = event_id < log->status.next_event_id
                     ? events[count - 1U].event_id : 0U;
    return RT_EOK;
}

rt_err_t event_log_verify_event(event_log_t *log, uint32_t event_id)
{
    event_log_event_info_t info;
    event_log_event_info_t verified;
    rt_bool_t valid;
    rt_bool_t erased;

    if (find_event_metadata(log, event_id, &info) != RT_EOK
        || read_record(log, info.record_offset, &verified, &valid, &erased) != RT_EOK
        || erased || !valid || verified.event_id != event_id)
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

rt_err_t event_log_read_event(event_log_t *log,
                              uint32_t event_id,
                              uint32_t ev01_offset,
                              uint8_t *data,
                              uint32_t length,
                              uint32_t *read_length)
{
    event_log_event_info_t info;
    uint32_t available;
    uint32_t actual_length;

    if (data == RT_NULL || length == 0U || read_length == RT_NULL
        || find_event_metadata(log, event_id, &info) != RT_EOK
        || ev01_offset >= info.ev01_length)
    {
        return -RT_ERROR;
    }

    available = info.ev01_length - ev01_offset;
    actual_length = length < available ? length : available;
    if (log->flash->read(log->flash, info.record_offset + EVENT_LOG_RECORD_HEADER_BYTES
                         + ev01_offset, data, actual_length) != RT_EOK)
    {
        return -RT_ERROR;
    }

    *read_length = actual_length;
    return RT_EOK;
}
