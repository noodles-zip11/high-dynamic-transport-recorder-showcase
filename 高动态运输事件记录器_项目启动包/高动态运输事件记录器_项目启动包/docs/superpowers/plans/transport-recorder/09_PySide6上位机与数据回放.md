# PySide6上位机与数据回放 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建一个不阻塞界面、能管理设备与事件、能回放和导出证据的桌面上位机，并复用已经验证的Python协议库。

**Architecture:** 协议核心、串口传输、设备会话、数据仓库和Qt界面五层分离。串口I/O在工作线程，UI只通过信号接收不可变视图模型；原始事件文件保持只读，SQLite只存索引和用户标注。

**Tech Stack:** Python 3.12、PySide6、PyQtGraph、pyserial、SQLite、NumPy、pytest、pytest-qt。

---

## 0. 开发前Worktree门禁

在修改上位机、数据库、下载或回放逻辑前，先阅读仓库根目录的[AGENTS.md](../../../../../../AGENTS.md)，并创建本阶段专用工作树：

```powershell
git rev-parse --is-inside-work-tree
git status --short
git worktree add ..\高动态运输事件记录器-worktrees\desktop-app -b feature/desktop-app main
Set-Location ..\高动态运输事件记录器-worktrees\desktop-app
git branch --show-current
git status --short
```

只有第一条输出`true`、当前分支为`feature/desktop-app`且两次状态检查均无输出时，才允许开发。若Git无效、`main`不存在、目录或分支已存在，立即停止并先运行`git worktree list`排查；不得删除或硬重置来绕过。界面截图不能代替协议和数据测试，提交、推送和PR仍需显式授权。

## 1. 目录与依赖边界

```text
host/
  pyproject.toml
  transport_recorder/
    protocol/
    transport/
    device/
    repository/
    analysis/
    ui/
    cli.py
    app.py
  tests/
    unit/
    integration/
    ui/
```

约束：

- `protocol` 不导入Qt和串口；
- `transport` 不理解事件业务；
- `device` 组合请求、状态和取消；
- `repository` 不修改原始事件；
- `ui` 不手工拼TERP帧。

## 2. 先做无设备演示数据

- [ ] 从第五或第八阶段保存三份合法事件样本。
- [ ] 保存一份CRC错误、一份截断、一份未知版本样本。
- [ ] 建立 `host/tests/fixtures/events/`。
- [ ] 所有回放和导出功能先对fixture测试，不依赖每次插实板。

这能让GUI开发与硬件调试解耦，也能避免把偶发串口问题误判为绘图问题。

## 3. 手工搭建最小Qt窗口

第一版亲手写代码，不急着用Qt Designer：

- `QMainWindow`；
- 左侧设备/事件导航；
- 中央堆叠页面；
- 底部连接状态和操作进度；
- 菜单中的打开本地事件、导出、设置和关于。

理解布局和信号槽后，可用Qt Designer制作复杂表单，但 `.ui` 文件必须纳入版本管理并由脚本可复现转换或动态加载。

启动命令：

```powershell
.\.venv\Scripts\python.exe -m transport_recorder.app
```

## 4. 设备连接页

- [ ] 列出端口、描述、硬件ID和最近成功连接设备。
- [ ] 用户点击连接后进入会话状态机，不在UI线程直接open/read。
- [ ] HELLO完成后显示序列号、硬件版、固件版、协议版和健康状态。
- [ ] 断连时禁用危险操作，但保留已下载数据查看。
- [ ] 支持取消连接和手动重连。
- [ ] 多个相似串口存在时以设备序列号确认目标。

## 5. 事件列表与下载

列表字段：事件ID、UTC/时间无效、触发类型、峰值、持续时间、环境、模型版本、下载状态。

- [ ] 分页请求，不一次加载设备全部记录。
- [ ] 下载显示字节进度、速度和预计剩余时间。
- [ ] 取消后保留可续传临时文件。
- [ ] 下载完成先验证CRC，再进入本地索引。
- [ ] 删除设备事件需显示事件ID并二次确认。
- [ ] 下载成功不自动删除。

## 6. 本地仓库

目录建议：

```text
data/
  events/<device_serial>/<event_id>.terp-event
  exports/
  recorder.db
```

SQLite保存：文件路径、设备、事件摘要、标签、备注、导入时间和校验状态。

- [ ] 数据库迁移有版本号。
- [ ] 文件先落盘并校验，再提交数据库事务。
- [ ] 原始事件按内容校验，不用文件名判断身份。
- [ ] 找不到文件时标记missing，不默默删除索引。
- [ ] 用户备注与原始元数据分开。

## 7. 回放页

至少显示：

- 三轴加速度、合加速度；
- 三轴角速度；
- 触发点、阈值、预/后触发区；
- 环境和健康快照；
- 数据缺口、饱和、CRC或时间无效提示。

- [ ] 大文件使用分层抽样或可见窗口加载，不能把百万点全塞进Qt曲线。
- [ ] 缩放时显示原始极值，不用会丢峰值的简单隔点抽样。
- [ ] 鼠标游标显示统一时间轴和各通道值。
- [ ] 单位与量程来自事件元数据，不写死在界面。

## 8. 导出与报告

支持：

- CSV：带字段名、单位、事件和版本元数据；
- JSON：事件摘要、健康、标签和校验信息；
- PNG：当前视图；
- 原始文件：字节不变复制并附SHA-256。

- [ ] CSV数值精度足够恢复原始计数或明确标注已换算。
- [ ] 导出不覆盖已有文件，除非用户确认。
- [ ] 报告注明算法/模型版本和UTC有效性。
- [ ] 用脚本重新读回CSV，核对行数、首尾时间和峰值。

## 9. 实时预览

实时预览只用于观察设备，不替代板上可靠记录：

- 限制到例如100 Hz显示率，设备端仍按1 kHz记录；
- 通信拥塞时丢预览帧而不是阻塞采集；
- UI显示“预览可能抽样”；
- 使用固定长度显示环，避免内存无限增长。

## 10. 线程、错误与取消

- [ ] 所有阻塞I/O在worker线程或受控异步层。
- [ ] UI更新只发生在主线程。
- [ ] 关闭窗口时先取消会话，再等待有限时间，不能无限挂起。
- [ ] 异常分为用户可恢复、设备拒绝、数据损坏和程序错误。
- [ ] 日志包含操作与错误码，不记录不必要的原始个人数据。
- [ ] 后台异常必须传回UI和日志，不能只打印到控制台。

## 11. 自动测试

```powershell
.\.venv\Scripts\python.exe -m pytest host/tests/unit -q
.\.venv\Scripts\python.exe -m pytest host/tests/integration -q
.\.venv\Scripts\python.exe -m pytest host/tests/ui -q
```

测试重点：

- 协议fixture导入；
- SQLite事务失败回滚；
- 下载取消和续传；
- CRC错误不会入库为有效；
- UI连接状态切换；
- worker异常传递；
- 大事件绘图不阻塞主线程。

## 12. 人工验收脚本

- [ ] 无设备启动，能打开本地事件。
- [ ] 连接设备，显示身份和健康。
- [ ] 下载一个事件并中途取消，再续传成功。
- [ ] 拔USB，UI不假死并提示断连。
- [ ] 重新插入后手动重连。
- [ ] 打开合法、损坏和未知版本文件，三者提示不同。
- [ ] 导出CSV/JSON/PNG并复核。
- [ ] 连续运行4小时，内存不持续增长。

## 13. 学习检查

你应能解释：

- 为什么CLI和协议库先于GUI？
- Qt主线程为何不能直接阻塞读串口？
- 为什么SQLite不应该吞掉原始事件本体？
- 曲线降采样为什么可能丢失冲击峰值？
- 下载完成、CRC正确、成功入库是三个不同状态吗？

下一步：[10_TinyML数据训练量化与部署.md](10_TinyML数据训练量化与部署.md)
