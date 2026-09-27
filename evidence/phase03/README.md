# Phase 03 hardware evidence

`build.log` 和 `build-incremental.log` 是主机侧构建证据，不是硬件验收证据。没有
ST-Link 或目标板连接，因此尚未记录硬件证据，也不能把 BSP 参考引脚、时钟和下载地址
视为实物已确认。

When the board is connected, run:

```powershell
pwsh scripts/program_firmware.ps1 -List
pwsh scripts/program_firmware.ps1 -Program
```

Capture the serial console output to `boot-smoke.log` while performing the ten cold boots, ten reset-button tests, LED timing measurement and two-hour run required by Phase 03.
