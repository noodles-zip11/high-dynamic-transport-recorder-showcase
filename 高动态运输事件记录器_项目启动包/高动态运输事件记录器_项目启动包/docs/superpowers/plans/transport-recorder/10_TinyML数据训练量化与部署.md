# TinyML数据、训练、量化与部署 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 训练并部署一个能区分运输事件类型或过滤误报的轻量模型，用真实独立测试证明它相对规则基线有价值，而不是为了简历硬贴“AI”。

**Architecture:** MCU保留确定性阈值触发，确保异常不会因模型漏判而完全丢失；TinyML对已触发窗口做分类、置信度评分或二级筛选。训练、量化和部署共享同一特征定义与版本清单。

**Tech Stack:** Python 3.12、NumPy、pandas、scikit-learn、PyTorch或TensorFlow、ONNX/TFLite、STM32Cube.AI、CMSIS-DSP、int8量化。

---

## 0. 开发前Worktree门禁

在修改数据处理、特征、训练、量化或模型运行时前，先阅读仓库根目录的[AGENTS.md](../../../../../../AGENTS.md)，并创建本阶段专用工作树：

```powershell
git rev-parse --is-inside-work-tree
git status --short
git worktree add ..\高动态运输事件记录器-worktrees\tinyml-pipeline -b feature/tinyml-pipeline main
Set-Location ..\高动态运输事件记录器-worktrees\tinyml-pipeline
git branch --show-current
git status --short
```

只有第一条输出`true`、当前分支为`feature/tinyml-pipeline`且两次状态检查均无输出时，才允许开发。若Git无效、`main`不存在、目录或分支已存在，立即停止并先运行`git worktree list`排查；不得删除或硬重置来绕过。数据划分、模型与评估证据必须可追溯，提交、推送和PR仍需显式授权。

## 1. 先明确AI任务

第一版选择“事件分类”，类别控制在可采集、可重复的范围：

- 正常搬运/背景振动；
- 碰撞或敲击；
- 跌落；
- 持续振动；
- 倾倒/翻转可作为后续类别。

模型输入为固定长度六轴窗口，输出各类概率和最高类别。阈值触发仍保存原始事件，AI结果作为事件元数据。

不承诺：自动判断货损、适用于所有包装、达到安全认证或替代实验室冲击仪器。

## 2. 数据协议先于采集

建立 `docs/ai/dataset_protocol.md`：

| 字段 | 示例含义 |
|---|---|
| session_id | 一次连续实验 |
| device_id | 记录器序列号 |
| mounting_id | 安装方向/位置 |
| package_id | 箱体与缓冲材料 |
| operator_id | 操作者匿名编号 |
| action_class | 预定义动作 |
| repetition | 重复序号 |
| start/end | 有效窗口 |
| confidence | 标注可信度 |
| notes | 异常说明 |

- [ ] 每类至少多个日期、安装方向和强度。
- [ ] 同一连续采集切出的相邻窗口必须属于同一数据分组。
- [ ] 原始文件只读保存，裁剪与特征均由脚本生成。
- [ ] 标注规则写出正例、反例和模糊样本处理。
- [ ] 记录固件、传感器配置、量程和采样率。

## 3. 设计可复现实验

先做小规模可重复数据集：

- 每个核心类别至少100次独立动作；
- 至少3个采集日；
- 至少2种安装方向；
- 至少2种操作者或动作来源；
- 背景数据时长显著大于事件数据。

跌落试验使用保护盒和受控小高度，不摔电池与裸板；每次动作记录高度、表面和缓冲条件。模型成绩必须注明实验范围。

## 4. 数据检查脚本

```text
ai/
  configs/dataset_v1.yaml
  src/validate_dataset.py
  src/build_windows.py
  src/split_dataset.py
  src/features.py
  src/train_baseline.py
  src/train_model.py
  src/quantize_model.py
  src/evaluate.py
  tests/
```

执行：

```powershell
.\.venv\Scripts\python.exe -m ai.src.validate_dataset --config ai/configs/dataset_v1.yaml
.\.venv\Scripts\python.exe -m ai.src.split_dataset --config ai/configs/dataset_v1.yaml
```

校验内容：采样率、通道数、CRC、时间单调、饱和比例、缺样、标签合法、重复文件哈希、会话泄漏和类分布。

## 5. 严格划分避免数据泄漏

- 训练、验证、测试按 `session_id` 或更高层的package/device分组划分。
- 同一次撞击的重叠窗口不能一部分进训练、一部分进测试。
- 测试集在模型选择期间冻结。
- 标准化参数只从训练集计算。
- 数据增强只作用于训练集。

将划分清单和随机种子保存为文件；报告不仅写准确率，还写每类样本数和分组来源。

## 6. 先做规则基线

规则基线使用可解释特征：

- 峰值合加速度；
- 峰峰值；
- RMS；
- 持续时间；
- 峰度或crest factor；
- 低/中/高频带能量；
- 角速度峰值；
- 姿态变化近似量。

- [ ] 亲手计算一条样本的RMS和峰值，与脚本结果对照。
- [ ] 用训练集选择阈值，在验证集调优。
- [ ] 冻结后只运行一次测试集。
- [ ] 输出混淆矩阵、precision、recall、F1和每小时误报数。

AI模型必须与该基线同测试集比较。

## 7. 两条模型路线

路线A：特征 + 小MLP。

- MCU计算固定点特征；
- 输入维度小、易解释；
- 适合第一版部署和端侧一致性验证。

路线B：短窗口1D CNN。

- 直接输入归一化六轴序列；
- 可能捕捉时序形态；
- RAM、算力和量化调试成本更高。

先完成路线A。只有路线A不能达到目标且资源允许时才进入路线B。

## 8. 评价门槛

在冻结测试集上同时满足：

- 模型相对规则基线的误报率降低至少20%，同时关键事件召回不下降超过2个百分点；或
- 若目标是多分类，宏平均F1有明确提升，并且每类召回达到项目规定下限；
- int8模型与浮点模型指标差距可解释；
- MCU输出与PC量化参考输出一致；
- 推理时间和内存满足预算。

“准确率95%”不能单独作为通过标准，因为背景类别可能占多数。

## 9. 特征一致性

- [ ] Python和C分别实现同一特征接口。
- [ ] 选20个黄金窗口，保存原始输入和预期特征。
- [ ] 明确窗口长度、步长、单位、去均值、滤波、饱和处理和定点缩放。
- [ ] 每个特征设绝对/相对误差阈值。
- [ ] CI或本机测试比较C输出与Python输出。

若使用CMSIS-DSP，先对单个滤波器/FFT黄金向量验证，不一次性接入整个算法链。

## 10. 量化与STM32Cube.AI部署

- [ ] 使用代表性训练数据做int8校准，记录样本来源。
- [ ] 保存浮点模型、量化模型、输入输出scale/zero-point和转换日志。
- [ ] 用STM32Cube.AI分析Flash、RAM、MACC和支持算子。
- [ ] 生成网络代码后包在 `model_runtime` 深模块中，业务层不依赖生成文件细节。
- [ ] 模型工作区放入适合的AXI SRAM，确认与DMA/Cache策略兼容。
- [ ] 在板上运行同一批黄金输入，对比PC量化结果。

推理时机：事件窗口完成后由低优先级AI线程处理，不在IMU ISR和采集线程内推理。

## 11. 模型清单与兼容性

每个模型附 `model_manifest.json`，包含：

```text
model_version
dataset_hash
training_code_revision
class_names
input_shape
sample_rate_hz
feature_version
quantization
expected_runtime_version
model_crc32
evaluation_report_hash
```

固件启动时验证输入形状、采样率、特征版本、运行时版本和CRC。不兼容模型不得加载，系统回退到规则基线并进入DEGRADED。

## 12. 板上性能与回归

- [ ] 用DWT周期计数器测特征和推理时间，报告平均、P95和最大值。
- [ ] 从链接map和运行时统计记录Flash、静态RAM和峰值工作区。
- [ ] 连续推理10000次，无内存增长和HardFault。
- [ ] 同时采集、写Flash、USB传输时不造成采样丢失。
- [ ] 保存至少50个未参与训练的实物事件做端到端盲测。

## 13. 可解释输出与失败策略

事件保存：规则触发原因、模型类别、各类分数、模型版本、输入质量标记。

- 低置信度标记unknown，不硬分到某一类；
- 输入缺样或饱和过多时跳过模型并记录原因；
- 模型运行错误时保留原始事件和规则结果；
- 上位机明确区分人工标签与模型预测。

## 14. 学习检查

你应能解释：

- 为什么相邻窗口随机切分会产生数据泄漏？
- 量化的scale和zero-point是什么？
- 为什么模型不能放在中断里运行？
- 为什么规则触发仍需保留？
- 哪些证据能证明AI确实提高项目价值？

下一步：[11_Bootloader固件OTA与模型OTA.md](11_Bootloader固件OTA与模型OTA.md)
