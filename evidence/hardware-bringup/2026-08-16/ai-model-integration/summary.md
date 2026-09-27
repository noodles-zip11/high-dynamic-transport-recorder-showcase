# AI model integration software slice

日期：2026-08-16
分支：`feature/tinyml-pipeline`

## 已完成

- 冻结主机/固件共同输入契约：1600 Hz、EV03 完整 2400 点、六轴顺序
  `ax, ay, az, gx, gy, gz`、`counts_v1` 六维特征、`[background, impact]`、int8。
- 主机端实现了会话分组的数据特征构建、确定性 NumPy MLP、训练集校准参数、
  版本化模型包和 C 模型数据导出器。
- H743 端实现了 `counts_v1` 特征提取、int8 MLP 运行时、模型版本/维度/CRC
  校验，以及低优先级有界 AI 队列。事件导出协议没有改变。
- 未安装模型时固件保持 fallback，原始事件仍正常保存；没有把临时模型烧录到板卡。

## 验证信号

- `python -m pytest ai/tests -q`：56 passed
- `python -m pytest host/tests -q`：124 passed
- `scripts/test_native.ps1`：Native C tests: PASS（含 AI 特征、运行时、队列）
- `scons -C firmware -j4 -Q`：ELF/BIN 构建成功，AI 模块进入固件链接
- `git diff --check`：仅有既有 PowerShell 换行风格提示，无空白错误

## 当时的阶段边界

本文件是 2026-08-16 的软件阶段快照；当时尚未烧录模型，也没有 HIL 推理结果。
后续 Pilot v1 已完成实板冒烟验证，最新结论和原始日志见
`evidence/hardware-bringup/2026-08-17/ai-pilot-hil/summary.md`。真实箱内运输
泛化、更多独立 session 和正式现场模型验收仍未宣称。
