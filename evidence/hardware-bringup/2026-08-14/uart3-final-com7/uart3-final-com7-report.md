# UART3/TERP COM7 最终只读复测报告

用户将 CH340 接到 UART3，端口 COM7。所有命令使用 115200 8N1；本轮未写 MCU Flash、U2/U3 或 Bootloader/OTA。

## 结果

- `ports --json`：退出码 0，识别 `USB-SERIAL CH340 (COM7)`。
- `info --port COM7 --json`：退出码 0，固件 `phase08-terp-uart3`，硬件 `openmv4-h743-pd8-pd9`。
- `health --port COM7 --json`：退出码 0，`storage_ready=true`、`event_export_error_count=0`、`storage_error_count=0`；U2 已满（`state=2`、`free_log_bytes=0`）符合 100 条测试后的状态。
- `events list --port COM7 --json`：退出码 0，返回前 16 条列表项及 CRC；协议分页上限不是失败。
- `events download --id 1`：退出码 0，38528 字节，SHA-256 `5564B34E76C7592263E8A4CA2F479756F63E8D62C422EDCFAC12FBDA9100D063`。
- `events download --id 100`：退出码 0，38528 字节，SHA-256 `545330203D0956AAF1723A439DACE18DC131CA1DC5F6614CA728911428AD049A`。

原始日志与文件见本目录的 `*-console-2.log`、`*-exit-2.txt`、`event-1-2.bin`、`event-100.bin`。

## 边界

本轮验证的是当前物理连接下的 TERP 功能与下载；未进行用户必须参与的真实 USB-TTL 拔插/重连矩阵，因此共享表中的物理矩阵项仍不能标绿。SHT40 30 分钟计数已有独立 SWD 通过证据，本轮按用户要求不重复长跑 UART3 诊断流。
