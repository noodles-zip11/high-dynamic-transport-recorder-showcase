#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "nor_flash_w25q.h"

typedef struct
{
    uint8_t jedec_id[3];
    uint8_t command_log[16];
    uint32_t command_count;
    uint32_t busy_status_reads;
    uint32_t page_program_offset;
    uint32_t page_program_length;
    uint32_t erase_offset;
} spi_mock_t;

static rt_err_t mock_transfer(void *context,
                              const uint8_t *tx,
                              uint8_t *rx,
                              uint32_t length)
{
    spi_mock_t *mock = context;

    if (mock == RT_NULL || tx == RT_NULL || rx == RT_NULL || length == 0U
        || mock->command_count >= sizeof(mock->command_log))
    {
        return -RT_ERROR;
    }

    mock->command_log[mock->command_count++] = tx[0];
    memset(rx, 0, length);
    if (tx[0] == 0x9FU && length == 4U)
    {
        rx[1] = mock->jedec_id[0];
        rx[2] = mock->jedec_id[1];
        rx[3] = mock->jedec_id[2];
        return RT_EOK;
    }
    if (tx[0] == 0x05U && length == 2U)
    {
        if (mock->busy_status_reads != 0U)
        {
            mock->busy_status_reads--;
            rx[1] = 0x01U;
        }
        return RT_EOK;
    }
    if (tx[0] == 0x06U && length == 1U)
    {
        return RT_EOK;
    }
    if (tx[0] == 0x02U && length >= 5U)
    {
        mock->page_program_offset = ((uint32_t)tx[1] << 16U)
                                    | ((uint32_t)tx[2] << 8U) | tx[3];
        mock->page_program_length = length - 4U;
        return RT_EOK;
    }
    if (tx[0] == 0x20U && length == 4U)
    {
        mock->erase_offset = ((uint32_t)tx[1] << 16U)
                             | ((uint32_t)tx[2] << 8U) | tx[3];
        return RT_EOK;
    }

    return -RT_ERROR;
}

static void assert_probe_rejected(const uint8_t jedec_id[3])
{
    nor_flash_w25q_t flash = {0};
    spi_mock_t mock = {0};
    uint8_t observed[3] = {0xAAU, 0xAAU, 0xAAU};

    memcpy(mock.jedec_id, jedec_id, sizeof(mock.jedec_id));
    nor_flash_w25q_init(&flash, mock_transfer, &mock);
    assert(nor_flash_w25q_probe(&flash, observed) != RT_EOK);
    assert(flash.device.capacity_bytes == 0U);
    assert(flash.device.page_bytes == 0U);
    assert(flash.device.erase_sector_bytes == 0U);
}

int main(void)
{
    nor_flash_w25q_t flash = {0};
    spi_mock_t mock = {
        .jedec_id = {0xEFU, 0x40U, 0x17U},
    };
    uint8_t jedec_id[3] = {0};
    uint8_t data[4] = {1U, 2U, 3U, 4U};

    nor_flash_w25q_init(&flash, mock_transfer, &mock);
    assert(flash.device.capacity_bytes == 0U);
    assert(flash.device.page_bytes == 0U);
    assert(flash.device.erase_sector_bytes == 0U);
    assert(nor_flash_w25q_probe(&flash, jedec_id) == RT_EOK);
    assert(memcmp(jedec_id, mock.jedec_id, sizeof(jedec_id)) == 0);
    assert(flash.device.capacity_bytes == 8U * 1024U * 1024U);
    assert(flash.device.page_bytes == 256U);
    assert(flash.device.erase_sector_bytes == 4096U);
    assert(mock.command_count == 1U && mock.command_log[0] == 0x9FU);

    mock.command_count = 0U;
    mock.busy_status_reads = 1U;
    assert(flash.device.program(&flash.device, 0x1234U, data, sizeof(data)) == RT_EOK);
    assert(mock.command_count == 4U);
    assert(mock.command_log[0] == 0x06U && mock.command_log[1] == 0x02U
           && mock.command_log[2] == 0x05U && mock.command_log[3] == 0x05U);
    assert(mock.page_program_offset == 0x1234U);
    assert(mock.page_program_length == sizeof(data));
    assert(flash.device.program(&flash.device, 0x00FFU, data, sizeof(data)) != RT_EOK);

    mock.command_count = 0U;
    assert(flash.device.erase_sector(&flash.device, 0x3000U) == RT_EOK);
    assert(mock.command_count == 3U);
    assert(mock.command_log[0] == 0x06U && mock.command_log[1] == 0x20U
           && mock.command_log[2] == 0x05U);
    assert(mock.erase_offset == 0x3000U);

    {
        static const uint8_t rejected_ids[][3] = {
            {0xC8U, 0x40U, 0x17U},
            {0xEFU, 0x60U, 0x17U},
            {0xEFU, 0x40U, 0x16U},
            {0x00U, 0x00U, 0x00U},
            {0xFFU, 0xFFU, 0xFFU},
        };
        uint32_t index;

        for (index = 0U; index < sizeof(rejected_ids) / sizeof(rejected_ids[0]); index++)
        {
            assert_probe_rejected(rejected_ids[index]);
        }
    }

    return 0;
}
