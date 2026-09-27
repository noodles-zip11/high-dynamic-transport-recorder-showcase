# V1.0.0 公开证据包

这是高动态运输事件记录器 V1 的精选、可公开、可复核证据包。包内保存软件/硬件/AI 摘要、完整 tracked evidence 索引、来源哈希、代表波形 PNG 和机器元数据；外部 QSPI raw 没有复制进 Git。

## 能力结论

- V1 候选已取得启动、自然触发、固定 EV03 事件、QSPI 持久化、TERP 下载、CRC、真实四分类推理和 Cortex-M7 WFI 状态链的实板证据。
- 软件候选 gate、硬件验收和 AI pilot 的来源与限制分别见 [software gate](software/software-gate.md)、[hardware acceptance](hardware/hardware-acceptance.md) 和 [AI pilot](ai/real-ai-pilot.md)。
- 代表波形由现有 Host 解码器和 PySide6 `ReplayWidget.export_png` 离屏导出；PNG 只表达派生可视化，不能替代原始事件。

## 明确限制

- AI 结果是受控真实数据 pilot：测试为 10/13，macro-F1 为 0.755952，四类 recall 见 AI 摘要；`within_session_pilot=true`，每类 session 数不足以证明独立 session 泛化。
- 低功耗证据只覆盖普通 Cortex-M7 Sleep/WFI、唤醒和 blocker 软件链；没有实测电流、真实节电百分比或 9000 mAh 续航结论，STOP/Standby 也不在本包的 V1 声明内。
- 本轮 V1 HIL 动作没有可核实的连续振动人工动作真值，因此不伪造该类波形或标签；受控 AI pilot 的连续振动训练/测试标签仍按 AI 摘要保留。AI 预测不是人工真值。
- G2 100 次断电、物理断连和 72 小时长稳属于 deferred reliability matrix，不写成 V1 核心 PASS。

## 公开边界和 provenance

- 不可变基线提交：`v1.0.0^{}=a69b6c6c71b91e1267780c169760eca284b7523b`；当前 `main`/HEAD 可以是该 tag 之后的后续提交（tag object `75d183e21bd99795ec5d487d0363d0bdc83aaf1e`）。证据包本身是后续文档提交，不改变固件/协议/模型。
- 外部完整 QSPI 备份只以来源身份和 `summary.json` SHA-256 `e24a44b1c9c6b8df4e119cc23e35ca7c80034263683e076bf106f2a0a0392228` 表示；204 条事件的原始 bytes 不在仓库。
- 包文件的大小和 SHA-256 见 [manifest](manifest.json) 与 [SHA256SUMS](SHA256SUMS.txt)。manifest 不对自身做自引用，SHA256SUMS 覆盖 manifest 和其他包文件，清单自身不在自身列表中。
- [provenance/source-and-builds.md](provenance/source-and-builds.md) 记录候选构建身份、产物哈希和可回滚边界；[catalog/evidence-inventory.csv](catalog/evidence-inventory.csv) 索引基线提交下所有 tracked evidence，而不是复制历史 evidence。

## 桌面派生图

图、逐图 metadata 和选择说明见 [desktop README](desktop/README.md) 与 [desktop index](desktop/index.json)。每个 metadata 都分开保存 `human_label`、`planned_action` 与 `ai_prediction`，并追溯 event ID、raw SHA-256、CRC、sample/loss/subtrigger。
