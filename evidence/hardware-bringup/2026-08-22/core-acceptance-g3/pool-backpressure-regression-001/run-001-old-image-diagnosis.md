# Diagnostic only: old image was flashed

This run is intentionally excluded from acceptance. The first programming
command staged the main-tree image `4c47fe6` (128,952 bytes, SHA-256
`2F0E676507D1591060BA759D8F81C6DE472D302988172A22DEF627365B5E878A`) instead
of the fix worktree image. UART1 confirmed the old revision, and event 42
reproduced the known symptom: `pool_backpressure` rose to `13`.

The run was stopped after the mismatch was detected. It was replaced by the
committed-image run in `run-003-final-commit/hardware-run/`.
