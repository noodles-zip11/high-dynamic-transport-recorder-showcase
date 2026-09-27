# Phase 11 OTA 实板验证与合并门禁 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 STM32H743VIT6 + 板载 W25Q64 上证明 Phase 11 的固件 OTA 在错误包、断连、复位与断电时不会破坏可恢复性，并在模型运行时契约可用后证明模型 OTA 不影响应用启动。

**Architecture:** 将验收分成严格顺序的三层：先证明 W25Q64 的读、页写与 4 KiB 擦除，再验证 Bootloader 的内部 Flash、状态页、跳转和恢复，最后经由实际 TERP 暂存服务完成升级、试运行与回滚。低层失败时停止上层诊断，回到厂商例程的同一操作逐项对照；厂商例程只作引脚/HAL 配置参考，项目的 W25Q64 容量、24 位地址和命令由本项目的板级配置控制。

**Tech Stack:** STM32H743VIT6、板载 Winbond W25Q64、C11、SCons、ST-Link/SWD、UART3/TERP 日志、Python 3.12 打包工具、受控电源开关。

---

## 1. 已知条件、范围与禁止项

- 工作树固定为 `LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\ota`，分支为 `feature/ota`。该工作树当前已有未提交的 Phase 11 代码；本计划不允许通过重置、清理或切换工作树来“获得干净状态”。
- 实物板资料位于 `D:\BaiduNetdiskDownload\STM32H743\`。经原理图及 Cube 生成文件核对，U3 为 8 MiB W25Q64；QSPI 信号为 `PB10` nCS、`PB2` CLK、`PD11` IO0、`PD12` IO1、`PE2` IO2、`PD13` IO3。设备应返回 JEDEC `EF 40 17`，使用 24 位地址、256-byte 页和 4 KiB 擦除块。
- 已有“能读取”的经验是 QSPI 读路径的先验证据，不代表写、擦、掉电一致性、Bootloader 跳转或 OTA 已通过。
- 例程对照基线是 `D:\BaiduNetdiskDownload\STM32H743\SDK\H7_DEMO\STM32H743VIT6_DEMO_25M XTAL\STM32H743VIT6_DEMO_25M XTAL\QSPI_W25Q64 - DMA\Core\Src\quadspi.c`。其中 `FlashSize = 22` 和 PB/PD/PE 的 AF9 映射可用于对照；`Components\Devices\bsp_qspi_w25q256.*` 与其 32 MiB/4-byte-address 注释不得用于本项目的 W25Q64 参数，也不得复制其 120 MHz 级别的时钟配置。
- 本项目保持保守单线命令：读 `0x03`、页程序 `0x02`、4 KiB 擦除 `0x20`、写使能 `0x06`、JEDEC `0x9F`；QSPI 实际频率必须记录并不高于 40 MHz。禁止芯片擦除、禁止 memory-mapped 模式、禁止在候选/恢复/模型槽中做低层试验。
- QSPI 低层实验唯一允许擦写区域是元数据区末尾一个扇区 `0x0000F000..0x0000FFFF`。在测试代码、布局检查器和证据文档中将其命名为 `qspi_hil_scratch_sector`；任何地址不完全落入此区都必须在发送 `0x06` 前返回错误。
- 内部 Flash 仅允许使用锁定的布局：Bootloader `0x08000000..0x0801FFFF`、应用 `0x08020000..0x081BFFFF`、主状态页 `0x081C0000..0x081DFFFF`、备状态页 `0x081E0000..0x081FFFFF`。实板试验前先用 SWD 备份当前可运行 Bootloader 和应用 BIN，并记录 SHA-256。

## 2. 合并定义：两个不能混淆的出口

| 出口 | 必须满足 | 不允许声称 |
| --- | --- | --- |
| 固件 OTA/Bootloader 可合并 | 所有软件缺口修复；完整自动化测试通过；本计划第 4 至第 9 节的板级记录通过；30 次受控断电不变砖；错误包不改变运行中应用 | “模型 OTA 已验收” |
| 完整 Phase 11 可合并 | 上述条件，外加第 10 节模型 A/B 的实机契约、黄金输入、断电和回退均通过 | “仅能存模型包” 等同于模型升级可用 |

Phase 10 的模型运行时/manifest 契约尚未合并时，完整 Phase 11 没有资格合并；可以完成并评审固件 OTA/Bootloader 子切片，但必须把模型出口标为阻塞项，不能以模拟结果替代它。

## 3. 预期文件结构与证据格式

- Modify: `config/memory_layout.yaml` — 明确 `qspi_hil_scratch_sector` 的地址和 4 KiB 长度，检查其位于 metadata、且不与任何包槽重叠。
- Modify: `firmware/config/memory_layout.h` and `tools/check_memory_map.py` — 生成/验证同一 scratch 常量，拒绝非该扇区的诊断写擦。
- Modify: `firmware/components/ota/ota_state.{c,h}` and `bootloader/src/bootloader_state_store.c` — 区分“两个状态页均全 `FF`”与“非空但损坏/半写”。
- Modify: `tools/package_firmware.py`, `tools/inspect_package.py`, `host/tests/test_ota_package_tools.py` — 用同一严格 `major.minor.patch` 语法校验 `minimum_bootloader_version`。
- Create: `firmware/tests/board/phase11_qspi_hil.c` — 仅编译进 HIL 诊断镜像；只操作 scratch 扇区，打印每一步的地址、长度、CRC 与错误码。
- Create: `firmware/tests/board/phase11_boot_hil.c` — 只触发预定义的 Bootloader 测试场景，不提供任意地址擦写或固件包绕过入口。
- Create: `firmware/tests/board/README-phase11-ota.md` — 固定接线、刷写、日志捕获、断电操作和恢复步骤。
- Create: `evidence/phase11-ota/README.md` — 证据索引、版本、板号、工具版本、已知风险和通过条件。
- Create per run: `evidence/phase11-ota/YYYY-MM-DD/<case-id>/` — 原始串口日志、`manifest.json`、包 SHA-256、SWD 读回哈希、断电记录和照片/示波器截图（若使用）。
- Modify: `protocol/terp_messages.yaml`, 生成的协议声明、应用 OTA 下载服务及相应 C/Python 测试 — 完成正常路径的 begin/write/query/finalize/cancel、重传幂等、完成校验与重启请求。

每个证据目录都保存下列最小 `result.md` 内容；失败也必须保存，不能覆盖：

```text
case_id: HIL-QSPI-ERASE-001
board: <silkscreen/serial>
bootloader_sha256: <64 hex>
application_sha256: <64 hex>
package_sha256: <64 hex or N/A>
qspi_jedec: EF4017
power_action: cold / reset / cut@checkpoint
expected: <one sentence>
observed: <one sentence>
verdict: PASS / FAIL / BLOCKED
raw_log: uart.log
```

## 4. Task 1: 清除已知的软件 P0/P1 与闭环缺口

**Files:** modify the OTA state/package tools above; add native and Python tests; complete the Task 4 files listed in the previous Phase 11 software plan.

- [ ] **Step 1: 写状态页分类的失败用例。**

  在 `firmware/tests/native/test_ota_state.c` 增加三个彼此独立的测试：两个完整 `0xFF` 页返回 `OTA_STATE_SELECT_UNINITIALIZED`；一个全 `FF` 加一个损坏页返回 `OTA_STATE_SELECT_RECOVERY`；两个非空且 CRC/commit 无效的页返回 `OTA_STATE_SELECT_RECOVERY`。

- [ ] **Step 2: 运行并确认当前实现无法区分空白与损坏。**

  Run: `pwsh scripts/test_native.ps1 -Target test_ota_state`

  Expected: 新增“一个空白加一个损坏页”断言失败，或编译失败提示 `OTA_STATE_SELECT_UNINITIALIZED` 未定义。

- [ ] **Step 3: 最小化修复状态选择。**

  在 `ota_state_select_newest()` 前先用一个只读 `all_bytes_are_ff()` 检查两个完整状态记录；仅这一个组合产生 `OTA_STATE_SELECT_UNINITIALIZED`。`bootloader_state_store_load()` 只对该新枚举值且应用向量有效时写入 `NORMAL`；任何非空无效记录都停在 `RECOVERY`。

- [ ] **Step 4: 写并运行版本语法一致性测试。**

  在 `host/tests/test_ota_package_tools.py` 参数化 `0.0.0`、`1.2.3`、`255.255.255` 为可接受，`1`、`1.2`、`1.2.3.4`、`01.2.3`、`1.-1.3`、`256.0.0` 为拒绝；分别调用 builder 和 inspector。

  Run: `.venv\Scripts\python.exe -m pytest host/tests/test_ota_package_tools.py -q`

  Expected: 所有非法值被两个工具以非零退出码拒绝，错误文本包含 `minimum_bootloader_version`。

- [ ] **Step 5: 让 Python 与 C 共用同一版本规则。**

  Python 侧使用完整匹配的 `^(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})$`，并在数值大于 255 时拒绝；C 侧保留等价的三段十进制解析。更新工具测试和 C 测试的相同边界向量。

- [ ] **Step 6: 完成真实正常路径，不以人工预写 QSPI 代替 OTA。**

  实现已有软件计划的 app-side TERP 暂存任务：begin 只接受空闲状态；write 对 offset/length/chunk CRC 做检查；同 offset 重传只有字节与 CRC 完全一致时成功；finalize 重读整包并验证 manifest/CRC/SHA 后才写入 `PENDING_INSTALL`；cancel 擦除候选包元数据并保持当前应用。恢复包由明确的已验证包写入 `qspi_recovery`，不得从候选槽猜测“旧版本”。

- [ ] **Step 7: 运行软件闭环门禁。**

  Run: `pwsh scripts/run_tests.ps1`; `pwsh scripts/build_firmware.ps1 -RequireElf`; `git diff --check`

  Expected: 全部退出码为 0；应用 ELF 起始地址为 `0x08020000`；Bootloader ELF 不超过 128 KiB；测试输出中包含 OTA package/state/slots、Bootloader 和 OTA download service。

## 5. Task 2: 建立可观察、不可误写的 HIL 诊断镜像

**Files:** create `firmware/tests/board/phase11_qspi_hil.c`, `firmware/tests/board/phase11_boot_hil.c`, `firmware/tests/board/README-phase11-ota.md`; modify the board-test SCons target and `scripts/run_phase11_hil.ps1`.

- [ ] **Step 1: 定义固定日志协议和检查点。**

  每行使用 ASCII 格式 `P11,<case>,<step>,<result>,<arg0>,<arg1>,<crc32>`。诊断镜像必须在启动时输出 `P11,BOOT,JEDEC,OK,EF4017,<qspi_hz>,<build_id>`，在每个擦写前输出目标地址和长度；任何非 scratch 地址输出 `P11,QSPI,GUARD,REJECT,<address>,<length>,0` 后返回。

- [ ] **Step 2: 让 QSPI 诊断只链接单线轮询驱动。**

  `phase11_qspi_hil.c` 只通过项目的 QSPI 抽象调用 `0x9F/0x03/0x06/0x02/0x20`，不使用 DMA、MDMA 或 memory-mapped 模式。页程序必须在 256-byte 边界切分；每次程序/擦除后轮询 WIP 清零，再读回并比较 CRC32。

- [ ] **Step 3: 增加启动和恢复可观察性。**

  `phase11_boot_hil.c` 为 `NORMAL`、`PENDING_INSTALL`、`INSTALLING`、`TRIAL`、`ROLLBACK_REQUIRED`、`RECOVERY` 输出状态码，并输出所选状态页代次。Bootloader 释放版本不提供任意读写命令；仅允许读取版本、状态、错误码和受限恢复结果。

- [ ] **Step 4: 在台面上验证日志链路。**

  使用 SWD 刷写 HIL 诊断镜像并捕获 UART3/TERP 原始日志；断开调试器后冷启动一次。若 UART3 未接出，使用 SWD RTT 或半主机的等价原始日志，但必须在 `README-phase11-ota.md` 固定其中一种，不能只看 IDE 的临时窗口。

  Expected: 至少一次无调试器冷启动可保存完整 `P11,BOOT` 行。

## 6. Task 3: QSPI 底层实板门禁（先于 OTA）

**Files:** HIL diagnostic sources and `evidence/phase11-ota/YYYY-MM-DD/HIL-QSPI-*`.

- [ ] **Step 1: 记录厂商例程基线。**

  在未改动的厂商工程中捕获一次现有读成功日志，记录固件版本、板号、JEDEC 三字节和所读地址。只使用 `quadspi.c` 中的 AF9 引脚与 `FlashSize=22` 作为硬件对照；记录其 PLL/Prescaler 的计算值，不把它复制为项目运行频率。

- [ ] **Step 2: 证明项目驱动的身份与频率。**

  烧录项目 HIL 镜像，连续执行 100 次 `0x9F`。每次必须为 `EF4017`；日志中的计算 QSPI 频率不得大于 40 MHz。任一次不符即停止，不运行任何写擦测试。

- [ ] **Step 3: 验证读路径边界。**

  对 scratch 扇区的偏移 `0x000`、`0x0F0`、`0x0FF`、`0x100`、`0xF00` 和 `0xFFF` 执行 32-byte 读；读取完成后冷启动并重复。所有读请求必须被限制在 `0x0000F000..0x0000FFFF`。

- [ ] **Step 4: 验证擦除和页写。**

  依次执行：擦除 scratch；读回 4096 bytes 全 `FF`；写入 256-byte `00..FF`；在偏移 `0x0F0` 写入 32-byte `A5/5A` 交替模式（驱动必须拆页）；在偏移 `0xF00` 写入 256-byte伪随机模式；每一步读回并比较 CRC32。完成后冷断电至少 5 秒、重新上电、再次读回三个 CRC32。

- [ ] **Step 5: 验证稳定性和写保护。**

  重复“擦除—三种模式写入—读回”100 次，仅操作 scratch 扇区。随后尝试对 `0x00010000`（候选槽起点）和 `0x00210000`（模型 A 起点）发起诊断写擦；两个请求都必须在未发送写使能前被 `GUARD,REJECT` 拒绝。保留第 1、50、100 次的完整日志和最终 CRC32。

**Stop condition:** JEDEC、时钟、读回、WIP 超时、页边界、地址保护任一失败，停止 OTA 级测试。先在厂商例程执行同一最小操作，再逐项比对电源、PB2/PB10/PD11/PD12/PE2/PD13、AF9、`FlashSize=22`、时钟、命令、地址字节数和等待就绪逻辑；没有该对照结论不得尝试修改状态机或重刷应用。

## 7. Task 4: Bootloader 启动、状态页与跳转实板门禁

**Files:** `bootloader/`, boot HIL source, `evidence/phase11-ota/YYYY-MM-DD/HIL-BOOT-*`.

- [ ] **Step 1: 建立可恢复的基线。**

  用 SWD 读出并保存 Bootloader、可运行应用和两个状态页；刷入当前 Bootloader 与带版本日志的基线应用。对冷启动、NRST、软件复位、IWDG 复位各执行 10 次。每次应用必须报告 `VTOR=0x08020000`、自身版本和状态页代次；Bootloader 区读回 SHA-256 不变。

- [ ] **Step 2: 验证仅空白状态页会初始化。**

  擦除两个状态页后启动一次有效应用，预期仅一次写入 `NORMAL`。分别制造“主页全 FF/备页单字节损坏”和“两个页均有非 FF 损坏记录”；两种情况下预期进入 `RECOVERY` 且不跳转到应用。此步骤在能通过 SWD 恢复基线前不得进行。

- [ ] **Step 3: 验证向量拒绝与跳转清理。**

  在测试应用镜像中分别产生：MSP 非 RAM、Reset_Handler 非应用区、Reset_Handler Thumb 位清零、完整 manifest CRC 错误。四种镜像均必须不跳转，状态日志为 `RECOVERY`。再恢复有效应用并重复四类复位，确认 Bootloader 禁中断、SysTick 停止、VTOR/MSP 设置后可稳定交接。

- [ ] **Step 4: 验证内部 Flash 写入的最小单元。**

  使用一个确认无用的测试应用，覆盖 32-byte Flashword 写后读回、每个 128 KiB 应用区擦除单元、擦后全 `FF`、最终 CRC32/SHA-256。在每个操作后复位一次；失败时只允许由 Bootloader 进入恢复，禁止跳到部分写入的应用。

## 8. Task 5: 正常固件 OTA、错误包与断连门禁

**Files:** TERP OTA download service, host OTA client/tests, `evidence/phase11-ota/YYYY-MM-DD/HIL-FW-*`.

- [ ] **Step 1: 准备三个可识别包。**

  生成 `recovery-1.0.0`、`candidate-1.1.0` 和四种损坏包：产品 ID 错、硬件 ID 错、目标地址/长度错、payload CRC 或 SHA-256 错。每个包用 `tools/inspect_package.py` 输出 JSON 并保存 SHA-256；恢复包先经正常 TERP 写入 recovery slot 并全包重读验证。

- [ ] **Step 2: 证明错误包零副作用。**

  对四种损坏包分别执行 begin/write/finalize。每次 finalize 必须失败，候选 manifest 不得成为有效包，状态页不得进入 `PENDING_INSTALL`，当前 `recovery-1.0.0` 应用在一次冷启动后仍可运行。保存前后 candidate/recovery 状态和应用版本日志。

- [ ] **Step 3: 证明 10 次断连可续传。**

  对 `candidate-1.1.0` 以固定 4 KiB 块传输，在第 0%、10%、20%、30%、40%、50%、60%、70%、80%、90% 后各断开一次。每次重连均先 `query`，从最后一个已验证连续 offset 继续；重复发送上一块必须得到幂等成功，内容不同的同 offset 块必须失败。第 10 次后 finalize 一次通过，并仅此时进入 `PENDING_INSTALL`。

- [ ] **Step 4: 验证一次无故障安装。**

  在候选与恢复包均完整时重启设备。记录状态序列 `PENDING_INSTALL -> INSTALLING -> TRIAL`、每个内部 Flash 擦除单元和最终 CRC/SHA-256。新应用必须在 30 秒内完成既有关键健康检查后显式确认，最终状态为 `CONFIRMED`；确认前的复位不得被误认为已确认。

## 9. Task 6: 安装断电、试运行与回滚门禁

**Files:** Bootloader checkpoints/tests and `evidence/phase11-ota/YYYY-MM-DD/HIL-POWER-*`.

- [ ] **Step 1: 固定断电机制。**

  使用可手动断开的 3.3 V 主供电或受控电源开关；每次断电持续至少 5 秒，确认 3.3 V 降至 0 V 后才恢复。NRST 不可替代断电。用 HIL 日志的 `P11,INSTALL,<checkpoint>,READY` 行触发操作，并记录操作者、时间和 checkpoint。

- [ ] **Step 2: 执行 30 个预定义断电点。**

  每个点从完整 `recovery-1.0.0` + 已验证 `candidate-1.1.0` 的基线恢复后开始：

  ```text
  01 PENDING_WRITTEN              02 INSTALLING_WRITTEN
  03 ERASE_BEGIN_0                04 ERASE_END_0
  05 COPY_0_PERCENT               06 COPY_25_PERCENT
  07 COPY_50_PERCENT              08 COPY_75_PERCENT
  09 COPY_99_PERCENT              10 ERASE_BEGIN_MID
  11 ERASE_END_MID                12 COPY_MID_0_PERCENT
  13 COPY_MID_25_PERCENT          14 COPY_MID_50_PERCENT
  15 COPY_MID_75_PERCENT          16 COPY_MID_99_PERCENT
  17 ERASE_BEGIN_LAST             18 ERASE_END_LAST
  19 COPY_LAST_0_PERCENT          20 COPY_LAST_50_PERCENT
  21 COPY_LAST_99_PERCENT         22 IMAGE_CRC_DONE
  23 IMAGE_SHA_DONE                24 TRIAL_STATE_PREPARE
  25 TRIAL_STATE_COMMIT            26 FIRST_TRIAL_RESET_HANDLER
  27 TRIAL_HEALTH_10_SECONDS       28 TRIAL_HEALTH_29_SECONDS
  29 CONFIRM_STATE_PREPARE         30 CONFIRM_STATE_COMMIT
  ```

  每次恢复供电后允许的结果只有：继续/重装候选并最终确认 `1.1.0`；从 recovery slot 重装并最终确认 `1.0.0`；停在可读取状态的 `RECOVERY`。任何无法由 SWD 或受限救援路径恢复的状态均为 FAIL。

- [ ] **Step 3: 验证未确认与故障 trial 的回退。**

  生成两个专用测试候选应用：`trial-never-confirm` 在 30 秒内持续运行但不调用确认；`trial-fault` 在输出启动标记后触发受控 HardFault。两者均必须超过允许尝试次数后由 recovery slot 恢复 `1.0.0`，不得永久循环擦写，不得跳到随机向量。

- [ ] **Step 4: 做最终冷启动回归。**

  在 30 次断电及两个 trial 失败后，以已确认的新版本和已确认旧版本各做 20 次冷启动、NRST、软件复位、IWDG 复位。全部 160 次启动都必须输出匹配的应用版本和有效状态；Bootloader SHA-256 与 QSPI recovery 包 SHA-256 必须保持不变。

## 10. Task 7: 模型 OTA 的依赖与实机门禁

**Files:** Phase 10 model manifest/runtime contract, model OTA service/tests, `evidence/phase11-ota/YYYY-MM-DD/HIL-MODEL-*`.

- [ ] **Step 1: 在接入模型包前冻结 Phase 10 契约。**

  定义并实现 model manifest 的模型格式版本、特征版本、输入形状、量化参数、模型长度、CRC32、SHA-256、模型版本和黄金输入/输出向量 SHA-256。缺少其中任一字段的包必须拒绝，不能以“能写进 QSPI”通过。

- [ ] **Step 2: 验证不影响应用的 A/B 写入。**

  向非活动模型槽写入有效模型；写入中断电 10 次后，应用仍须从内部 Flash 启动并继续使用原活动模型。只有整包校验和黄金向量均通过后，才以状态页原子切换活动槽。

- [ ] **Step 3: 验证模型失败回退。**

  对特征版本不符、输入形状不符、CRC/SHA 错、黄金向量输出错四种模型分别测试。四种都必须保持旧模型活动；将两槽均置为无效后，应用必须启动并使用固件规则基线，同时日志报告模型不可用。

## 11. Task 8: 证据审查与合并判定

**Files:** `evidence/phase11-ota/README.md`, all case directories, relevant tests and design decision documents.

- [ ] **Step 1: 逐项审查证据完整性。**

  证据索引必须列出 HIL-QSPI、HIL-BOOT、HIL-FW、HIL-POWER、HIL-MODEL 的每个 case ID、固件/包哈希、日志路径、预期和结论。缺失原始日志的 PASS 改为 BLOCKED；只有“屏幕上看起来成功”的记录也改为 BLOCKED。

- [ ] **Step 2: 运行最终软件门禁。**

  Run: `pwsh scripts/selfcheck.ps1 -Mode HostOnly`; `pwsh scripts/run_tests.ps1`; `pwsh scripts/build_firmware.ps1 -RequireElf`; `git diff --check`

  Expected: 全部退出码为 0，且测试清单覆盖状态页空白/损坏区分、版本语法、错误包、chunk 重传、完整包校验、Bootloader 镜像大小和应用链接地址。

- [ ] **Step 3: 给出结论而非自动合并。**

  仅当固件 OTA/Bootloader 的所有 case 为 PASS、没有 P0/P1、并且证据可复现时，才能标为“固件 OTA/Bootloader 可合并”。模型任务通过前，完整 Phase 11 仍为 BLOCKED。提交、合并、推送和删除工作树仍须由用户单独明确授权。

## 12. 统一诊断决策树

```text
异常发生
  |
  +-- QSPI JEDEC/读写擦/页边界失败？
  |      -> 停止 OTA；复跑厂商例程同一最小操作；对照引脚、AF9、FlashSize、时钟、命令、地址和 WIP。
  |
  +-- QSPI 通过但 Bootloader 不能启动/跳转？
  |      -> 读取 Bootloader/app/state 页；检查向量、VTOR、MSP、状态页分类和内部 Flash 读回哈希。
  |
  +-- Bootloader 通过但传输/安装失败？
  |      -> 保留 TERP 原始帧；先验证候选/恢复包 SHA，再检查 chunk offset/CRC 与 PENDING 写入时机。
  |
  +-- 安装后 trial/回滚异常？
  |      -> 从最后一个 checkpoint 重跑；检查状态代次、trial 计数、确认时点和 recovery 包哈希。
  |
  +-- 模型异常？
         -> 不修改 Bootloader；先校验 Phase 10 manifest/特征契约和黄金输入，再检查 A/B 激活状态。
```

## 13. 自审查

- 原始 `11_Bootloader固件OTA与模型OTA.md` 的向量表、状态页、错误包、下载续传、断电、trial、回滚和模型 A/B 要求均映射到第 4 至第 10 节。
- 对已知 P0（空白/损坏状态页混淆）、P1（主机/Bootloader 版本规则不一致）和缺失的 app-side TERP 闭环均有先行修复任务；未修复前不开始破坏性实板测试。
- 本计划没有使用厂商 W25Q256 驱动的容量、4-byte address 或时钟参数；它仅在低层异常时用作已验证的引脚/HAL 对照。
- 所有擦写与断电步骤都指定了可恢复基线、唯一的 QSPI scratch 范围、原始日志和停止条件；模拟或构建通过不作为板级验收替代物。
