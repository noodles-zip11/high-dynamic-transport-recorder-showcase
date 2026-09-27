# Real AI pilot 发布摘要

来源：`evidence/phase2-ai/2026-08-23/real-ai-pilot.md`，SHA-256 `093c1088c39f4db5bf72c6b9572534be5d62ea6be38016ea228566542f58dd4a`。

- 类别顺序：`background`, `impact`, `continuous_vibration`, `drop`。
- 真实合格事件 91 条；显式 controlled-pilot split 为 train 64、validation 14、test 13。
- Release int8 test accuracy：`0.7692308`（10/13）；macro-F1：`0.755952`。
- 四类 test recall：background `0.75`、impact `1.0`、continuous_vibration `0.3333`、drop `1.0`。
- `within_session_pilot=true`；每类目前只有一个受控 session，不代表独立 session 泛化，也不替代至少三个独立 session/类的正式门禁。
- float/int8 test prediction parity 为 `1.0`；模型 CRC32 为 `0xAD10980E`；权重 SHA-256 为 `F4EF4C310F808F0DC76E59E4F2296FC97D5A90118C73506E34C995E47E53FB78`。

当前仓库中的 `ai/artifacts/pilot-v1/` 是历史二分类兼容工件；本四分类发布快照依据本页来源、`docs/ai/model_contract_v1.md` 和固件模型数据合同生成，不把二分类文件冒充四分类 manifest。结构化摘要见 `training-report.json` 和 `model-manifest.json`。
