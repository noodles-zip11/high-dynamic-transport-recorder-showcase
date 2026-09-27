# V1 软件门禁发布快照

来源：`evidence/v1/2026-08-24/software-gate.md`，SHA-256 `d8b3a8dbee17513f6df10b2c6cbc2775469a77ea1eaa61f12136367e5d79f613`。

- Debug 总门禁 exit 0：native 47 个 executable、Host 131、AI 78；固件/vector/memory-map/ICM alignment 通过。
- Release 构建 exit 0，无编译 warning；ROM 135,760 B，RAM 271,388 B，D2_SRAM1 2,144 B。
- Release ELF 222,568 B，SHA-256 `1DF1E8D7C629FB8947502098473595DCAE5361E5B9BAE06B0F069A8D8EEF24C7`。
- Release BIN 135,760 B，SHA-256 `C610DDE842B8F6358AC2DA686221DAA042A7C78D434ABCAD174595ED30E997CF`。
- Release MAP 838,917 B，SHA-256 `CD40463967C03EB1D2CC985E4517DC54481E62B91124DD365FA8B77B9ECE17E5`。
- 软件证据包含四分类模型、EV03/2400、4112 B FIFO 分块、自然触发 one-shot/30 s cooldown 和 WFI-only power policy。

这是一份来源摘要，不把软件 gate 当作所有实板可靠性矩阵；真实电流/续航、G2、物理断连和 72 小时长稳仍有独立边界。
