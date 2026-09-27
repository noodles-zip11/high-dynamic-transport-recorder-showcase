# V1 正式版候选软件门禁

## 候选身份

- 日期：2026-08-24
- 隔离工作树：`LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\v1-release-candidate-20260824`
- 分支：`feature/v1-release-candidate-20260824`
- 固件源码候选提交：`46a131af376033c26b0ebf59f724c1fbcfeb0004`
- 固件身份：`profile=Release revision=46a131af376033c26b0ebf59f724c1fbcfeb0004 serial=recorder-001`
- 基线及回滚来源：`main@58477d38b700e3edad3a7d144a6916f5f574ffce`
- 状态：软件候选门禁通过；尚未刷板，尚未完成 V1 实板验收，不创建 `v1.0.0` 标签。

证据文档提交位于固件源码候选提交之后，只记录门禁，不改变固件源码。构建产物嵌入并对应上述 `46a131a...` 固件源码提交。

## 纳入范围

- 真实四分类 AI：固定顺序为 `background`、`impact`、`continuous_vibration`、`drop`；包含训练/导出合同、四类模型、golden vectors、OTA 2..4 类兼容和 2560 B AI worker 栈保护。
- IMU FIFO：按完整 16 B 包分块；4112 B 积压按 `1024/1024/1024/1024/16` 连续排空。
- 事件数据合同：32 samples/block；25 个预触发块和 50 个后触发块；每条事件固定 75 块、2400 samples。序列不连续、短块或发布失败不会形成可提交事件。
- 正式自然触发：impact 为合加速度不小于 2.5 g、连续 2 samples；drop 为合加速度不大于 0.75 g、连续 8 samples；首次命中后 one-shot；完成后冷却 30 s。
- 存储：沿用主线 EV03/QSPI 日志布局，不自动覆盖旧事件，不后台格式化。
- 低功耗：仅普通 Cortex-M7 WFI，顺序为 `clear SLEEPDEEP -> DSB -> WFI -> ISB`；SysTick、IMU INT1、DMA、UART 可唤醒。DMA、事件、存储、AI、UART、OTA 生命周期接入 blocker；`power status` 暴露 entries/wakes/skips/blocker；STOP/tickless/WOM/关陀螺仪均未启用。
- 生产候选不包含按键采集 campaign、定时/五分钟采集、现场临时分区或为了凑样本而周期写入背景事件。

真实 AI 集成提交祖先包括 `7882202`、`510c082`、`f978855`、`2c978fb`，均已进入 `46a131a...`。

## 自动化验证

最终 Debug 总门禁命令：

```powershell
pwsh -NoProfile -File .\scripts\run_tests.ps1 -WithFirmware -FirmwareProfile Debug -FirmwareRevision 46a131af376033c26b0ebf59f724c1fbcfeb0004 -FirmwareSerial recorder-001
```

结果：exit 0。

- scripts/protocol fixture：1 passed。
- firmware native：47 个 executable，全部 PASS。
- bootloader native/image：PASS，image 使用 24,968 / 131,072 B，无 RT-Thread symbols。
- Host pytest：131 passed。
- AI pytest：78 passed。
- Debug 固件：ROM 217,664 B；RAM 271,632 B；D2_SRAM1 2,144 B；`text=216132 data=1532 bss=272244`。
- application vector、memory map、ICM45686 alignment：PASS。
- 嵌入身份：完整候选 revision，`profile=Debug`，`serial=recorder-001`。

最终 Release 构建命令：

```powershell
pwsh -NoProfile -File .\scripts\build_firmware.ps1 -BuildProfile Release -GitRevision 46a131af376033c26b0ebf59f724c1fbcfeb0004 -DeviceSerial recorder-001 -RequireElf
```

结果：exit 0；无编译 warning。

- Release 固件：ROM 135,760 B；RAM 271,388 B；D2_SRAM1 2,144 B；`text=134388 data=1372 bss=272160`。
- application vector、memory map、ICM45686 alignment：PASS。
- BIN 内可以读取完整 revision `46a131af376033c26b0ebf59f724c1fbcfeb0004`。
- BIN 未发现 `collect timed`、`field capture`、`periodic capture`、`ai-timed-button`、`collection campaign`、`pending capture` 标记。

## Release 产物

| 产物 | 绝对路径 | 大小 | SHA-256 |
|---|---|---:|---|
| ELF | `LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\v1-release-candidate-20260824\firmware\build\transport_recorder.elf` | 222,568 B | `1DF1E8D7C629FB8947502098473595DCAE5361E5B9BAE06B0F069A8D8EEF24C7` |
| BIN | `LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\v1-release-candidate-20260824\firmware\build\transport_recorder.bin` | 135,760 B | `C610DDE842B8F6358AC2DA686221DAA042A7C78D434ABCAD174595ED30E997CF` |
| MAP | `LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\v1-release-candidate-20260824\firmware\build\transport_recorder.map` | 838,917 B | `CD40463967C03EB1D2CC985E4517DC54481E62B91124DD365FA8B77B9ECE17E5` |

构建产物不纳入 Git；后续重新构建会改变含构建时间的 ELF/BIN 哈希，应重新记录，不能沿用本表。

## 保护与回滚

- 主仓库 HEAD 核验为 `58477d38b700e3edad3a7d144a6916f5f574ffce`，未切分支、未合并、未提交。
- 主仓库原有 `MISSION.md` 修改及未跟踪资料保持原状；本任务未清理或纳入它们。
- 本轮未 push、未 merge、未 tag、未 flash、未格式化 QSPI、未读取或提交私有 raw 数据。
- 刷板前应备份当前内部 Flash并记录板上 revision；失败时恢复该备份。源码级回滚基线为 `main@58477d3`。

## 未完成的实板门禁与风险

- 电流、真实节电百分比、9000 mAh 电池续航和封板温升均未测量；当前只能声明 WFI 软件链完成，不能声明真实节电量或续航数字。
- V1 候选尚未验证实板 impact/drop 阈值、EV03 固定 2400 samples、AI sidecar、QSPI 写入和 WFI blocker 长时无泄漏。
- 不要求再次真实路测；静置/正常拿取、可选书包活动、敲击和软垫低高度掉落实验足以做 V1 端到端验收。
- 非阻断 P2：DMA blocker 获取失败时 DMA 仍可能继续；storage begin/write/abort blocker 获取失败时仍可能 I/O；AI mutex 获取失败边界仍需实板观察；UART 已验证 drain helper，但实际 UART 全链仍需板上确认。
- 当前模型已证明端到端四分类链路可用，但不宣称覆盖所有运输环境的生产级泛化准确率。

结论：`46a131a...` 可作为 V1 实板候选进行备份、刷写和下面的硬件验收；在清单完成前不得称为已验收的 `v1.0.0` 正式发布。
