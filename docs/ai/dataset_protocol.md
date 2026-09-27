# 运输事件 TinyML 数据集协议 v1

## 1. 目的与边界

本协议定义第一版运输事件分类数据怎样采集、标注、保存和划分，使后续训练结果可追溯、可复现。

模型的用途是对**已经由确定性阈值触发并保存**的事件给出类型和置信度；它不能替代触发器，也不能因模型低置信度而丢弃原始事件。原始事件是证据，始终只读保存。

第一版只覆盖一个明确范围：使用同一类运输包装和本记录器，对下列四类运动做事件分类。它不声称判断货损、适用于所有包装，或替代冲击试验仪器。

## 2. 术语

| 术语 | 定义 |
|---|---|
| `session` | 一次连续、条件一致的实验采集；它是训练、验证、测试划分的最小分组单位。 |
| `event` | 固件阈值触发后保存的一份完整原始事件。 |
| `window` | 从已验证事件中按固定规则取出的定长六轴波形，作为模型输入。 |
| `label` | 对一个事件或窗口的人为类别判断及其可信度。 |
| `pending` | 尚未由板卡、实验记录或资料确认的真实值；不得用猜测替代。 |

一个 `session` 可以包含多个 `event`，一个 `event` 可以生成多个 `window`。同一 `session` 的所有事件和窗口必须进入同一个数据集分区。

### 支持的事件格式

- EV03 是新固件的正式训练数据来源。它在 EV02 基础上增加丢失样本数量、缺口范围和缺口次数。
- EV02 可作为兼容数据使用，但必须通过同一个严格解析器和完整性检查。
- EV01 原始文件继续保留。它缺少当前数据质量和冻结上下文，未经显式、可追溯的迁移流程不得进入 v1 训练集。

EV03 中 `lost_sample_count > 0` 的事件仍保留在数据集中，但默认排除训练。不得用插值掩盖丢样后再把它当成完整原始事件。

## 3. 第一版标签

| `action_class` | 正例 | 不应标为该类 | 采集方式 |
|---|---|---|---|
| `background` | 静止、正常人工搬运、轻微环境振动 | 明显冲击、受控跌落、持续强振动 | 在没有刻意冲击的条件下连续记录。 |
| `impact` | 单次敲击、碰撞、短促冲击 | 连续抖动；受控跌落 | 用固定物体轻敲已包装箱体，记录作用面和力度等级。 |
| `continuous_vibration` | 连续一段时间的机械或车辆式振动 | 单次敲击 | 记录振动来源、持续时间和安装方式。 |
| `drop` | 已装入保护箱的受控低高度跌落 | 裸板或带电池的不可控摔落 | 使用安全实验区域；记录高度、落面和缓冲条件。 |

以下情况标为 `unknown`，从第一版监督训练集中排除，但原始事件必须保留：动作过程未观察到、多个类别混合、传感器饱和/缺样过多、或标注者不能有把握判断。

训练类别合同由配置中的 `model.class_names` 冻结，正式顺序为
`[background, impact, continuous_vibration, drop]`；历史二分类工件只允许使用
该顺序的 `[background, impact]` 前缀。固件模型输出类别数不得超过 4，类别位置不能
靠训练脚本临时重排。

定时离线采集的每个类别结束前 30 秒属于 `excluded_tail_30s`：固件报告保留
这些 EV03 原始记录和事件编号，主机清单把它们标为 `unknown`，并从 train、
validation、test 的全部窗口中排除。该排除只改变训练资格，不修改、删除或重写
原始 EV03；清单仍必须保留它们的来源和可追溯关系。

`tilt`（倾倒/翻转）不属于第一版类别。它依赖安装方向和姿态基准，待真实安装方式验证后单独设计。

## 4. 每个 session 的记录

每次开始连续实验前创建一条会话记录。字段使用 UTF-8 文本；ID 只用小写字母、数字、`-` 和 `_`。

| 字段 | 要求 | 示例 |
|---|---|---|
| `session_id` | 唯一；推荐 `YYYYMMDD_序号` | `20260803_01` |
| `device_id` | 记录器序列号，未知时为 `pending` | `pending` |
| `operator_id` | 匿名操作者编号 | `operator_01` |
| `date_local` | 采集日期 | `2026-08-03` |
| `mounting_id` | 安装位置和六轴朝向的文字说明 | `box_bottom_x_right` |
| `package_id` | 箱体和缓冲材料版本 | `carton_a_foam_01` |
| `action_class` | 第 3 节中的一个值 | `impact` |
| `repetition` | 动作重复序号或范围 | `01-20` |
| `sample_rate_hz` | 设备实际配置；未知时为 `pending` | `1600` |
| `accel_range` / `gyro_range` | 传感器量程；未知时为 `pending` | `pending` |
| `firmware_revision` | 采集固件的 Git revision | `pending` |
| `notes` | 表面、力度、跌落高度、异常等 | `wood_table_side_tap` |

原始 `event` 清单关联 `session_id`、`event_id`、原始文件哈希、最终 `label` 和
`label_confidence`（`high`、`medium`、`low`）。窗口起止位置由窗口脚本生成，属于
派生 window 清单；它不能反写或覆盖原始 event 清单。`event_id` 只要求在一个
session 内唯一，所以任何派生数据都必须同时保留 `session_id`，不能只保存事件编号。

`pending` 是采集准备期允许的诚实记录，但含有 `pending` 必填元数据的事件不得进入训练；板卡证据确认后应补录真实值并重新生成数据集清单。

### 4.1 已冻结的硬件档案

硬件验收完成后，项目配置 `ai/configs/dataset_v1.yaml` 固定以下输入条件：

| 参数 | 固定值 | 说明 |
|---|---|---|
| 板卡 / MCU | `openmv4_h743` / `STM32H743VIT6` | 当前工程的 OpenMV4 H743 目标 |
| IMU 模块 / 芯片 | `GY-601N1` / `ICM45686`，`WHO_AM_I=0xE9` | GY-601N1 资料中的 BMI323、ICM42688 是其他可选芯片资料，本项目不采用 |
| 加速度量程 | `16g` | ICM45686 固件配置 |
| 陀螺仪量程 | `2000dps` | ICM45686 固件配置 |
| 采样率 | `1600 Hz` | 固件配置和 EV03/TERP 记录一致 |
| TERP 固件标识 | `phase08-terp-uart3` | 设备 `info` 返回的协议固件标识 |
| AI 采集 Git revision | `203bfaa55ab905375c640216e78b8d438e25a89c` | 第一批数据的历史采集基线；不是当前 V1 `main`/tag |
| 辅助模块 | `SHT40` | 第一版模型暂不作为六轴输入 |

量程和采样率是数据可比性的前提；校验器会把不匹配的 session 保留在原始数据中，
但排除出训练候选。`mounting_id` 不放进全局硬件档案，因为安装位置和六轴朝向是
实验变量；每次连续实验都必须写清楚它，并且改变安装方式就新建 session。第一批 AI
数据统一使用上表的历史采集 revision，session 仍重复记录该值，防止原始数据脱离配置
后失去来源；后续采集必须记录实际镜像身份，不能继续复制这个旧值。

本项目第一批采集固定安装标识为
`box_bottom_right_xright_yrear_zup`，含义是：打开箱体、面对箱体前侧时，IMU 模块
固定在箱底右下角；模块芯片面朝箱盖，Z+ 向上；X+ 指向箱体右侧壁；Y+ 指向箱体
后侧/铰链侧。请在模块或固定板上贴上 X+/Y+/Z+ 方向标记，照片和 session 的
`mounting_id` 必须与此一致。若改变其中任一方向，必须创建新的 `mounting_id`。

本次连接板卡的只读姿态探针从 UART3/TERP 导出了设备中已有的 event 1 和 event 2，
并用正式 EV02/EV03 解码器校验。两份记录均为 1600 Hz、2400 点、零丢样，且
`imu_transport_error_count`、`imu_dma_error_count`、存储错误和导出错误均为 0；
静止段的加速度均约为 `(-70,-72,+2058)` 和 `(+28,-67,+2059)` counts。
在当前 16g 量程下约 2048 counts 为 1g，因此这验证了当前 ICM 的 Z+ 朝箱盖/上方。
重力静止读数不能区分水平面的 X+/X- 或 Y+/Y-；它们必须由箱体基准和贴在模块上的
方向标记固定，不能根据模型训练结果事后旋转。探针记录与事件哈希见
`evidence/hardware-bringup/2026-08-16/current-icm-orientation-readonly.md`。

硬件证据入口：

- `evidence/hardware-bringup/curated-closeout.md`：身份、U2 和 IMU 验收摘要；
- `evidence/hardware-bringup/2026-08-14/feature-hardware-bringup-icm-30min/attempt-4/verification-summary.md`：1600 Hz 采集长稳；
- `evidence/hardware-bringup/2026-08-15/main-two-hour-regression-001/results/main-final-verification.json`：TERP EV03 的 `sample_rate_hz=1600`、零丢样和解码通过。

## 5. 采集和保存规则

1. 在一次 session 内保持包装、安装方向、动作方法和设备配置不变；任何改变都开启新的 session。
2. 原始事件从设备通过正式 TERP 导出；先完成协议、长度和 CRC 校验，后保存和处理。
3. 原始事件文件不得被裁剪、重采样或覆盖。窗口、CSV、特征和模型输入必须由脚本从原始文件生成。
4. 每次动作后记录是否真的发生预期动作。无触发、重复触发或异常情况写入 `notes`，不事后删除记录。当前黑匣子里的放入、轻敲和轻微搬运应标记为 `controlled_bench_simulation`，不能冒充真实运输或跌落数据。
5. 在硬件和安全流程未确认前，不做裸板、裸电池或不可控跌落实验。

V1 发布基线已经启用自然触发，不再使用 `UINT32_MAX` 禁用检测：impact profile 为
模长不低于 5120 counts、连续 2 个样本，drop profile 为模长不高于 1536 counts、连续
8 个样本；一次事件接纳后进入 30 秒 cooldown。V1 实板验收包含 10 分钟静置无误触发和
自然事件写入/读回，但这些阈值不是 AI 标签，也不证明跨安装条件的触发泛化；边界见
[`V1 实板验收发布快照`](../../evidence/releases/v1.0.0/hardware/hardware-acceptance.md)。

立即触发的 `event trigger_test` 和延迟触发的
`event trigger_delay <delay_ms>`（范围 1–60000）仍只用于协议、固件诊断或明确标注的
受控采集。此类记录必须在 `notes` 中写明 `manual_trigger` 或
`manual_trigger_delayed`、延迟值和动作相对触发时刻，不能把手动触发本身误标成自然碰撞。
使用专用 timed/button 采集镜像时，也必须记录镜像身份和采集模式，不能把它冒充 V1 发布
镜像；禁止临时改寄存器或烧录身份不明的固件。

数据集验证器使用上位机现有的 EV02/EV03 严格解析器，不在 `ai/` 重复解释二进制协议。验证器把结果分成两类：

- **错误**：文件缺失、路径越界、哈希不符、CRC/格式错误、事件 ID 不符或重复记录。命令返回非零。
- **质量排除**：`unknown`、必填元数据仍为 `pending`、EV03 报告丢样、采样率不符或饱和比例超限。原始文件继续保留，命令可以成功，但事件不得进入训练。

除 `unknown` 外，event 的最终标签必须与 session 的 `action_class` 一致；若实验中
实际发生了另一类动作，应创建或修正 session 记录，而不是留下互相矛盾的来源信息。

执行入口：

```powershell
.\.venv\Scripts\python.exe -m ai.src.validate_dataset --config ai/configs/dataset_v1.yaml
.\.venv\Scripts\python.exe -m ai.src.split_dataset --config ai/configs/dataset_v1.yaml
```

正式训练入口必须消费已经写入且不可覆盖的冻结划分；它不会根据训练 seed 重新分组。先由
`build_feature_dataset` 生成 `features.npy`、`labels.json`、`groups.json` 和
`event_ids.json`，再使用同一份 split manifest 和原始数据 manifest。入口会重新计算并校验
原始 manifest SHA-256 以及 split 记录的 `dataset_hash`，避免只把来源哈希写进报告而不校验：

```powershell
.\.venv\Scripts\python.exe -m ai.src.train_model `
  --features artifacts/features/features.npy `
  --labels artifacts/features/labels.json `
  --groups artifacts/features/groups.json `
  --event-ids artifacts/features/event_ids.json `
  --feature-metadata artifacts/features/metadata.json `
  --dataset-manifest datasets/manifest.yaml `
  --split-manifest ai/artifacts/split_manifest_v1.json `
  --output ai/artifacts/model-v1/model `
  --report ai/artifacts/model-v1/training-report.json `
  --generated-header ai/artifacts/model-v1/ai_model_data.h `
  --generated-source ai/artifacts/model-v1/ai_model_data.c `
  --training-revision <采集/训练代码 Git revision>
```

训练报告会绑定源清单、split、特征输入、训练代码、模型包和生成 C 的 SHA-256；缺少事件 ID、
分区不覆盖全部行或身份/标签不一致时命令失败。`pilot-v1/training-report.json` 在该入口加入
之前生成，只能作为历史占位证据。

项目配置中的采样率已经固定为 `1600`；只有尚未完成硬件确认的临时配置才允许保留
`null`。硬件档案中的采样率必须和 `quality.expected_sample_rate_hz` 一致。

配置模板位于 `ai/configs/dataset_v1.yaml`，清单格式示例位于
`ai/configs/manifest_v1.example.yaml`。示例中的 `pending` 和假哈希用于说明格式，
本身不具备训练资格。

建议最小实验量：每个核心类别至少 100 次独立动作，至少 3 个采集日、2 种安装方向和 2 个动作来源；`background` 的总时长应明显长于事件类。它是进入模型评估的目标，不是当前硬件未就绪时的完成声明。

## 6. 防止数据泄漏

训练、验证和测试必须按 `session_id` 或更高层的 `package_id`/`device_id` 分组，而不是按单个 window 随机划分。相邻或重叠窗口共享同一次物理动作，若分别落入训练和测试，测试结果会虚高。

测试集在模型选择前冻结；标准化参数、特征参数和数据增强只能从训练集得出。任何类别不足或会话来源单一的情况必须在评估报告中明确写出，不能用总体准确率掩盖。

划分器按标签对 session 做可复现分组，保存随机种子、数据集哈希、每类 session
数量和每个 event 的分区。已存在但内容不同的划分文件不会被覆盖；调整数据或种子
必须输出新的版本文件。划分文件同时保存原始清单的 SHA-256；因此原始文件、标签、
会话元数据或清单内容变化后，都必须重新验证并生成一个新的版本化划分文件。
每个实际参与划分的类别至少需要 3 个合格 session，保证 train、validation、test
各有一个独立来源；不足时划分器明确失败，不生成看似完整但没有测试来源的清单。

对于本轮每类只有一个受控批次的过渡性软件验证，只能显式把 `split.grouping` 设置为
`pilot_event_stratified`。该模式按 `class + session_id + event_id` 做确定性事件级约
70/15/15 分层，要求每类每个 split 非空、同一事件身份不跨 split，并在 manifest/report
中写出 `within_session_pilot=true` 与“不代表独立 session 泛化”。它不伪造 session，也
不替代正式独立 session 门禁；默认值仍是 `session_id`。

## 7. 软件可独立验证的边界

当前可以验证：EV02/EV03 完整性与质量门禁、session 防泄漏划分、触发对齐/滑动
窗口的边界、count 域峰值/RMS/crest factor 等数学结果、规则基线接口，以及混淆矩阵、
precision、recall、F1、事件召回和每小时误报的计算。

窗口长度、步长、频带特征、规则阈值、模型类型和量化校准仍依赖真实独立数据。
`ai/configs/dataset_v1.yaml` 对这些值保留 `null` 或空规则，不用模拟数据伪造结论。
窗口脚本会读取该配置：`background` 使用滑动窗口，其他训练事件使用触发对齐窗口；
参数仍为 `null` 时会明确拒绝生成窗口，不会偷偷采用默认值。规则基线也从同一配置
读取，只接受已声明的标签和 `counts_v1` 特征；空规则只表示接口就绪，不表示已经有
有效阈值。

评价工具接受冻结预测 JSON 并输出机器可读报告。`truth`、`predictions`、
`session_ids` 和 `event_ids` 一一对应 window；同一个 event 的所有 window 必须使用
相同真值标签。
混淆矩阵和每类指标按 window 统计；事件召回按 `event_ids` 聚合，只要该关键事件的
任一 window 被判为非背景就算检出。背景误报也先按 event 聚合，再除以输入中明确
记录的真实背景时长。若不提供 `event_ids`，工具会把每行视为一个独立事件。

```powershell
.\.venv\Scripts\python.exe -m ai.src.evaluate --input predictions.json --output evaluation.json
```

要输出候选方案相对规则基线的差值，必须使用完全相同的真值、事件 ID、类别和背景
时长：

```powershell
.\.venv\Scripts\python.exe -m ai.src.evaluate `
  --input model-predictions.json `
  --output model-evaluation.json `
  --baseline-input baseline-predictions.json `
  --comparison-output model-vs-baseline.json
```

正式模型/基线对照必须使用 hash-linked bundle 入口；它只接受有效的 v2 模型包、可解析的
规则基线配置、冻结 split 的 `test` 分区、与 `metadata.json` 行数一致的 `features.npy`，
并要求两份 prediction JSON 的身份和真值按 `partitions.test` 的顺序完全一致：

```powershell
.\.venv\Scripts\python.exe -m ai.src.generate_evaluation_report `
  --model-input model-predictions.json `
  --baseline-input baseline-predictions.json `
  --model-package ai/artifacts/model-v1/model `
  --baseline-config ai/configs/dataset_v1.yaml `
  --split-manifest ai/artifacts/split_manifest_v1.json `
  --features artifacts/features/features.npy `
  --feature-metadata artifacts/features/metadata.json `
  --prediction-code scripts/run_predictions.py `
  --output evaluation-bundle.json
```

两份 prediction JSON 都必须包含以下相同的 `provenance` 对象；入口会重新计算这些文件的
SHA-256，并拒绝 train/validation 行、标签篡改、行重排、特征矩阵/metadata 不一致、模型包
版本或权重哈希无效、baseline 配置缺少正背景时长的输入：

```json
{
  "provenance": {
    "model_manifest_sha256": "...",
    "model_weights_sha256": "...",
    "baseline_config_sha256": "...",
    "split_manifest_sha256": "...",
    "feature_matrix_sha256": "...",
    "feature_metadata_sha256": "...",
    "prediction_code_sha256": "..."
  }
}
```

窗口与规则配置的 Python 入口分别为
`ai.src.build_windows.load_window_config()`、
`ai.src.build_windows.build_configured_windows()` 和
`ai.src.train_baseline.load_rule_baseline()`。它们已经可以测试数据契约，但真实参数仍
必须由训练/验证数据确定，不能查看冻结测试集后再修改。

## 7.1 已冻结的 v1 模型输入契约

主机和固件现在共同使用 `docs/ai/model_contract_v1.md`：完整 EV03 事件 2400 点、
1600 Hz、`ax, ay, az, gx, gy, gz` 六轴顺序、`counts_v1` 六维特征，以及
`[background, impact, continuous_vibration, drop]` 类别顺序。历史二分类模型使用该
顺序的前缀。模型量化方案固定为 int8，但具体尺度和零点只
从训练分区校准后写入模型包；不能人工填写，也不能从测试集反推。

## 8. 首次硬件采集前检查表

- [x] 设备身份、IMU 实际量程和采样率已从板卡或固件证据确认，并写入硬件档案。
- [x] 通过 TERP 成功导出至少一个完整原始事件，且 CRC、样本长度和时间顺序有效。
- [ ] 按 `box_bottom_right_xright_yrear_zup` 摆放并拍照记录设备在箱内的安装位置和六轴朝向。
- [ ] 为本次实验创建 session 记录；未知事实明确填 `pending`。
- [ ] 实验场地和受控跌落方式安全，不涉及裸板或电池跌落。

通过本检查表只说明数据采集链可开始；不表示模型已经有效，也不表示硬件已完成全部验收。
