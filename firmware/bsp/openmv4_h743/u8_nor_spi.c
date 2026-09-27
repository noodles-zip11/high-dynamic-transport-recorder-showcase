#include "u8_nor_spi.h"

#include "board.h"

#define BOARD_U8_NOR_SPI_TIMEOUT_MS 100U

static SPI_HandleTypeDef u8_nor_spi_handle;
static rt_bool_t u8_nor_spi_initialized;

rt_err_t board_u8_nor_spi_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};

    if (u8_nor_spi_initialized)
    {
        return RT_EOK;
    }

    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_SPI2;
    peripheral_clock.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
    {
        return -RT_ERROR;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();
    HAL_GPIO_WritePin(BOARD_U8_NOR_CS_GPIO_PORT,
                      BOARD_U8_NOR_CS_GPIO_PIN, GPIO_PIN_SET);

    gpio.Pin = BOARD_U8_NOR_CS_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_U8_NOR_CS_GPIO_PORT, &gpio);

    gpio.Pin = BOARD_U8_NOR_SCK_GPIO_PIN | BOARD_U8_NOR_MISO_GPIO_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = BOARD_U8_NOR_GPIO_AF;
    HAL_GPIO_Init(BOARD_U8_NOR_SCK_GPIO_PORT, &gpio);

    gpio.Pin = BOARD_U8_NOR_MOSI_GPIO_PIN;
    HAL_GPIO_Init(BOARD_U8_NOR_MOSI_GPIO_PORT, &gpio);

    u8_nor_spi_handle.Instance = BOARD_U8_NOR_SPI_INSTANCE;
    u8_nor_spi_handle.Init.Mode = SPI_MODE_MASTER;
    u8_nor_spi_handle.Init.Direction = SPI_DIRECTION_2LINES;
    u8_nor_spi_handle.Init.DataSize = SPI_DATASIZE_8BIT;
    u8_nor_spi_handle.Init.CLKPolarity = SPI_POLARITY_LOW;
    u8_nor_spi_handle.Init.CLKPhase = SPI_PHASE_1EDGE;
    u8_nor_spi_handle.Init.NSS = SPI_NSS_SOFT;
    u8_nor_spi_handle.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    u8_nor_spi_handle.Init.FirstBit = SPI_FIRSTBIT_MSB;
    u8_nor_spi_handle.Init.TIMode = SPI_TIMODE_DISABLE;
    u8_nor_spi_handle.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    u8_nor_spi_handle.Init.CRCPolynomial = 0U;
    u8_nor_spi_handle.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    u8_nor_spi_handle.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    u8_nor_spi_handle.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    u8_nor_spi_handle.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    u8_nor_spi_handle.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    u8_nor_spi_handle.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    u8_nor_spi_handle.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    u8_nor_spi_handle.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    u8_nor_spi_handle.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
    u8_nor_spi_handle.Init.IOSwap = SPI_IO_SWAP_DISABLE;
    if (HAL_SPI_Init(&u8_nor_spi_handle) != HAL_OK)
    {
        return -RT_ERROR;
    }

    u8_nor_spi_initialized = RT_TRUE;
    return RT_EOK;
}

rt_err_t board_u8_nor_spi_transfer(const uint8_t *tx,
                                   uint8_t *rx,
                                   rt_size_t length)
{
    HAL_StatusTypeDef status;

    if (!u8_nor_spi_initialized || tx == RT_NULL || rx == RT_NULL
        || length == 0U || length > UINT16_MAX)
    {
        return -RT_ERROR;
    }

    HAL_GPIO_WritePin(BOARD_U8_NOR_CS_GPIO_PORT,
                      BOARD_U8_NOR_CS_GPIO_PIN, GPIO_PIN_RESET);
    status = HAL_SPI_TransmitReceive(&u8_nor_spi_handle, (uint8_t *)tx, rx,
                                     (uint16_t)length, BOARD_U8_NOR_SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(BOARD_U8_NOR_CS_GPIO_PORT,
                      BOARD_U8_NOR_CS_GPIO_PIN, GPIO_PIN_SET);
    return status == HAL_OK ? RT_EOK : -RT_ERROR;
}
