# Phase 05 pretrigger memory budget

## Adopted boundary

The fixed `sample_block_pool` contains 192 parsed-sample blocks. Eight blocks
remain reserved for acquisition handoff; event retention must stop before the
free count falls below that reserve. The additional 64-block export headroom
keeps acquisition supplied while the synchronous event exporter writes a
maximum event to external storage.

One `sample_block_t` is 1,184 bytes on the current target layout:

- 64 parsed ICM45686 samples × 18 B = 1,152 B;
- block metadata and alignment = 32 B;
- 192 blocks × 1,184 B plus pool metadata = 227,336 B (about 222.0 KiB).

`sample_block_t` reserves capacity for 64 samples, but the V1 publisher fixes
each event block at `IMU_SAMPLE_BATCH_SIZE=32`. At 1.6 kHz, one published block
therefore spans 20 ms. V1 exports exactly 25 pretrigger blocks (0.5 s) and 50
post-trigger blocks (1.0 s): 75 blocks, 2400 samples, 1.5 seconds. The assembler
retains a legacy 100-block internal ceiling, but V1 natural admission is
one-shot and the service rejects any event that is not the fixed 25+50 shape;
the old 4-second extension is not a V1 record contract.

## RAM placement and build evidence

Event blocks live in AXI RAM. D2 SRAM1 remains for DMA/non-cacheable buffers;
it is not used as a long-lived event cache. The historical Phase 05 H743 build
reported:

- RAM: 162,576 B / 512 KiB (31.01%);
- D2 SRAM1: 2,144 B / 128 KiB (1.64%).

The current Release build with the export-headroom fix reports:

- RAM: 269,452 B / 512 KiB (51.39%);
- D2 SRAM1: 2,144 B / 128 KiB (1.64%).

This leaves headroom for later storage, thread-stack and model work, but it is
not a final AI-memory allocation.

## Overflow policy

- Reference ownership prevents a pretrigger overwrite from freeing a block
  still held by an event.
- With fewer than eight free blocks, new event retention stops and the active
  event is released with an explicit resource-reject counter; no protected
  data is silently reused.
- A longer physical episode is represented by the first fixed 1.5-second
  window. Natural admission is latched for that event and a 30-second cooldown
  follows; segmented long-event storage is outside V1.

## V1 verification boundary

The V1 scoped hardware regression observed `pool_backpressure=0` and
`pool_min_free=49` while acquiring, exporting and reading back controlled
events; the fixed event shape also passed the release software gate. This
closes the V1 pool-headroom defect, but does not claim the deferred 72-hour,
100-cycle power-cut or complete physical-disconnect reliability matrices.
