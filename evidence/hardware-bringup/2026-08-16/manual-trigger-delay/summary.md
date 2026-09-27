# 人工采集延迟触发方案

- 固件工作树：feature/tinyml-pipeline
- 应用地址：0x08020000
- BIN：firmware/build/transport_recorder.bin
- BIN SHA-256：F9593464CCC6EB59767B32A50DA7203BFCC50BDFE378BADC4BD5623ABFFCF432
- BIN 大小：177648 bytes

## 行为

UART1/FinSH 新增命令：

```text
event trigger_delay 1000
```

设备从收到命令的时刻等待指定毫秒，在对应的真实采样点开始事件；到点会打印：

```text
event trigger scheduled delay_ms=1000
event trigger delayed start
```

延迟范围为 1–60000 ms。已有的 event trigger_test 保留，但只建议用于诊断。
延迟请求尚未触发时，第二个立即触发请求会被拒绝，避免一个动作生成两个事件。

## 数据安全

本改动不改 TERP 帧、不改事件 EV03 格式、不改引脚、不执行 Flash 擦除。已有
event 1–15 静止数据继续保留；烧录应用镜像只更新 STM32 应用区，不能通过该命令
删除外部 NOR 中的事件记录。

## 验证

- 原生固件测试：Native C tests PASS
- 事件服务测试覆盖：延迟期间不触发、在截止采样点触发、重复触发请求被拒绝
- H743 固件构建：Firmware artifacts PASS；Memory map PASS
