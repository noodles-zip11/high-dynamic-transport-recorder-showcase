# Sample-pool export-headroom regression

This session validates the independent sample-pool backpressure defect. The
pre-fix two-hour G3 run was completed and recorded a strict failure; its full
evidence is in [the 2026-08-21 run](../../../2026-08-21/core-acceptance-g3/two-hour-preacceptance-001/summary.md).
This post-fix scoped run is not a replacement for a full post-fix two-hour G3
long-run gate.

## Fix and artifact identity

- Fix commit: `0a9ff12e5c40abdd3ad0506418b022db9914e252`
- Final Release embedded revision: `0a9ff127c1a2a0a2596c1c61d616f06efc47af0f`
  (historical build-input typo; this 40-character string is not a Git object)
- Final BIN: 129,088 bytes, SHA-256 `E7836F4D12B3FC7AAB6A7FE7FECF3262393EDCCC0B8785F8CB536D8A3D071D37`
- Program address: `0x08020000`; ST-LINK verify: PASS
- Hardware: STM32H743 / `recorder-001`; UART1 COM6, UART3 COM7

The first diagnostic attempt in this directory used the old main-tree image
(`4c47fe6`, 128,952 bytes) by mistake and observed the known old behaviour
(`pool_backpressure=13` after event 42). It is not an acceptance result; see
`run-001-old-image-diagnosis.md`. The final run below used the image identity
listed above.

Identity correction recorded 2026-08-25: the original logs and
`artifact-hashes.txt` intentionally retain the revision string emitted by the
board. Git resolves the corresponding short commit `0a9ff12` to
`0a9ff12e5c40abdd3ad0506418b022db9914e252`; artifact size/SHA-256 and ST-LINK
verification remain the authoritative identity of the tested binary. This
correction does not rewrite raw UART or programming evidence.

## Final committed-image scoped run

`run-003-final-commit/hardware-run/run-summary.json` records the machine result:

- 240 seconds of continuous acquisition;
- 16 UART1/UART3 heartbeats at 15-second intervals;
- two controlled `event trigger_test` events, IDs 47 and 48;
- both events downloaded and verified through UART3;
- `pool_backpressure` maximum: `0`;
- `pool_min_free` minimum: `49` blocks;
- FIFO, DMA, export, resource, queue, storage, feature and runtime counters: `0`;
- no fatal marker, storage health and model state remained valid;
- result: `SCOPED_PASS`.

Raw UART logs, frame records, event files, trigger records and hashes are under
`run-003-final-commit/hardware-run/`. Build and ST-LINK logs are in the parent
`run-003-final-commit/` directory. The earlier uncommitted-source run with
three events (44–46) is retained as supporting evidence in
`run-002-fixed-image/`, but the final conclusion uses the committed-image run.

## Scope boundary

This closes the independent pool-headroom defect for the current phase. It does
not claim the deferred two-hour G3 run, 72-hour continuous run, physical
disconnect matrix or controlled power-cycle matrix.
