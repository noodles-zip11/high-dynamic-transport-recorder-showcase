#include "board.h"


void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_D2SRAM1_CLK_ENABLE();

    HAL_MPU_Disable();

    MPU_Region_InitTypeDef region = {0};
    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER0;
    region.BaseAddress = TRANSPORT_D2_SRAM1_BASE;
    region.Size = MPU_REGION_SIZE_128KB;
    region.SubRegionDisable = 0x00U;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void HAL_UART_MspInit(UART_HandleTypeDef *uart)
{
    GPIO_InitTypeDef gpio = {0};

    if (uart->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        gpio.Pin = BOARD_USART1_TX_GPIO_PIN | BOARD_USART1_RX_GPIO_PIN;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        gpio.Alternate = BOARD_USART1_GPIO_AF;
        HAL_GPIO_Init(BOARD_USART1_TX_GPIO_PORT, &gpio);
        return;
    }
    if (uart->Instance == USART3)
    {
        __HAL_RCC_USART3_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        gpio.Pin = BOARD_USART3_TX_GPIO_PIN | BOARD_USART3_RX_GPIO_PIN;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        gpio.Alternate = BOARD_USART3_GPIO_AF;
        HAL_GPIO_Init(BOARD_USART3_TX_GPIO_PORT, &gpio);
    }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *uart)
{
    if (uart->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_DISABLE();
        HAL_GPIO_DeInit(BOARD_USART1_TX_GPIO_PORT,
                        BOARD_USART1_TX_GPIO_PIN | BOARD_USART1_RX_GPIO_PIN);
        return;
    }
    if (uart->Instance == USART3)
    {
        __HAL_RCC_USART3_CLK_DISABLE();
        HAL_GPIO_DeInit(BOARD_USART3_TX_GPIO_PORT,
                        BOARD_USART3_TX_GPIO_PIN | BOARD_USART3_RX_GPIO_PIN);
    }
}
