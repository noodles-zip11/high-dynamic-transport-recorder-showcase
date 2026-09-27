# Phase 05 minimal acceleration trigger（历史决策，已被 V1 取代）

> **SUPERSEDED：** 本页记录 Phase 05 在没有实测模块时的安全默认值，不是 V1 当前配置。
> V1 已采用 impact 5120 counts / 连续 2 samples / ABOVE，以及 drop 1536 counts /
> 连续 8 samples / BELOW 的双 profile 自然触发，并在一次接纳后执行 30 秒 cooldown。
> 当前实现以 `firmware/app/event/event_service.c` 为准，实板结论见
> [`evidence/releases/v1.0.0/hardware/hardware-acceptance.md`](../../evidence/releases/v1.0.0/hardware/hardware-acceptance.md)。
> 下文只为解释早期设计演进而保留。

## Fixed mechanism

- Input: raw signed ICM45686 acceleration samples at 1.6 kHz.
- Comparison: `ax² + ay² + az²` against an integer squared threshold; no
  floating point or square root is used in the high-rate path.
- Debounce: two consecutive above-threshold samples produce a trigger fact.
- Event window: 1 s pretrigger plus 2 s post-trigger normally; repeated
  triggers extend post-trigger capture only up to 3 s, for a 4 s total cap.
- Trigger facts carry sample sequence, magnitude squared, configured threshold
  squared and an axis mask.

## Historical Phase 05 boundary

At the Phase 05 checkpoint, `threshold_magnitude_sq` had not been calibrated
without the actual ICM45686 module. That firmware started its automatic
detector with `UINT32_MAX`, which disabled natural triggers until measured
thresholds were supplied. `event trigger_test` asked the event service to use
the next real block as a trigger; it did not fabricate sensor samples.

## Historical calibration plan

Record at least ten runs each for:

1. static device;
2. light knock;
3. protected small drop.

For each run retain EV01, parser validation result, plotted waveform, selected
raw threshold, false-trigger count and missed-trigger count. Use those results
to choose the first production threshold and update this decision.
