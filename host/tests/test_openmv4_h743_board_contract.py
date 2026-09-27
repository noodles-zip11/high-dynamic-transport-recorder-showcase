from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
PINMAP_PATH = PROJECT_ROOT / "firmware" / "bsp" / "openmv4_h743" / "board_pinmap.h"
IMU_SPI_PATH = PROJECT_ROOT / "firmware" / "bsp" / "openmv4_h743" / "imu_spi.c"
SHT4X_I2C_PATH = PROJECT_ROOT / "firmware" / "bsp" / "openmv4_h743" / "sht4x_i2c.c"
BOARD_SCONSCRIPT_PATH = (
    PROJECT_ROOT / "firmware" / "bsp" / "openmv4_h743" / "SConscript"
)
HAL_CONFIG_PATH = (
    PROJECT_ROOT / "firmware" / "bsp" / "openmv4_h743" / "stm32h7xx_hal_conf.h"
)
APP_RUNTIME_PATH = PROJECT_ROOT / "firmware" / "app" / "runtime" / "app_runtime.c"
RTTHREAD_CONFIG_PATH = PROJECT_ROOT / "firmware" / "config" / "rtconfig.h"
SCONSTRUCT_PATH = PROJECT_ROOT / "firmware" / "SConstruct"
ROOT_SCONSCRIPT_PATH = PROJECT_ROOT / "firmware" / "SConscript"
RTCONFIG_PATH = PROJECT_ROOT / "firmware" / "rtconfig.py"


def test_openmv4_h743_pin_contract() -> None:
    pinmap = PINMAP_PATH.read_text(encoding="utf-8")

    expected_definitions = (
        "#define BOARD_LED_GPIO_PORT GPIOC",
        "#define BOARD_LED_GPIO_PIN GPIO_PIN_2",
        "#define BOARD_LED_ACTIVE_HIGH 0",
        "#define BOARD_BUTTON_GPIO_PORT GPIOC",
        "#define BOARD_BUTTON_GPIO_PIN GPIO_PIN_13",
        "#define BOARD_BUTTON_ACTIVE_HIGH 0",
        "#define BOARD_USART3_TX_GPIO_PORT GPIOD",
        "#define BOARD_USART3_TX_GPIO_PIN GPIO_PIN_8",
        "#define BOARD_USART3_RX_GPIO_PORT GPIOD",
        "#define BOARD_USART3_RX_GPIO_PIN GPIO_PIN_9",
        "#define BOARD_IMU_SPI_INSTANCE SPI1",
        "#define BOARD_IMU_SCK_GPIO_PORT GPIOA",
        "#define BOARD_IMU_SCK_GPIO_PIN GPIO_PIN_5",
        "#define BOARD_IMU_MISO_GPIO_PORT GPIOA",
        "#define BOARD_IMU_MISO_GPIO_PIN GPIO_PIN_6",
        "#define BOARD_IMU_MOSI_GPIO_PORT GPIOA",
        "#define BOARD_IMU_MOSI_GPIO_PIN GPIO_PIN_7",
        "#define BOARD_IMU_CS_GPIO_PORT GPIOB",
        "#define BOARD_IMU_CS_GPIO_PIN GPIO_PIN_0",
        "#define BOARD_IMU_INT1_GPIO_PORT GPIOB",
        "#define BOARD_IMU_INT1_GPIO_PIN GPIO_PIN_1",
        "#define BOARD_IMU_SPI_RX_DMA_REQUEST DMA_REQUEST_SPI1_RX",
        "#define BOARD_IMU_SPI_TX_DMA_REQUEST DMA_REQUEST_SPI1_TX",
        "#define BOARD_U8_NOR_SPI_INSTANCE SPI2",
        "#define BOARD_U8_NOR_SCK_GPIO_PIN GPIO_PIN_13",
        "#define BOARD_U8_NOR_MISO_GPIO_PIN GPIO_PIN_14",
        "#define BOARD_U8_NOR_MOSI_GPIO_PIN GPIO_PIN_15",
        "#define BOARD_U8_NOR_CS_GPIO_PIN GPIO_PIN_12",
        "#define BOARD_HSE_FREQUENCY_HZ UINT32_C(25000000)",
        "#define BOARD_LSE_FREQUENCY_HZ UINT32_C(32768)",
    )

    for definition in expected_definitions:
        assert definition in pinmap


def test_firmware_build_selects_openmv4_h743_bsp() -> None:
    sconstruct = SCONSTRUCT_PATH.read_text(encoding="utf-8")
    root_sconscript = ROOT_SCONSCRIPT_PATH.read_text(encoding="utf-8")
    rtconfig = RTCONFIG_PATH.read_text(encoding="utf-8")

    assert 'os.path.join("bsp", "openmv4_h743")' in sconstruct
    assert 'SConscript("bsp/openmv4_h743/SConscript")' in root_sconscript
    assert "-T bsp/openmv4_h743/linker_scripts/link.lds" in rtconfig


def test_openmv4_h743_imu_irq_uses_rtthread_interrupt_context() -> None:
    imu_spi = IMU_SPI_PATH.read_text(encoding="utf-8")

    assert """void DMA1_Stream0_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_DMA_IRQHandler(&imu_spi_rx_dma_handle);
    rt_interrupt_leave();
}""" in imu_spi
    assert """void DMA1_Stream1_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_DMA_IRQHandler(&imu_spi_tx_dma_handle);
    rt_interrupt_leave();
}""" in imu_spi


def test_openmv4_h743_int1_uses_board_pinmap_rtthread_pin() -> None:
    pinmap = PINMAP_PATH.read_text(encoding="utf-8")
    imu_spi = IMU_SPI_PATH.read_text(encoding="utf-8")

    assert "#define BOARD_IMU_INT1_RT_PIN GET_PIN(B, 1)" in pinmap
    assert "rt_pin_attach_irq(BOARD_IMU_INT1_RT_PIN," in imu_spi
    assert "rt_pin_irq_enable(BOARD_IMU_INT1_RT_PIN, PIN_IRQ_ENABLE)" in imu_spi


def test_hardware_bringup_stack_and_console_limits_are_preserved() -> None:
    rtconfig = RTTHREAD_CONFIG_PATH.read_text(encoding="utf-8")
    runtime = APP_RUNTIME_PATH.read_text(encoding="utf-8")

    assert "#define IDLE_THREAD_STACK_SIZE 1024" in rtconfig
    assert "#define RT_CONSOLEBUF_SIZE 384" in rtconfig
    assert 'rt_thread_create("sysreport", system_report_entry, RT_NULL,' in runtime
    assert "2048, 12, 10" in runtime


def test_openmv4_h743_sht4x_i2c_and_ota_qspi_contract() -> None:
    pinmap = PINMAP_PATH.read_text(encoding="utf-8")
    board_sconscript = BOARD_SCONSCRIPT_PATH.read_text(encoding="utf-8")
    hal_config = HAL_CONFIG_PATH.read_text(encoding="utf-8")
    sconstruct = SCONSTRUCT_PATH.read_text(encoding="utf-8")

    assert SHT4X_I2C_PATH.is_file()
    sht4x_i2c = SHT4X_I2C_PATH.read_text(encoding="utf-8")
    assert "#define BOARD_SHT4X_I2C_INSTANCE I2C1" in pinmap
    assert "#define BOARD_SHT4X_SCL_GPIO_PIN GPIO_PIN_8" in pinmap
    assert "#define BOARD_SHT4X_SDA_GPIO_PIN GPIO_PIN_9" in pinmap
    assert "#define BOARD_SHT4X_I2C_TIMING UINT32_C(0x307075B1)" in pinmap
    assert '"sht4x_i2c.c"' in board_sconscript
    assert "#define HAL_I2C_MODULE_ENABLED" in hal_config
    assert '"stm32h7xx_hal_i2c.c"' in sconstruct
    assert "HAL_I2C_Master_Transmit" in sht4x_i2c
    assert "HAL_I2C_Master_Receive" in sht4x_i2c
    # QSPI is reserved for the Phase 11 OTA candidate/recovery slots.  The
    # event log remains on the separate SPI NOR path and is not exercised by
    # the OTA-only image.
    assert "#define HAL_QSPI_MODULE_ENABLED" in hal_config
    # STM32H7's QSPI HAL embeds MDMA handles and therefore requires the MDMA
    # type/module even though this OTA slice uses indirect polling transfers.
    assert "#define HAL_MDMA_MODULE_ENABLED" in hal_config
