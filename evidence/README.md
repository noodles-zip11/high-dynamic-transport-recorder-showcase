# 证据索引

## 当前发布状态

V1 的唯一对外状态入口是 [`releases/v1.0.0/`](releases/v1.0.0/README.md)。该包绑定
`v1.0.0^{}`=`a69b6c6c71b91e1267780c169760eca284b7523b`，并集中给出：

- [软件门禁](releases/v1.0.0/software/software-gate.md)；
- [实板验收](releases/v1.0.0/hardware/hardware-acceptance.md)；
- [四分类真实 AI pilot](releases/v1.0.0/ai/real-ai-pilot.md)；
- 低功耗证据与边界见[发布包“明确限制”](releases/v1.0.0/README.md#明确限制)；
- [来源、构建身份和哈希](releases/v1.0.0/provenance/source-and-builds.md)。

## 历史证据规则

`hardware-bringup/`、`phase11-ota/`、`phase2-ai/`、`v1/` 和各 phase 目录是按日期保存的
原始或阶段性证据。里面的 `current`、`BLOCKED`、`NOT DONE`、测试数量和提交号只描述当时
固定点，不能覆盖上面的 V1 发布状态。旧失败、串口原始流、机器 JSON 和产物哈希不会因为
后续修复而删除；发现错误身份时，在对应摘要添加 erratum，同时保留原始字节。

因此：查看“现在能做什么”先读发布包；追查“结论怎样形成”再进入历史目录。模拟测试不能
替代实板结论，AI 预测不能替代人工真值，派生 PNG 也不能替代原始 EV03。

## 校验已提交发布包

仅校验仓库中已提交的 manifest、SHA-256、链接和公开边界时，不需要外部 raw：

```powershell
python scripts/generate_v1_evidence_package.py --verify `
  --output evidence/releases/v1.0.0
```

`--source-backup` 只在重新生成事件 metadata 和 PySide6 PNG 时需要。
