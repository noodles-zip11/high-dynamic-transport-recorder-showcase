# V1 实板验收发布快照

来源：`evidence/v1/2026-08-25/hardware-acceptance-001.md`，SHA-256 `c6b7f940cc74aa2dfe4fd904095a1bdd83523397abb5a57ce84b80d85727a682`。

- mandatory checklist 1/3/4/5/6：PASS；书包 30 分钟按清单为 SKIP，不写成 PASS 或 FAIL。
- 10 分钟静置：21 次每 30 s 采样保持 event 179，无误触发，storage/export error 为 0；WFI attempts/entries/wakes 递增，blocker holds 为 0，STOP 编译和允许值均为 0。
- event 180–202 连续、共 23 条；全部 EV03、2400 samples、CRC 通过、subtrigger=0、lost=0、AI status=1、class_count=4。
- 计划最后 5 次掉落对应 event 191–195，`planned_action=drop` 只是验收记录中的计划映射，不是逐事件人工真值；五条 MCU 预测均为 `drop`。event 180–195 中其他动作的逐条真值不完全确定，不计算动作分类准确率。
- 本轮没有 continuous_vibration 动作，因而不提供该类人工真值结论。
- 外部 full-QSPI backup 包含 event 001–204，summary SHA-256 为 `e24a44b1c9c6b8df4e119cc23e35ca7c80034263683e076bf106f2a0a0392228`；原始文件和逐条 readback 没有纳入 Git。

V1 硬件结论不声称 AI 泛化准确率、不声称低功耗电流/续航，也不把 format 后的容量状态误写成原始日志仍在 QSPI。
