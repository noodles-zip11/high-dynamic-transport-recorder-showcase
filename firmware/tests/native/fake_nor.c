#include <string.h>

#include "fake_nor.h"

static rt_err_t fake_nor_read(nor_flash_t *flash,
                              uint32_t offset,
                              uint8_t *data,
                              uint32_t length)
{
    fake_nor_t *fake = (fake_nor_t *)flash->context;

    if (fake == RT_NULL || data == RT_NULL || fake->busy
        || offset > fake->storage_bytes
        || length > fake->storage_bytes - offset)
    {
        return -RT_ERROR;
    }

    if (fake->read_failure_armed
        && fake->successful_read_count >= fake->read_failure_after_successes)
    {
        return -RT_ERROR;
    }

    memcpy(data, &fake->storage[offset], length);
    fake->successful_read_count++;
    fake->read_call_count++;
    fake->read_byte_count += length;
    return RT_EOK;
}

static rt_err_t fake_nor_program(nor_flash_t *flash,
                                 uint32_t offset,
                                 const uint8_t *data,
                                 uint32_t length)
{
    fake_nor_t *fake = (fake_nor_t *)flash->context;
    uint32_t index;

    if (fake == RT_NULL || data == RT_NULL || fake->busy || length == 0U
        || offset > fake->storage_bytes || length > fake->storage_bytes - offset
        || (offset / flash->page_bytes) != ((offset + length - 1U) / flash->page_bytes))
    {
        return -RT_ERROR;
    }

    for (index = 0U; index < length; index++)
    {
        if ((fake->storage[offset + index] & data[index]) != data[index])
        {
            return -RT_ERROR;
        }
    }

    for (index = 0U; index < length; index++)
    {
        if (fake->power_cut_armed
            && fake->programmed_after_arm == fake->power_cut_after_bytes)
        {
            fake->power_cut_triggered = RT_TRUE;
            return -RT_ERROR;
        }
        fake->storage[offset + index] &= data[index];
        fake->programmed_after_arm++;
    }

    return RT_EOK;
}

void fake_nor_set_power_cut(fake_nor_t *fake, uint32_t after_bytes)
{
    if (after_bytes == 0U)
    {
        fake_nor_clear_power_cut(fake);
    }
    else
    {
        fake_nor_arm_power_cut(fake, after_bytes);
    }
}

void fake_nor_arm_power_cut(fake_nor_t *fake, uint32_t after_bytes)
{
    if (fake != RT_NULL)
    {
        fake->power_cut_after_bytes = after_bytes;
        fake->programmed_after_arm = 0U;
        fake->power_cut_armed = RT_TRUE;
        fake->power_cut_triggered = RT_FALSE;
    }
}

void fake_nor_clear_power_cut(fake_nor_t *fake)
{
    if (fake != RT_NULL)
    {
        fake->power_cut_armed = RT_FALSE;
        fake->power_cut_after_bytes = 0U;
        fake->programmed_after_arm = 0U;
    }
}

void fake_nor_arm_erase_power_cut(fake_nor_t *fake)
{
    if (fake != RT_NULL)
    {
        fake->erase_power_cut_armed = RT_TRUE;
        fake->erase_power_cut_triggered = RT_FALSE;
    }
}

void fake_nor_clear_erase_power_cut(fake_nor_t *fake)
{
    if (fake != RT_NULL)
    {
        fake->erase_power_cut_armed = RT_FALSE;
    }
}

void fake_nor_set_busy(fake_nor_t *fake, rt_bool_t busy)
{
    if (fake != RT_NULL)
    {
        fake->busy = busy;
    }
}

void fake_nor_set_read_failure_after(fake_nor_t *fake,
                                     uint32_t successful_read_count)
{
    if (fake != RT_NULL)
    {
        fake->read_failure_after_successes = successful_read_count;
        fake->successful_read_count = 0U;
        fake->read_failure_armed = RT_TRUE;
    }
}

void fake_nor_reset_counters(fake_nor_t *fake)
{
    if (fake != RT_NULL)
    {
        fake->read_call_count = 0U;
        fake->read_byte_count = 0U;
    }
}

static rt_err_t fake_nor_erase_sector(nor_flash_t *flash, uint32_t offset)
{
    fake_nor_t *fake = (fake_nor_t *)flash->context;

    if (fake == RT_NULL || fake->busy || offset % flash->erase_sector_bytes != 0U
        || offset > fake->storage_bytes
        || flash->erase_sector_bytes > fake->storage_bytes - offset)
    {
        return -RT_ERROR;
    }

    if (fake->erase_power_cut_armed)
    {
        fake->storage[offset] = 0x00U;
        fake->erase_power_cut_triggered = RT_TRUE;
        return -RT_ERROR;
    }

    fake->erase_call_count++;
    memset(&fake->storage[offset], 0xFF, flash->erase_sector_bytes);
    return RT_EOK;
}

rt_err_t fake_nor_init(fake_nor_t *fake,
                       uint8_t *storage,
                       uint32_t storage_bytes)
{
    if (fake == RT_NULL || storage == RT_NULL
        || storage_bytes < 3U * 4096U || storage_bytes % 4096U != 0U)
    {
        return -RT_ERROR;
    }

    memset(storage, 0xFF, storage_bytes);
    memset(fake, 0, sizeof(*fake));
    fake->storage = storage;
    fake->storage_bytes = storage_bytes;
    fake->device.context = fake;
    fake->device.capacity_bytes = storage_bytes;
    fake->device.erase_sector_bytes = 4096U;
    fake->device.page_bytes = 256U;
    fake->device.read = fake_nor_read;
    fake->device.program = fake_nor_program;
    fake->device.erase_sector = fake_nor_erase_sector;
    return RT_EOK;
}
