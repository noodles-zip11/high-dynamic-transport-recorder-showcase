#include <stdbool.h>

#include "bootloader_hal.h"
#include "stm32h7xx_hal.h"

bool bootloader_platform_hal_init(void)
{
    if (HAL_Init() != HAL_OK)
    {
        return false;
    }
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }
    return true;
}

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
}

void SysTick_Handler(void)
{
    HAL_IncTick();
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

void HAL_QSPI_MspDeInit(QSPI_HandleTypeDef *handle)
{
    if (handle == NULL || handle->Instance != QUADSPI)
    {
        return;
    }
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_2 | GPIO_PIN_10);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13);
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_2);
    __HAL_RCC_QSPI_CLK_DISABLE();
}

void bootloader_recovery_wait(void)
{
    __disable_irq();
    for (;;)
    {
        __WFI();
    }
}
