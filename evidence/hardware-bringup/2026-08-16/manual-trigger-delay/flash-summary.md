# 延迟触发固件烧录记录

- 烧录时间：2026-08-16
- ST-Link SN：DEVICE_SERIAL_REDACTED__
- ST-Link 固件：V2J46S7
- 目标：STM32H743，Device ID 0x450，目标电压 3.23 V
- 写入地址：0x08020000
- 镜像：transport_recorder.bin，SHA-256 F9593464CCC6EB59767B32A50DA7203BFCC50BDFE378BADC4BD5623ABFFCF432
- 结果：Download verified successfully，软件复位成功

## 烧录后只读复核

- TERP info：firmware_version=phase08-terp-uart3，hardware_version=openmv4-h743-pd8-pd9
- TERP health：storage_ready=true，state=2，storage_error_count=0，event_export_error_count=0
- 事件列表仍为 41 条，首 ID=1，末 ID=41；未执行 NOR 擦除

因此原有 event 1–15 静止记录及其他原始记录仍在设备中。
