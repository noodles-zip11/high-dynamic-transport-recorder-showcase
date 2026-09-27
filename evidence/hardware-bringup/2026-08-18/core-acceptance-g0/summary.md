# G0 final image baseline — 2026-08-18

状态：**PASS / 10/10 COLD_BOOT**。

## 基线、刷写与软件门禁

- 本轮实板运行镜像对应已合入 `main` 的完整提交：
  `4c47fe69b544ef9327300910d66f04927871f0dc`。
- 合并后 `main` Release 产物：ELF SHA-256
  `AC3B7CFFD5034E8D6E2669DE8D50793ED139985D4403A2D6E4BB9544FC78454E`，
  BIN SHA-256
  `2F0E676507D1591060BA759D8F81C6DE472D302988172A22DEF627365B5E878A`，
  MAP SHA-256
  `ADA8A0DAB124D6B584EED399E067465342327488598C4B44AED225E5F52F3089`；
  BIN 长度 `128952`，应用地址 `0x08020000`。精确产物刷写/校验日志见 G1 的
  `merged-main-reflash/cubeprogrammer-program.log`。
- Native C、Bootloader/image、Host `131 passed`、AI `66 passed` 通过；ARM 链接、向量和内存映射通过。
  统一门禁日志为 [`run-tests-release.log`](run-tests-release.log)，SHA-256 为
  `9DB2D198C6D9FE55032A6B0BEEAD54A3B39258D009EC69A24B590CAB2139F477`，尾部明确记录
  `run_tests_exit_code=0`。
- 通过 ST-LINK `DEVICE_SERIAL_REDACTED__` 读取到 STM32H743VIT6、目标电压 3.23 V、2 MiB 内部 Flash。
- 刷写前完成 2 MiB 内部 Flash 只读备份，SHA-256 为
  `2B9845C8D9F459B271FB06247E50239D8E6ED88377B330EE9D1ADB3C9D502F32`；备份同时保存在
  `LOCAL_USER_HOME/Desktop/hardware_acceptance_g0_20260818/internal-flash-before.bin`，不纳入 Git。
- 仅将应用 BIN 写入 `0x08020000`，CubeProgrammer 下载校验通过；未写 Bootloader，未格式化或主动写入 U2/QSPI。

## UART3 与 10/10 冷启动结果

- USB-TTL 使用 UART3/TERP：COM7、115200 8N1，PD8（板 TX）→ TTL RX、PD9（板 RX）→ TTL TX、共地，3.3 V 逻辑，TTL VCC 未接。
- 10 次均由操作者执行物理断电后重新上电，再通过 UART3 进行 `info`、`health`、事件列表和模型状态只读检查。
- 正式轮次为 `1,2,3,4,5,6,7,8,9,10`；第3轮采用 `round-03-redo.json`，此前两次超时仅作为排障记录，不计入正式轮次。
- 10/10 均匹配：`model=STM32H743`、`firmware_version=phase08-terp-uart3`、
  `hardware_version=openmv4-h743-pd8-pd9`、`serial_number=recorder-001`、能力标志 `235`。
- 10/10 均正常完成 TERP 读回；事件列表稳定为 ID 1～39，末事件 ID 为 39。
- 10/10 均为 `storage_ready=true`、`storage_error_count=0`、`event_export_error_count=0`；
  `health state=2` 是已知 `HEALTH_DEGRADED` 观测，不将其误写成整机 health PASS。
- 10/10 均为 `model_valid=true`、`pending_install=false`、`active_slot=1`；本轮未观察到 HardFault、assert 或意外复位。

机器汇总与完整轮次记录见 [`cold-boot-10-manual/run-summary.json`](cold-boot-10-manual/run-summary.json)、
[`cold-boot-10-manual/rounds.jsonl`](cold-boot-10-manual/rounds.jsonl) 和
[`cold-boot-10-manual/uart3-cold-boot.frames.jsonl`](cold-boot-10-manual/uart3-cold-boot.frames.jsonl)。
逐轮 JSON 记录位于 [`cold-boot-10-manual`](cold-boot-10-manual)。

## 本门禁边界

- G0 仅关闭最终镜像身份、正常冷启动和 UART3 基础能力回归；不关闭 G2 受控掉电矩阵、G3/B-08 长稳合同。
- G1 已有独立记录并通过审查；B-04、B-08 核心子门和 B-09 仍按台账保持 `PENDING_HARDWARE`，直到 G2/G3 合同完成。
- 旧的 `cold-boot-10/`、`cold-boot-10-final/`、`cold-boot-10-formal/` 目录是未计数的自动化排障记录，保留用于审计；正式结论只引用 `cold-boot-10-manual/`。
