#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "fake_nor.h"

static void assert_all_erased(const uint8_t *data, uint32_t length)
{
    uint32_t index;

    for (index = 0U; index < length; index++)
    {
        assert(data[index] == 0xFFU);
    }
}

int main(void)
{
    uint8_t storage[3U * 4096U];
    uint8_t readback[32];
    uint8_t exact_write[17];
    uint8_t one_to_zero = 0xF0U;
    uint8_t further_one_to_zero = 0xE0U;
    uint8_t invalid_zero_to_one = 0xF0U;
    uint8_t cross_page[2] = {0x00U, 0x00U};
    uint32_t index;
    fake_nor_t flash = {0};

    for (index = 0U; index < sizeof(exact_write); index++)
    {
        exact_write[index] = (uint8_t)(0xF0U - index);
    }

    assert(fake_nor_init(&flash, storage, sizeof(storage)) == RT_EOK);
    assert(flash.device.read(&flash.device, 0U, readback, sizeof(readback)) == RT_EOK);
    assert_all_erased(readback, sizeof(readback));

    assert(flash.device.program(&flash.device, 0U, &one_to_zero, 1U) == RT_EOK);
    assert(flash.device.program(&flash.device, 0U, &further_one_to_zero, 1U) == RT_EOK);
    assert(flash.device.program(&flash.device, 0U, &invalid_zero_to_one, 1U) != RT_EOK);
    assert(flash.device.read(&flash.device, 0U, readback, 1U) == RT_EOK);
    assert(readback[0] == further_one_to_zero);

    assert(flash.device.program(&flash.device, 64U, exact_write,
                                sizeof(exact_write)) == RT_EOK);
    memset(readback, 0, sizeof(readback));
    assert(flash.device.read(&flash.device, 64U, readback,
                             sizeof(exact_write)) == RT_EOK);
    assert(memcmp(readback, exact_write, sizeof(exact_write)) == 0);

    assert(flash.device.program(&flash.device, 255U, cross_page,
                                sizeof(cross_page)) != RT_EOK);
    assert(flash.device.read(&flash.device, sizeof(storage) - 1U, readback, 2U) != RT_EOK);
    assert(flash.device.program(&flash.device, sizeof(storage) - 1U, cross_page, 2U)
           != RT_EOK);
    assert(flash.device.erase_sector(&flash.device, sizeof(storage)) != RT_EOK);

    fake_nor_set_busy(&flash, RT_TRUE);
    assert(flash.device.read(&flash.device, 0U, readback, 1U) != RT_EOK);
    assert(flash.device.program(&flash.device, 128U, &one_to_zero, 1U) != RT_EOK);
    assert(flash.device.erase_sector(&flash.device, 4096U) != RT_EOK);
    fake_nor_set_busy(&flash, RT_FALSE);
    assert(flash.device.read(&flash.device, 0U, readback, 1U) == RT_EOK);

    return 0;
}
