#include "sht4x_i2c.h"

#include "board.h"

#define BOARD_SHT4X_I2C_TIMEOUT_MS 100U

static I2C_HandleTypeDef sht4x_i2c_handle;
static rt_bool_t sht4x_i2c_initialized;

rt_err_t board_sht4x_i2c_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};

    if (sht4x_i2c_initialized)
    {
        return RT_EOK;
    }

    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    peripheral_clock.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
    {
        return -RT_ERROR;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio.Pin = BOARD_SHT4X_SCL_GPIO_PIN | BOARD_SHT4X_SDA_GPIO_PIN;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = BOARD_SHT4X_I2C_GPIO_AF;
    HAL_GPIO_Init(BOARD_SHT4X_SCL_GPIO_PORT, &gpio);

    sht4x_i2c_handle.Instance = BOARD_SHT4X_I2C_INSTANCE;
    sht4x_i2c_handle.Init.Timing = BOARD_SHT4X_I2C_TIMING;
    sht4x_i2c_handle.Init.OwnAddress1 = 0U;
    sht4x_i2c_handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    sht4x_i2c_handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    sht4x_i2c_handle.Init.OwnAddress2 = 0U;
    sht4x_i2c_handle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    sht4x_i2c_handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    sht4x_i2c_handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&sht4x_i2c_handle) != HAL_OK)
    {
        return -RT_ERROR;
    }

    sht4x_i2c_initialized = RT_TRUE;
    return RT_EOK;
}

rt_err_t board_sht4x_i2c_is_ready(uint8_t address)
{
    if (!sht4x_i2c_initialized
        || HAL_I2C_IsDeviceReady(&sht4x_i2c_handle,
                                 (uint16_t)address << 1U, 1U,
                                 BOARD_SHT4X_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

rt_err_t board_sht4x_i2c_write(uint8_t address, const uint8_t *data,
                               rt_size_t length)
{
    if (!sht4x_i2c_initialized || data == RT_NULL || length == 0U
        || length > UINT16_MAX
        || HAL_I2C_Master_Transmit(&sht4x_i2c_handle,
                                   (uint16_t)address << 1U,
                                   (uint8_t *)data, (uint16_t)length,
                                   BOARD_SHT4X_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}

rt_err_t board_sht4x_i2c_read(uint8_t address, uint8_t *data,
                              rt_size_t length)
{
    if (!sht4x_i2c_initialized || data == RT_NULL || length == 0U
        || length > UINT16_MAX
        || HAL_I2C_Master_Receive(&sht4x_i2c_handle,
                                  (uint16_t)address << 1U,
                                  data, (uint16_t)length,
                                  BOARD_SHT4X_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return -RT_ERROR;
    }
    return RT_EOK;
}
