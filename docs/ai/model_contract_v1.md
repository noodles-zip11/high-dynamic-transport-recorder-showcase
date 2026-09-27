# TinyML 模型输入契约 v1

输入/特征契约仍为 v1；本轮完整模型 CRC 字段序列的运行时版本为 v2。旧的
`feature_mlp_runtime_v1` 包不能与新的 MCU 校验语义混用，必须重新导出。

这是主机训练和 H743 固件推理之间的“插头”。任何一项改变，都必须提升
`feature_version` 或模型包版本，不能只改一边。

| 项目 | v1 固定值 |
|---|---|
| 事件来源 | 完整、CRC 已验证的 EV03 |
| 事件长度 | 2400 点，1600 Hz，即 1.5 s |
| 原始轴顺序 | `ax, ay, az, gx, gy, gz` |
| 原始单位 | ICM45686 有符号 raw counts；不在模型入口偷偷换单位 |
| 特征版本 | `counts_v1` |
| 特征顺序 | 峰值、峰峰值、RMS、持续时间、crest factor、陀螺峰值 |
| v1 类别顺序 | `[background, impact, continuous_vibration, drop]` |
| 模型路线 | 六维特征 → 小型 ReLU MLP → 四类 logits（固件上限 4） |
| 量化 | int8 输入/权重/隐藏激活；校准参数只来自训练分区 |

窗口的触发点继续保存在事件元数据中，但第一版特征模型使用完整 2400 点，
不把触发索引作为额外输入。这避免了当前人工延迟触发在约 800 点附近的微小
偏移变成模型的“捷径”。

训练类别合同由 `ai/configs/dataset_v1.yaml` 驱动，类别只能使用上述固定顺序的
2 类历史前缀或完整 4 类合同；不允许交换类别位置。历史 `pilot-v1` 二分类模型和
其 golden fixture 仍按二类兼容路径读取，新的四分类 CSV golden vectors 会按
`class_names` 动态生成和解析 logits/probability 列。

主机侧保存 `manifest.json` 和 `weights.npz`；导出工具再生成固件的
`ai_model_data.{h,c}`。固件启动时检查 magic、版本、维度、量化尺度和模型张量
CRC；检查失败时不运行神经网络，保留原始事件并进入 fallback 状态。

模型 CRC 的规范输入不是独立的复制字节，而是以下固定小端字段序列：模型头部维度和版本、
输入/隐藏量化尺度与零点、交错的 feature mean/scale、hidden-major 的第一层 int8 权重、
第一层权重尺度和 bias、第二层 int8 权重、第二层权重尺度和 bias。Host 导出器的
`_canonical_model_bytes()` 与 MCU 的 `ai_runtime_model_crc32()` 必须保持同一顺序；修改任一
实际推理参数都应使启动校验失败。

TRMD 导出器从模型 `manifest.class_names` 读取并校验固定顺序，允许历史二类前缀和当前
四分类合同（`class_count` 为 2..4），并将真实类别数写入 header。CSV golden vectors
可以按类别包含全部 logits/probabilities；为保持 QSPI/OTA 兼容，每条 TRMD golden record
仍固定 40 字节，只序列化 6 个特征、前两个 logits、`expected_class_index` 和 confidence。
因此四分类记录仍可用 `class_index=3`，但不会扩展现有 40B 布局。

历史二分类的 Host/native C 数值一致性使用共同 fixture
`ai/tests/fixtures/ai_golden_vectors.csv`；四分类 MCU/native 合同使用
`ai/tests/fixtures/ai_model_v1_four_class_golden_vectors.csv`，并由四分类导出/OTA 测试覆盖
package 布局和类别边界。两者都覆盖六维输入、logits、概率、类别和置信度，但不能把旧二类
fixture 说成当前四类共同文件。正式
评价使用 `python -m ai.src.generate_evaluation_report` 同时输出模型与规则基线的每类
precision/recall/F1、4×4（或配置类别数）混淆矩阵、事件召回和背景每小时误报。入口必须同时接收模型包、baseline 配置、
冻结 split、特征矩阵、feature metadata 和预测代码，并要求两份 prediction JSON 携带与这些
文件完全一致的 SHA-256；这仍不能替代真实数据的 C-11 泛化验收。

## V1 当前模型状态

V1 接入的是四分类受控真实数据 pilot，不再把历史二分类 `pilot-v1` 工件作为当前模型
状态。训练使用 91 条合格真实事件，固定 split 为 train 64、validation 14、test 13；
Release int8 测试为 10/13，macro-F1 为 0.755952，并已完成板端四分类推理、模型槽切换和
复位后持久化验证。固件启动时校验模型并启用低优先级推理队列；校验或推理失败仍保留
原始事件和 fallback。

`ai/artifacts/pilot-v1/` 只保留为历史二分类兼容工件，不能再用来描述 V1 当前能力。
四分类发布快照、指标和哈希见
`evidence/releases/v1.0.0/ai/real-ai-pilot.md`、
`evidence/releases/v1.0.0/ai/training-report.json` 和
`evidence/releases/v1.0.0/ai/model-manifest.json`。由于每类仍只有一个受控 session，
`within_session_pilot=true`；这证明真实数据与部署链路闭环，不证明独立 session 泛化。

发布包 `model-manifest.json` 中的 `model_contract` SHA-256 绑定不可变
`v1.0.0^{}` 快照；本页在 tag 之后只修正状态说明，因此当前工作树文件哈希不会与该历史
来源哈希相同。复核发布来源时应读取 tag 中的文件，而不是用后续 `main` 文档覆盖它。

## 四分类受控 pilot 状态

训练入口、特征构建和评估已经接受配置驱动的四类顺序；默认 `session_id` 划分仍要求
每个参与类别至少 3 个独立 session。`pilot_event_stratified` 只能显式 opt-in，按
`class + session_id + event_id` 确定性分层约 70/15/15，并在 manifest/report 中标记
`within_session_pilot=true` 及“不代表独立 session 泛化”。2026-08-23 的受控 pilot 已用
91 条合格真实事件训练并量化四分类模型，完成 TRMD 导出、MCU golden-vector 校验、活动模型
槽切换和复位后持久化验证。该结果只证明真实数据链路和板端部署闭环；每类仍只有一个受控
session，连续振动测试召回偏低，不能替代至少三个独立 session/类的正式泛化门禁。
