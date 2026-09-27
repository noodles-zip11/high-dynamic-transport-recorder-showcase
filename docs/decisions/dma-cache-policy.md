# Phase 03 DMA and Cache policy

DMA buffers use `TRANSPORT_DMA_BUFFER`, which places them in D2 SRAM1 at `0x30000000` and aligns them to the Cortex-M7 32-byte cache line. DTCM is prohibited for DMA buffers.

Phase 03 maps the whole 128 KiB D2 SRAM1 region as MPU non-cacheable, shareable and execute-never before enabling I/D Cache. This favours correctness while no peripheral DMA is enabled.

If a later phase uses cacheable DMA buffers, CPU-to-DMA transfers must clean the affected range before DMA starts; DMA-to-CPU transfers must invalidate it before CPU reads. Both ranges must be expanded outward to 32-byte boundaries.
