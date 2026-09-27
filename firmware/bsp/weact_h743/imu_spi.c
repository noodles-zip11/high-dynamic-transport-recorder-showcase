#include "imu_spi.h"
#include "board.h"


static SPI_HandleTypeDef imu_spi_handle;
static rt_bool_t imu_spi_initialized;


static DMA_HandleTypeDef imu_spi_rx_dma_handle;
static DMA_HandleTypeDef imu_spi_tx_dma_handle;

static board_imu_dma_handler_t imu_dma_handler;
static void *imu_dma_context;
static volatile rt_bool_t imu_spi_dma_active;
static volatile rt_bool_t imu_spi_polling_active;

static board_imu_isr_handler_t imu_int1_handler;
static void *imu_int1_context;

static rt_bool_t imu_spi_dma_initialized;
static rt_bool_t imu_int1_initialized;



#define BOARD_IMU_SPI_TIMEOUT_MS 100U

rt_err_t board_imu_spi_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};

    if (imu_spi_initialized)
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

    HAL_GPIO_WritePin(BOARD_IMU_CS_GPIO_PORT,
                      BOARD_IMU_CS_GPIO_PIN,
                      GPIO_PIN_SET);

    gpio.Pin = BOARD_IMU_CS_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_IMU_CS_GPIO_PORT, &gpio);

    gpio.Pin = BOARD_IMU_SCK_GPIO_PIN | BOARD_IMU_MISO_GPIO_PIN
               | BOARD_IMU_MOSI_GPIO_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = BOARD_IMU_SCK_GPIO_AF;
    HAL_GPIO_Init(BOARD_IMU_SCK_GPIO_PORT, &gpio);

    imu_spi_handle.Instance = BOARD_IMU_SPI_INSTANCE;
    imu_spi_handle.Init.Mode = SPI_MODE_MASTER;
    imu_spi_handle.Init.Direction = SPI_DIRECTION_2LINES;
    imu_spi_handle.Init.DataSize = SPI_DATASIZE_8BIT;
    imu_spi_handle.Init.CLKPolarity = SPI_POLARITY_LOW;
    imu_spi_handle.Init.CLKPhase = SPI_PHASE_1EDGE;
    imu_spi_handle.Init.NSS = SPI_NSS_SOFT;
    imu_spi_handle.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    imu_spi_handle.Init.FirstBit = SPI_FIRSTBIT_MSB;
    imu_spi_handle.Init.TIMode = SPI_TIMODE_DISABLE;
    imu_spi_handle.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    imu_spi_handle.Init.CRCPolynomial = 0U;
    imu_spi_handle.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    imu_spi_handle.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    imu_spi_handle.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    imu_spi_handle.Init.TxCRCInitializationPattern =
        SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    imu_spi_handle.Init.RxCRCInitializationPattern =
        SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    imu_spi_handle.Init.MasterSSIdleness =
        SPI_MASTER_SS_IDLENESS_00CYCLE;
    imu_spi_handle.Init.MasterInterDataIdleness =
        SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    imu_spi_handle.Init.MasterReceiverAutoSusp =
        SPI_MASTER_RX_AUTOSUSP_DISABLE;
    imu_spi_handle.Init.MasterKeepIOState =
        SPI_MASTER_KEEP_IO_STATE_DISABLE;
    imu_spi_handle.Init.IOSwap = SPI_IO_SWAP_DISABLE;

    if (HAL_SPI_Init(&imu_spi_handle) != HAL_OK)
    {
        return -RT_ERROR;
    }

    imu_spi_initialized = RT_TRUE;
    return RT_EOK;
}

static rt_err_t board_imu_spi_rx_dma_init(void)
{
    imu_spi_rx_dma_handle.Instance = BOARD_IMU_SPI_RX_DMA_INSTANCE;
    imu_spi_rx_dma_handle.Init.Request = BOARD_IMU_SPI_RX_DMA_REQUEST;
    imu_spi_rx_dma_handle.Init.Direction = DMA_PERIPH_TO_MEMORY;
    imu_spi_rx_dma_handle.Init.PeriphInc = DMA_PINC_DISABLE;
    imu_spi_rx_dma_handle.Init.MemInc = DMA_MINC_ENABLE;
    imu_spi_rx_dma_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    imu_spi_rx_dma_handle.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    imu_spi_rx_dma_handle.Init.Mode = DMA_NORMAL;
    imu_spi_rx_dma_handle.Init.Priority = DMA_PRIORITY_HIGH;
    imu_spi_rx_dma_handle.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&imu_spi_rx_dma_handle) != HAL_OK)
    {
        return -RT_ERROR;
    }

    __HAL_LINKDMA(&imu_spi_handle, hdmarx, imu_spi_rx_dma_handle);
    return RT_EOK;
}

static rt_err_t board_imu_spi_tx_dma_init(void)
{
    imu_spi_tx_dma_handle.Instance = BOARD_IMU_SPI_TX_DMA_INSTANCE;
    imu_spi_tx_dma_handle.Init.Request = BOARD_IMU_SPI_TX_DMA_REQUEST;
    imu_spi_tx_dma_handle.Init.Direction = DMA_MEMORY_TO_PERIPH;
    imu_spi_tx_dma_handle.Init.PeriphInc = DMA_PINC_DISABLE;
    imu_spi_tx_dma_handle.Init.MemInc = DMA_MINC_ENABLE;
    imu_spi_tx_dma_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    imu_spi_tx_dma_handle.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    imu_spi_tx_dma_handle.Init.Mode = DMA_NORMAL;
    imu_spi_tx_dma_handle.Init.Priority = DMA_PRIORITY_HIGH;
    imu_spi_tx_dma_handle.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&imu_spi_tx_dma_handle) != HAL_OK)
    {
        return -RT_ERROR;
    }

    __HAL_LINKDMA(&imu_spi_handle, hdmatx, imu_spi_tx_dma_handle);
    return RT_EOK;
}


rt_err_t board_imu_spi_dma_init(void)
{
    if (!imu_spi_initialized)
    {
        return -RT_ERROR;
    }

    if (imu_spi_dma_initialized)
    {
        return RT_EOK;
    }

    __HAL_RCC_DMA1_CLK_ENABLE();

    if (board_imu_spi_rx_dma_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (board_imu_spi_tx_dma_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    HAL_NVIC_SetPriority(BOARD_IMU_SPI_RX_DMA_IRQn,
                         BOARD_IMU_SPI_DMA_IRQ_PRIORITY,
                         BOARD_IMU_SPI_DMA_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(BOARD_IMU_SPI_RX_DMA_IRQn);

    HAL_NVIC_SetPriority(BOARD_IMU_SPI_TX_DMA_IRQn,
                         BOARD_IMU_SPI_DMA_IRQ_PRIORITY,
                         BOARD_IMU_SPI_DMA_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(BOARD_IMU_SPI_TX_DMA_IRQn);

    imu_spi_dma_initialized = RT_TRUE;
    return RT_EOK;
}

//设置中断handler，供回调
rt_err_t board_imu_spi_set_dma_handler(board_imu_dma_handler_t handler,
                                       void *context)
{
    if (handler == RT_NULL)
    {
        return -RT_ERROR;
    }

    imu_dma_handler = handler;
    imu_dma_context = context;
    return RT_EOK;
}

rt_err_t board_imu_spi_transfer_dma(const uint8_t *tx,
                                    uint8_t *rx,
                                    rt_size_t length)
{
    HAL_StatusTypeDef status;

    if (!imu_spi_initialized || !imu_spi_dma_initialized
    || imu_spi_dma_active || imu_spi_polling_active
    || tx == RT_NULL || rx == RT_NULL
    || length == 0U || length > UINT16_MAX)
    {
        return -RT_ERROR;
    }

    HAL_GPIO_WritePin(BOARD_IMU_CS_GPIO_PORT,
                      BOARD_IMU_CS_GPIO_PIN,
                      GPIO_PIN_RESET);
    imu_spi_dma_active = RT_TRUE;

    status = HAL_SPI_TransmitReceive_DMA(&imu_spi_handle,
                                         (uint8_t *)tx,
                                         rx,
                                         (uint16_t)length);
    if (status != HAL_OK)
    {
        imu_spi_dma_active = RT_FALSE;
        HAL_GPIO_WritePin(BOARD_IMU_CS_GPIO_PORT,
                          BOARD_IMU_CS_GPIO_PIN,
                          GPIO_PIN_SET);
        return -RT_ERROR;
    }

    return RT_EOK;
}

rt_err_t board_imu_spi_abort_dma(void)
{
    if (!imu_spi_initialized || !imu_spi_dma_initialized)
    {
        return -RT_ERROR;
    }

    if (!imu_spi_dma_active)
    {
        return RT_EOK;
    }

    if (HAL_SPI_Abort(&imu_spi_handle) != HAL_OK)
    {
        return -RT_ERROR;
    }

    imu_spi_dma_active = RT_FALSE;
    HAL_GPIO_WritePin(BOARD_IMU_CS_GPIO_PORT,
                      BOARD_IMU_CS_GPIO_PIN,
                      GPIO_PIN_SET);
    return RT_EOK;
}

//INT1，设置回调函数的handler
rt_err_t board_imu_int1_set_handler(board_imu_isr_handler_t handler,
                                    void *context)
{
    if (handler == RT_NULL)
    {
        return -RT_ERROR;
    }

    imu_int1_handler = handler;
    imu_int1_context = context;
    return RT_EOK;
}

static void board_imu_int1_irq_handler(void *context)
{
    (void)context;

    if (imu_int1_handler != RT_NULL)
    {
        imu_int1_handler(imu_int1_context);
    }
}


rt_err_t board_imu_int1_init(void)
{
    rt_err_t result;

    if (imu_int1_initialized)
    {
        return RT_EOK;
    }

    result = rt_pin_attach_irq(GET_PIN(B, 5),
                               PIN_IRQ_MODE_RISING,
                               board_imu_int1_irq_handler,
                               RT_NULL);
    if (result != RT_EOK)
    {
        return result;
    }

    result = rt_pin_irq_enable(GET_PIN(B, 5), PIN_IRQ_ENABLE);
    if (result != RT_EOK)
    {
        return result;
    }

    HAL_NVIC_SetPriority(BOARD_IMU_INT1_EXTI_IRQn,
                         BOARD_IMU_INT1_IRQ_PRIORITY,
                         BOARD_IMU_INT1_IRQ_SUBPRIORITY);

    imu_int1_initialized = RT_TRUE;
    return RT_EOK;
}

void DMA1_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&imu_spi_rx_dma_handle);
}

void DMA1_Stream1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&imu_spi_tx_dma_handle);
}



void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *spi)
{
    if (spi != &imu_spi_handle)
    {
        return;
    }

    imu_spi_dma_active = RT_FALSE;

    HAL_GPIO_WritePin(BOARD_IMU_CS_GPIO_PORT,
                      BOARD_IMU_CS_GPIO_PIN,
                      GPIO_PIN_SET);

    if (imu_dma_handler != RT_NULL)
    {
        imu_dma_handler(imu_dma_context, RT_EOK);
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *spi)
{
    if (spi != &imu_spi_handle)
    {
        return;
    }

    imu_spi_dma_active = RT_FALSE;

    HAL_GPIO_WritePin(BOARD_IMU_CS_GPIO_PORT,
                      BOARD_IMU_CS_GPIO_PIN,
                      GPIO_PIN_SET);

    if (imu_dma_handler != RT_NULL)
    {
        imu_dma_handler(imu_dma_context, -RT_ERROR);
    }
}






































rt_err_t board_imu_spi_transfer(const uint8_t *tx,
                                uint8_t *rx,
                                rt_size_t length)
{
    HAL_StatusTypeDef status;

    if (!imu_spi_initialized || imu_spi_dma_active
    || imu_spi_polling_active || tx == RT_NULL || rx == RT_NULL
    || length == 0U || length > UINT16_MAX)
    {
        return -RT_ERROR;
    }

    imu_spi_polling_active = RT_TRUE;

    HAL_GPIO_WritePin(BOARD_IMU_CS_GPIO_PORT,
                      BOARD_IMU_CS_GPIO_PIN,
                      GPIO_PIN_RESET);

    status = HAL_SPI_TransmitReceive(&imu_spi_handle,
                                     (uint8_t *)tx,
                                     rx,
                                     (uint16_t)length,
                                     BOARD_IMU_SPI_TIMEOUT_MS);

    imu_spi_polling_active = RT_FALSE;

    HAL_GPIO_WritePin(BOARD_IMU_CS_GPIO_PORT,
                      BOARD_IMU_CS_GPIO_PIN,
                      GPIO_PIN_SET);

    if (status != HAL_OK)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}
