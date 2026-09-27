# Phase 03 clock tree

## Chosen configuration

`HSE = 25 MHz -> PLLM = 5 -> PLL input = 5 MHz -> PLLN = 96 -> VCO = 480 MHz -> PLLP = 2 -> SYSCLK/HCLK = 240 MHz`.

All APB domains use `/2`, so PCLK1/PCLK2/PCLK3/PCLK4 are 120 MHz. With the STM32H7 timer-clock rule under an APB prescaler greater than one, timer clocks are 240 MHz.

The implementation uses LDO supply, voltage scale 0 and Flash latency 4. These settings are conservative for the selected 240 MHz bring-up point.

## Evidence and validation status

The input frequency comes from the user-supplied OpenMV4 H743 schematic, which labels a 25 MHz HSE. It has not been measured on the physical board. Before hardware acceptance, measure MCO or a timer-toggle output with an oscilloscope or logic analyser and record the observed frequency and error here.
