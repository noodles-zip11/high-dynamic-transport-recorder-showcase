# STM32 构建链速查

```text
.c + .h
  │  arm-none-eabi-gcc -c
  ▼
.o  （单个目标文件，尚未确定最终地址）
  │  linker + startup + libraries + link.lds
  ▼
.elf （完整可调试固件，含符号和地址）
  │  arm-none-eabi-objcopy -O binary
  ▼
.bin （烧录用原始字节）
  │  OpenOCD / STM32CubeProgrammer
  ▼
MCU Flash
```

## 工具边界

| 工具 | 作用 | 是否编译代码 |
|---|---|---:|
| GNU Arm GCC | C 源文件编译、汇编、链接 | 是 |
| SCons | 按 SConstruct/SConscript 组织上述命令 | 否 |
| RT-Thread Env | 准备命令行环境、`pkgs`、`menuconfig`、SCons 入口 | 否 |
| `pkgs` | 下载 CMSIS/HAL 等依赖包 | 否 |
| OpenOCD/Programmer | 把 ELF/BIN 写入 MCU | 否 |

## 本项目当前状态

- `firmware/tests/native`：本机测试，输出 `.exe`。
- `firmware/SConstruct`：目前只验证 ARM GCC 能把 `app/main.c` 编成 `.o`。
- `RTT_ROOT`：指向外部 RT-Thread 源码树，类似一个 SDK 依赖路径。
- `stm32h743-atk-apollo`：已经成功构建过的 RT-Thread 参考 BSP，不是最终项目固件。
- `firmware/bsp/transport_recorder`：自己的 BSP 适配层，目前只有空骨架。
