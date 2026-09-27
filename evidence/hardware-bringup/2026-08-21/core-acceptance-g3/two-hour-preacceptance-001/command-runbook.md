# 命令准备表

所有命令均在独立工作树
`LOCAL_USER_HOME\Desktop\高动态运输事件记录器-worktrees\hardware-2h-run-20260821`
执行。实际端口必须以开始时的枚举结果替换，不得盲用旧 COM 号。

## 无硬件操作的准备

```powershell
$env:TRANSPORT_VENV_ROOT = 'LOCAL_USER_HOME\Desktop\高动态运输事件记录器\.venv'
pwsh scripts/selfcheck.ps1 -Mode Full
& "$env:TRANSPORT_VENV_ROOT\Scripts\python.exe" -m host.transport_recorder.cli ports --json
```

## UART3 只读读回

```powershell
$out = 'evidence/hardware-bringup/2026-08-21/core-acceptance-g3/two-hour-preacceptance-001/uart3/baseline'
& "$env:TRANSPORT_VENV_ROOT\Scripts\python.exe" scripts/hardware_acceptance_readback.py `
    --port COM7 --output-dir $out --label baseline
```

若要指定已存在事件，只有在本轮 `events list` 观察到事件 ID 后才增加
`--event-id <OBSERVED_ID> --require-ai-result`；不硬编码历史 ID。

## UART1 FinSH 命令顺序

在 UART1 原始日志开始记录、提示符出现后逐条发送并保存完整回显：

```text
sysinfo
ps
event status
log status
log inspect
```

安全事件回路只在 `log status` 正常且未显示需要格式化时执行：

```text
event trigger_test
event export
log list
log verify EVENT_ID_FROM_THIS_LIST
```

## 安全边界

本表不执行 `log format --confirm`、`board_flash_diagnostic=1`、U2 擦除、写中断、物理掉电或
USB-TTL 拔插压力。所有命令的 stdout、stderr、退出码和原始串口字节都要进入本轮证据目录。
