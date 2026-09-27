#include "ota_qspi_candidate.h"

#include <rtthread.h>

#include "memory_layout.h"
#include "project_config.h"
#include "stm32h7xx_hal.h"

#define OTA_QSPI_JEDEC UINT32_C(0xEF4017)
#define OTA_QSPI_TIMEOUT_MS UINT32_C(1000)
#define OTA_QSPI_PAGE_BYTES UINT32_C(256)

static QSPI_HandleTypeDef ota_qspi;
static bool ota_qspi_ready;
static struct rt_mutex ota_qspi_bus_mutex;
static bool ota_qspi_bus_mutex_initialized;

static bool ota_qspi_prepare_bus_mutex(void)
{
    rt_base_t level;
    bool initialized;

    if (ota_qspi_bus_mutex_initialized)
    {
        return true;
    }
    level = rt_hw_interrupt_disable();
    if (!ota_qspi_bus_mutex_initialized
        && rt_mutex_init(&ota_qspi_bus_mutex, "qspi", RT_IPC_FLAG_PRIO)
           == RT_EOK)
    {
        ota_qspi_bus_mutex_initialized = true;
    }
    initialized = ota_qspi_bus_mutex_initialized;
    rt_hw_interrupt_enable(level);
    return initialized;
}

bool ota_qspi_bus_lock(void)
{
    return ota_qspi_prepare_bus_mutex()
           && rt_mutex_take(&ota_qspi_bus_mutex, RT_WAITING_FOREVER) == RT_EOK;
}

void ota_qspi_bus_unlock(void)
{
    if (ota_qspi_bus_mutex_initialized)
    {
        (void)rt_mutex_release(&ota_qspi_bus_mutex);
    }
}

static bool slot_range(uint32_t slot_size, uint32_t offset, uint32_t length)
{
    return offset <= slot_size && length <= slot_size - offset;
}

static void command_defaults(QSPI_CommandTypeDef *command)
{
    command->InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command->AddressSize = QSPI_ADDRESS_24_BITS;
    command->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command->DummyCycles = 0U;
    command->DdrMode = QSPI_DDR_MODE_DISABLE;
    command->DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command->SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
}

static bool read_jedec(uint32_t *jedec)
{
    QSPI_CommandTypeDef command = {0};
    uint8_t bytes[3];

    command_defaults(&command);
    command.Instruction = 0x9FU;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = sizeof(bytes);
    if (HAL_QSPI_Command(&ota_qspi, &command, OTA_QSPI_TIMEOUT_MS) != HAL_OK
        || HAL_QSPI_Receive(&ota_qspi, bytes, OTA_QSPI_TIMEOUT_MS) != HAL_OK)
    {
        return false;
    }
    *jedec = ((uint32_t)bytes[0] << 16U) | ((uint32_t)bytes[1] << 8U) | bytes[2];
    return true;
}

static bool wait_ready(void)
{
    QSPI_CommandTypeDef command = {0};
    QSPI_AutoPollingTypeDef poll = {0};

    command_defaults(&command);
    command.Instruction = 0x05U;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = 1U;
    poll.Match = 0U;
    poll.Mask = 1U;
    poll.MatchMode = QSPI_MATCH_MODE_AND;
    poll.StatusBytesSize = 1U;
    poll.Interval = 0x10U;
    poll.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;
    return HAL_QSPI_AutoPolling(&ota_qspi, &command, &poll, OTA_QSPI_TIMEOUT_MS) == HAL_OK;
}

static bool write_enable(void)
{
    QSPI_CommandTypeDef command = {0};

    command_defaults(&command);
    command.Instruction = 0x06U;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.DataMode = QSPI_DATA_NONE;
    return HAL_QSPI_Command(&ota_qspi, &command, OTA_QSPI_TIMEOUT_MS) == HAL_OK;
}

static bool mutation_allowed(void)
{
    uint32_t jedec;

    return ota_qspi_ready && read_jedec(&jedec) && jedec == OTA_QSPI_JEDEC;
}

bool ota_qspi_candidate_init(void)
{
    RCC_PeriphCLKInitTypeDef clock = {0};
    uint32_t jedec;

    if (!ota_qspi_bus_lock())
    {
        return false;
    }
    if (ota_qspi_ready)
    {
        ota_qspi_bus_unlock();
        return true;
    }
    clock.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
    clock.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
    {
        ota_qspi_bus_unlock();
        return false;
    }
    ota_qspi.Instance = QUADSPI;
    ota_qspi.Init.ClockPrescaler = (TRANSPORT_HCLK_HZ / UINT32_C(40000000)) - 1U;
    ota_qspi.Init.FifoThreshold = 1U;
    ota_qspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    ota_qspi.Init.FlashSize = 22U;
    ota_qspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
    ota_qspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
    ota_qspi.Init.FlashID = QSPI_FLASH_ID_1;
    ota_qspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
    if (HAL_QSPI_Init(&ota_qspi) != HAL_OK || !read_jedec(&jedec) || jedec != OTA_QSPI_JEDEC)
    {
        ota_qspi_bus_unlock();
        return false;
    }
    ota_qspi_ready = true;
    ota_qspi_bus_unlock();
    return true;
}

static int ota_qspi_slot_read(uint32_t slot_offset, uint32_t slot_size,
                              uint32_t offset, uint8_t *data, uint32_t length)
{
    QSPI_CommandTypeDef command = {0};

    if (!ota_qspi_ready || data == NULL || !slot_range(slot_size, offset, length))
    {
        return -1;
    }
    command_defaults(&command);
    command.Instruction = 0x03U;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.Address = slot_offset + offset;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = length;
    return HAL_QSPI_Command(&ota_qspi, &command, OTA_QSPI_TIMEOUT_MS) == HAL_OK
           && HAL_QSPI_Receive(&ota_qspi, data, OTA_QSPI_TIMEOUT_MS) == HAL_OK ? 0 : -1;
}

static int ota_qspi_slot_erase(uint32_t slot_offset, uint32_t slot_size,
                               uint32_t offset, uint32_t length)
{
    uint32_t address;

    if (!slot_range(slot_size, offset, length)
        || offset % TRANSPORT_OTA_QSPI_ERASE_BLOCK_SIZE_BYTES != 0U
        || length % TRANSPORT_OTA_QSPI_ERASE_BLOCK_SIZE_BYTES != 0U || !mutation_allowed())
    {
        return -1;
    }
    for (address = offset; address < offset + length; address += TRANSPORT_OTA_QSPI_ERASE_BLOCK_SIZE_BYTES)
    {
        QSPI_CommandTypeDef command = {0};

        if (!write_enable())
        {
            return -1;
        }
        command_defaults(&command);
        command.Instruction = 0x20U;
        command.AddressMode = QSPI_ADDRESS_1_LINE;
        command.Address = slot_offset + address;
        command.DataMode = QSPI_DATA_NONE;
        if (HAL_QSPI_Command(&ota_qspi, &command, OTA_QSPI_TIMEOUT_MS) != HAL_OK || !wait_ready())
        {
            return -1;
        }
    }
    return 0;
}

static int ota_qspi_slot_write(uint32_t slot_offset, uint32_t slot_size,
                               uint32_t offset, const uint8_t *data, uint32_t length)
{
    if (data == NULL || !slot_range(slot_size, offset, length) || !mutation_allowed())
    {
        return -1;
    }
    while (length != 0U)
    {
        QSPI_CommandTypeDef command = {0};
        const uint32_t page_remaining = OTA_QSPI_PAGE_BYTES - (offset % OTA_QSPI_PAGE_BYTES);
        const uint32_t chunk = length < page_remaining ? length : page_remaining;

        if (!write_enable())
        {
            return -1;
        }
        command_defaults(&command);
        command.Instruction = 0x02U;
        command.AddressMode = QSPI_ADDRESS_1_LINE;
        command.Address = slot_offset + offset;
        command.DataMode = QSPI_DATA_1_LINE;
        command.NbData = chunk;
        if (HAL_QSPI_Command(&ota_qspi, &command, OTA_QSPI_TIMEOUT_MS) != HAL_OK
            || HAL_QSPI_Transmit(&ota_qspi, (uint8_t *)data, OTA_QSPI_TIMEOUT_MS) != HAL_OK
            || !wait_ready())
        {
            return -1;
        }
        offset += chunk;
        data += chunk;
        length -= chunk;
    }
    return 0;
}

int ota_qspi_candidate_read(void *context, uint32_t offset, uint8_t *data, uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_read(TRANSPORT_OTA_QSPI_CANDIDATE_OFFSET,
                                TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_candidate_erase(void *context, uint32_t offset, uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_erase(TRANSPORT_OTA_QSPI_CANDIDATE_OFFSET,
                                 TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                 offset, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_candidate_write(void *context, uint32_t offset, const uint8_t *data,
                             uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_write(TRANSPORT_OTA_QSPI_CANDIDATE_OFFSET,
                                 TRANSPORT_OTA_QSPI_CANDIDATE_SIZE_BYTES,
                                 offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_recovery_read(void *context, uint32_t offset, uint8_t *data, uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_read(TRANSPORT_OTA_QSPI_RECOVERY_OFFSET,
                                TRANSPORT_OTA_QSPI_RECOVERY_SIZE_BYTES,
                                offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_recovery_erase(void *context, uint32_t offset, uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_erase(TRANSPORT_OTA_QSPI_RECOVERY_OFFSET,
                                 TRANSPORT_OTA_QSPI_RECOVERY_SIZE_BYTES,
                                 offset, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_recovery_write(void *context, uint32_t offset, const uint8_t *data,
                            uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_write(TRANSPORT_OTA_QSPI_RECOVERY_OFFSET,
                                 TRANSPORT_OTA_QSPI_RECOVERY_SIZE_BYTES,
                                 offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_ai_result_read(void *context, uint32_t offset, uint8_t *data,
                            uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_read(TRANSPORT_OTA_QSPI_AI_RESULT_OFFSET,
                                TRANSPORT_OTA_QSPI_AI_RESULT_SIZE_BYTES,
                                offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_ai_result_erase(void *context, uint32_t offset, uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_erase(TRANSPORT_OTA_QSPI_AI_RESULT_OFFSET,
                                 TRANSPORT_OTA_QSPI_AI_RESULT_SIZE_BYTES,
                                 offset, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_ai_result_write(void *context, uint32_t offset,
                             const uint8_t *data, uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_write(TRANSPORT_OTA_QSPI_AI_RESULT_OFFSET,
                                 TRANSPORT_OTA_QSPI_AI_RESULT_SIZE_BYTES,
                                 offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_model_a_read(void *context, uint32_t offset, uint8_t *data,
                          uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_read(TRANSPORT_OTA_QSPI_MODEL_A_OFFSET,
                                TRANSPORT_OTA_QSPI_MODEL_SLOT_CAPACITY_BYTES,
                                offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_model_a_erase(void *context, uint32_t offset, uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_erase(TRANSPORT_OTA_QSPI_MODEL_A_OFFSET,
                                 TRANSPORT_OTA_QSPI_MODEL_SLOT_CAPACITY_BYTES,
                                 offset, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_model_a_write(void *context, uint32_t offset, const uint8_t *data,
                           uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_write(TRANSPORT_OTA_QSPI_MODEL_A_OFFSET,
                                 TRANSPORT_OTA_QSPI_MODEL_SLOT_CAPACITY_BYTES,
                                 offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_model_b_read(void *context, uint32_t offset, uint8_t *data,
                          uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_read(TRANSPORT_OTA_QSPI_MODEL_B_OFFSET,
                                TRANSPORT_OTA_QSPI_MODEL_SLOT_CAPACITY_BYTES,
                                offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_model_b_erase(void *context, uint32_t offset, uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_erase(TRANSPORT_OTA_QSPI_MODEL_B_OFFSET,
                                 TRANSPORT_OTA_QSPI_MODEL_SLOT_CAPACITY_BYTES,
                                 offset, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_model_b_write(void *context, uint32_t offset, const uint8_t *data,
                           uint32_t length)
{
    int result;

    (void)context;
    if (!ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_write(TRANSPORT_OTA_QSPI_MODEL_B_OFFSET,
                                 TRANSPORT_OTA_QSPI_MODEL_SLOT_CAPACITY_BYTES,
                                 offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

static uint32_t ota_qspi_model_state_bank_offset(uint8_t bank)
{
    return bank == 0U ? TRANSPORT_OTA_QSPI_MODEL_STATE_OFFSET
                      : bank == 1U
                            ? TRANSPORT_OTA_QSPI_MODEL_STATE_SECONDARY_OFFSET
                            : UINT32_MAX;
}

int ota_qspi_model_state_bank_read(void *context, uint8_t bank,
                                   uint32_t offset, uint8_t *data,
                                   uint32_t length)
{
    const uint32_t state_offset = ota_qspi_model_state_bank_offset(bank);
    int result;

    (void)context;
    if (state_offset == UINT32_MAX || !ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_read(state_offset,
                                TRANSPORT_OTA_QSPI_MODEL_STATE_SIZE_BYTES,
                                offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_model_state_bank_erase(void *context, uint8_t bank,
                                    uint32_t offset, uint32_t length)
{
    const uint32_t state_offset = ota_qspi_model_state_bank_offset(bank);
    int result;

    (void)context;
    if (state_offset == UINT32_MAX || !ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_erase(state_offset,
                                 TRANSPORT_OTA_QSPI_MODEL_STATE_SIZE_BYTES,
                                 offset, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_model_state_bank_write(void *context, uint8_t bank,
                                    uint32_t offset, const uint8_t *data,
                                    uint32_t length)
{
    const uint32_t state_offset = ota_qspi_model_state_bank_offset(bank);
    int result;

    (void)context;
    if (state_offset == UINT32_MAX || !ota_qspi_bus_lock())
    {
        return -1;
    }
    result = ota_qspi_slot_write(state_offset,
                                 TRANSPORT_OTA_QSPI_MODEL_STATE_SIZE_BYTES,
                                 offset, data, length);
    ota_qspi_bus_unlock();
    return result;
}

int ota_qspi_model_state_read(void *context, uint32_t offset, uint8_t *data,
                              uint32_t length)
{
    return ota_qspi_model_state_bank_read(context, 0U, offset, data, length);
}

int ota_qspi_model_state_erase(void *context, uint32_t offset, uint32_t length)
{
    return ota_qspi_model_state_bank_erase(context, 0U, offset, length);
}

int ota_qspi_model_state_write(void *context, uint32_t offset,
                               const uint8_t *data, uint32_t length)
{
    return ota_qspi_model_state_bank_write(context, 0U, offset, data, length);
}

void HAL_QSPI_MspInit(QSPI_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpio = {0};

    if (handle == NULL || handle->Instance != QUADSPI)
    {
        return;
    }
    __HAL_RCC_QSPI_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF9_QUADSPI;
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
    HAL_GPIO_Init(GPIOD, &gpio);
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOE, &gpio);
}
