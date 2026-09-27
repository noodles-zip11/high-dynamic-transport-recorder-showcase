# 版本管理工作流决策

## 决策

本项目采用“受保护的`main`、短生命周期任务分支、PR/MR软件门禁、独立硬件验收”的工作流。

`main`表示当前可集成的软件基线，而不是“所有实物测试均已完成”的发布承诺。进入`main`的增量必须通过适用的软件检查，并在变更记录中明确硬件验证状态。

## 日常流程

1. 从最新`main`创建`feature/<task>`、`fix/<task>`或`docs/<task>`分支及对应工作树。
2. 一个分支对应一个可审查、可测试、可回退的任务；强耦合的小改动可以属于同一任务，但不要长期叠放未合并的功能分支。
3. 在分支上完成目标、相关自动化测试、固件构建和`git diff --check`，并记录结果。
4. 经授权的PR/MR审查通过后合入`main`。远端启用后，保护`main`，禁止直接推送和强推，并把必要检查设为合入门禁。
5. 确认合并安全后，经授权移除或复用该任务工作树。

## 硬件验证

无硬件时，软件测试只能证明软件行为和构建链，不得替代板级验收。PR/MR必须列出未验证项目、硬件风险和计划证据路径。

硬件到货或需要实板复现时，从最新`main`创建`feature/hardware-bringup`；若已定位到缺陷，使用`fix/<subsystem>-board-validation`。将串口日志、逻辑分析仪截图、波形、内存统计和长稳结果放入`evidence/`，并把必要修复与证据一同合回`main`。

硬件验证只针对已合入当前`main`的基线：上板前必须记录`git rev-parse main`、被测提交、分支和`git status --short`。未提交、未合并或只存在于其他工作树的代码，不得把其构建、烧录或实板结果写入主线共同维护表，也不得据此标绿；这类代码只能在所属工作树建立独立证据记录。

硬件项只有在实板确实达到验收条件，并保存可复查的真实原始日志、结果截图或仪器导出，且文件已链接到共同维护表并记录 SHA-256 后，才允许标记`🟢 已通过`。人工抄录、模拟输出、软件单元测试或缺少原始文件的 Markdown 说明，只能标为“已执行/证据不足”，不能写`PASS`或标绿。

每轮烧录前先用 SWD 只读保存整片内部 Flash 备份并计算哈希；身份、备份和测试区门禁未通过前禁止擦除/写入。默认非空测试区必须保持保护；任一前置门禁失败时保存证据并停止下游写操作。Bootloader、OTA 和未授权的其他工作树按独立范围验收，不改变本表状态。

仅在可演示的里程碑创建带说明的标签，并明确标注“软件验证通过”或“硬件验证通过”。

## 当前迁移

此前的BSP、采集、事件和应用结构分支是硬件未到货期间形成的临时叠放链。首次整理时，先确认每个工作树的未提交内容归属，再把包含全部已审查软件工作的最顶端分支合入`main`。此后所有新任务从更新后的`main`开始。

## 依据

- [GitHub：受保护分支](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches)
- [GitHub：GitHub Flow](https://docs.github.com/en/get-started/using-github/github-flow)
- [GitHub：状态检查](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/collaborating-on-repositories-with-code-quality-features/about-status-checks)
- [GitLab：受保护分支](https://docs.gitlab.com/user/project/repository/branches/protected/)
- [GitLab：分支策略](https://docs.gitlab.com/user/project/repository/branches/strategies/)
