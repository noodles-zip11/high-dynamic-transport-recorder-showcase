#include <string.h>

#include "nor_flash_w25q.h"

#define W25Q_CMD_WRITE_ENABLE 0x06U
#define W25Q_CMD_READ_STATUS_1 0x05U
#define W25Q_CMD_READ_DATA 0x03U
#define W25Q_CMD_PAGE_PROGRAM 0x02U
#define W25Q_CMD_SECTOR_ERASE_4K 0x20U
#define W25Q_CMD_JEDEC_ID 0x9FU
#define W25Q_STATUS_BUSY 0x01U
#define W25Q_TRANSFER_MAX_BYTES 260U
#define W25Q_BUSY_POLL_LIMIT 50000U

typedef struct
{
    uint8_t jedec_id[3];
    uint32_t capacity_bytes;
    uint32_t page_bytes;
    uint32_t erase_sector_bytes;
} w25q_device_info_t;

static const w25q_device_info_t supported_devices[] = {
    {{0xEFU, 0x40U, 0x17U}, 8U * 1024U * 1024U, 256U, 4096U},
};

static rt_err_t transfer(nor_flash_w25q_t *flash,
                         const uint8_t *tx,
                         uint8_t *rx,
                         uint32_t length)
{
    if (flash == RT_NULL || flash->transfer == RT_NULL || tx == RT_NULL
        || rx == RT_NULL || length == 0U || length > W25Q_TRANSFER_MAX_BYTES)
    {
        return -RT_ERROR;
    }

    return flash->transfer(flash->transfer_context, tx, rx, length);
}

static rt_err_t write_enable(nor_flash_w25q_t *flash)
{
    uint8_t tx[1] = {W25Q_CMD_WRITE_ENABLE};
    uint8_t rx[1] = {0};

    return transfer(flash, tx, rx, sizeof(tx));
}

static rt_err_t wait_ready(nor_flash_w25q_t *flash)
{
    uint8_t tx[2] = {W25Q_CMD_READ_STATUS_1, 0xFFU};
    uint8_t rx[2] = {0};
    uint32_t polls;

    for (polls = 0U; polls < W25Q_BUSY_POLL_LIMIT; polls++)
    {
        if (transfer(flash, tx, rx, sizeof(tx)) != RT_EOK)
        {
            return -RT_ERROR;
        }
        if ((rx[1] & W25Q_STATUS_BUSY) == 0U)
        {
            return RT_EOK;
        }
    }

    return -RT_ERROR;
}

static rt_err_t w25q_read(nor_flash_t *device,
                          uint32_t offset,
                          uint8_t *data,
                          uint32_t length)
{
    nor_flash_w25q_t *flash = (nor_flash_w25q_t *)device->context;
    uint8_t tx[W25Q_TRANSFER_MAX_BYTES];
    uint8_t rx[W25Q_TRANSFER_MAX_BYTES];
    uint32_t chunk;

    if (data == RT_NULL || offset > device->capacity_bytes
        || length > device->capacity_bytes - offset)
    {
        return -RT_ERROR;
    }

    while (length != 0U)
    {
        chunk = length < W25Q_TRANSFER_MAX_BYTES - 4U
                ? length : W25Q_TRANSFER_MAX_BYTES - 4U;
        tx[0] = W25Q_CMD_READ_DATA;
        tx[1] = (uint8_t)(offset >> 16U);
        tx[2] = (uint8_t)(offset >> 8U);
        tx[3] = (uint8_t)offset;
        memset(&tx[4], 0xFF, chunk);
        if (transfer(flash, tx, rx, chunk + 4U) != RT_EOK)
        {
            return -RT_ERROR;
        }
        memcpy(data, &rx[4], chunk);
        data += chunk;
        offset += chunk;
        length -= chunk;
    }

    return RT_EOK;
}

static rt_err_t w25q_program(nor_flash_t *device,
                             uint32_t offset,
                             const uint8_t *data,
                             uint32_t length)
{
    nor_flash_w25q_t *flash = (nor_flash_w25q_t *)device->context;
    uint8_t tx[W25Q_TRANSFER_MAX_BYTES];
    uint8_t rx[W25Q_TRANSFER_MAX_BYTES] = {0};

    if (data == RT_NULL || length == 0U || length > device->page_bytes
        || offset > device->capacity_bytes || length > device->capacity_bytes - offset
        || offset / device->page_bytes != (offset + length - 1U) / device->page_bytes)
    {
        return -RT_ERROR;
    }

    if (write_enable(flash) != RT_EOK)
    {
        return -RT_ERROR;
    }
    tx[0] = W25Q_CMD_PAGE_PROGRAM;
    tx[1] = (uint8_t)(offset >> 16U);
    tx[2] = (uint8_t)(offset >> 8U);
    tx[3] = (uint8_t)offset;
    memcpy(&tx[4], data, length);
    if (transfer(flash, tx, rx, length + 4U) != RT_EOK)
    {
        return -RT_ERROR;
    }

    return wait_ready(flash);
}

static rt_err_t w25q_erase_sector(nor_flash_t *device, uint32_t offset)
{
    nor_flash_w25q_t *flash = (nor_flash_w25q_t *)device->context;
    uint8_t tx[4];
    uint8_t rx[4] = {0};

    if (offset % device->erase_sector_bytes != 0U
        || offset > device->capacity_bytes
        || device->erase_sector_bytes > device->capacity_bytes - offset
        || write_enable(flash) != RT_EOK)
    {
        return -RT_ERROR;
    }
    tx[0] = W25Q_CMD_SECTOR_ERASE_4K;
    tx[1] = (uint8_t)(offset >> 16U);
    tx[2] = (uint8_t)(offset >> 8U);
    tx[3] = (uint8_t)offset;
    if (transfer(flash, tx, rx, sizeof(tx)) != RT_EOK)
    {
        return -RT_ERROR;
    }

    return wait_ready(flash);
}

void nor_flash_w25q_init(nor_flash_w25q_t *flash,
                         nor_flash_transfer_fn transfer_fn,
                         void *transfer_context)
{
    if (flash == RT_NULL)
    {
        return;
    }

    memset(flash, 0, sizeof(*flash));
    flash->transfer = transfer_fn;
    flash->transfer_context = transfer_context;
    flash->device.context = flash;
    flash->device.read = w25q_read;
    flash->device.program = w25q_program;
    flash->device.erase_sector = w25q_erase_sector;
}

rt_err_t nor_flash_w25q_probe(nor_flash_w25q_t *flash,
                              uint8_t jedec_id[3])
{
    uint8_t tx[4] = {W25Q_CMD_JEDEC_ID, 0xFFU, 0xFFU, 0xFFU};
    uint8_t rx[4] = {0};
    uint32_t index;

    if (flash == RT_NULL || jedec_id == RT_NULL
        || transfer(flash, tx, rx, sizeof(tx)) != RT_EOK)
    {
        return -RT_ERROR;
    }

    for (index = 0U;
         index < sizeof(supported_devices) / sizeof(supported_devices[0]);
         index++)
    {
        const w25q_device_info_t *device = &supported_devices[index];

        if (memcmp(&rx[1], device->jedec_id, sizeof(device->jedec_id)) == 0)
        {
            memcpy(jedec_id, device->jedec_id, sizeof(device->jedec_id));
            flash->device.capacity_bytes = device->capacity_bytes;
            flash->device.page_bytes = device->page_bytes;
            flash->device.erase_sector_bytes = device->erase_sector_bytes;
            return RT_EOK;
        }
    }

    return -RT_ERROR;
}
