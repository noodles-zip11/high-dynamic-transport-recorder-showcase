# 来源与构建身份

- V1 不可变基线：`v1.0.0^{}` = `a69b6c6c71b91e1267780c169760eca284b7523b`；当前 `main`/HEAD 可以位于该 tag 之后。
- 既有 tag object：`75d183e21bd99795ec5d487d0363d0bdc83aaf1e`；本包不移动 tag。
- 外部 QSPI source identity：`full-qspi-event-backup/summary.json`，summary SHA-256 `e24a44b1c9c6b8df4e119cc23e35ca7c80034263683e076bf106f2a0a0392228`，204 条连续事件，raw 不入 Git。
- 候选固件 revision：`46a131af376033c26b0ebf59f724c1fbcfeb0004`。
- Release ELF：222,568 B，SHA-256 `1DF1E8D7C629FB8947502098473595DCAE5361E5B9BAE06B0F069A8D8EEF24C7`。
- Release BIN：135,760 B，SHA-256 `C610DDE842B8F6358AC2DA686221DAA042A7C78D434ABCAD174595ED30E997CF`。
- Release MAP：838,917 B，SHA-256 `CD40463967C03EB1D2CC985E4517DC54481E62B91124DD365FA8B77B9ECE17E5`。

产物哈希来自 V1 software gate；本证据包提交不重新构建、不刷板、不修改固件。板上候选等价性以该 revision、software gate 和硬件验收记录共同界定。
