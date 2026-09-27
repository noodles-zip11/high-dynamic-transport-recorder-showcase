# Pilot v1 AI hardware smoke validation

日期：2026-08-17
工作树：`feature/tinyml-pipeline`
固件：`phase08-terp-uart3`，STM32H743 + ICM45686，1600 Hz，EV03/2400 点
串口：UART1（FinSH）与 UART3（TERP），USB-TTL 端口号随重新接线变化

## 结论

本轮完成 Pilot v1 的板级闭环冒烟验证：静止事件被判为
`background`（class 0），明显敲击事件被判为 `impact`（class 1）。这不是
真实箱内运输泛化验收；样本仍是受控台架模拟，后续真实箱内数据应作为 v2
数据/模型迭代。

## 设备输出

UART1 closeout 日志显示：

```text
log state=1 events=5 next_id=6 next_offset=0x00034000 jedec=ef4017
ai model_ready=1 submitted=2 processed=2 queue_drop=0 feature_err=0 runtime_err=0
last_event=5 class=1 confidence_permille=999
```

`class=0` 为 `background`，`class=1` 为 `impact`，类别顺序来自
`ai/artifacts/pilot-v1/model/manifest.json`。两条本轮新增事件均完成提交和处理，
没有队列丢弃、特征错误或运行时错误。

## 原始事件核验

事件文件保留在本机临时目录，未把原始私有数据复制进仓库。每条记录均为
38560 bytes、完整 EV03、2400 点、1600 Hz、无丢样、CRC 通过。

| event | 角色 | 加速度峰值 counts | 陀螺峰值 counts | SHA-256 |
|---:|---|---:|---:|---|
| 1 | background/static | 2074.50 | 14.32 | `5C029C36C58CC3D45711D7F3730B9A85EC4F49825FBDECE3CAA49AFC0305BD62` |
| 2 | background/static | 2073.58 | 13.64 | `B4466BB906A15CC32C50387EB9C374F5E88560E18135CE3198B680654B4299AF` |
| 3 | background/static check | 2093.32 | 14.32 | `962BBD7E9581F96F29C034B878F5409FCB149E6DB5D8B984B9E3B6AABA1F51C8` |
| 4 | controlled impact | 11943.90 | 983.03 | `9FA1A21C4DC2D9E2E69D3BF1590AA67BF4C27BFC84DB0A6339C61EB89ACB02D1` |
| 5 | controlled impact | 17642.27 | 6882.80 | `7B621311B92A965C48C96EBA21854EDB60CADF75B30A7649B89D128E07072EA7` |

## 原始串口日志

- `logs/uart1-format.log`：整片 U2 格式化确认过程；随后 `events=0` 已验证。
- `logs/uart1-static-first.log`：格式化后第一次静止触发；因旧内存 event ID
  与刚格式化日志不同步而被丢弃，随后固件自恢复。
- `logs/uart1-static-committed.log`：静止事件 1 正式写入，AI 判为 background。
- `logs/uart1-ai-closeout.txt`：最终 `model_ready/processed/class/confidence` 输出。

日志 SHA-256：

- `uart1-format.log`：`848DE65AA566B9E68372534E810C84276690FA1567F5EB27DC0A7E51CCEDA75D`
- `uart1-static-first.log`：`C4118822D5856B5B80B99D67AECC30EF54BCC8A249AFDE02739DC20A3870BA58`
- `uart1-static-committed.log`：`7FC89DAF1F14E894E9F5F22057164803F912F64D5EAE8771B492511E2E67054A`
- `uart1-ai-closeout.txt`：`4AE02631A19B2EF6157241D495C308C34AFF7C6610B6B3E8430D571746F6F8B4`

该首条丢弃路径已在代码中增加“每次新触发前从日志 sink 同步 next_event_id”的
回归修复，避免格式化后的第一条事件再次丢失。

## 保留风险

- 数据仍是黑匣子台架模拟，不等价于装入真实箱体后的搬运/振动/碰撞分布。
- 当前 Pilot 只覆盖 `background` 与 `impact`；`drop`、持续振动和复杂混合动作
  不属于本轮模型结论。
- 需要更多独立 session、不同日期/安装条件和真实箱内背景时长，才能做正式泛化、
  误报率和漏报率验收。
