# 0002 SCons、RT-Thread 与 CMake 面试要点

## 一分钟表述

本项目是 STM32H743 + RT-Thread 固件工程。CLion 只负责编辑、代码索引和展示
构建结果；正式构建由 PowerShell 脚本统一入口，再调用 SCons 和
`arm-none-eabi-gcc` 生成 ELF、BIN、MAP。之所以没有为了 IDE 体验改成 CMake，
是因为 RT-Thread 的标准 BSP、Kconfig 和 Env 生态以 SCons 为主。通过生成
`compile_commands.json`，CLion 仍可获得准确的跳转、补全和宏解析能力。

## 当前项目的构建链

```text
CLion 的 Firmware Build 按钮
        -> scripts/build_firmware.ps1
        -> scripts/selfcheck.ps1 + project_env.ps1
        -> SConstruct / SConscript
        -> arm-none-eabi-gcc 编译、链接
        -> transport_recorder.elf
        -> objcopy 导出 transport_recorder.bin
        -> MAP 与内存布局检查
        -> ST-Link 烧录和串口硬件验证（上板阶段）
```

构建成功不等于硬件通过：构建只证明源码、配置、链接和内存布局正确；下载、启动、
时钟、LED、串口和长期稳定性仍必须在实物板上验证。

## 文件职责

| 文件或目录 | 职责 |
|---|---|
| `scripts/build_firmware.ps1` | 唯一正式构建入口：自检、构建、产物和内存检查。 |
| `firmware/SConstruct` | 顶层构建定义，组织 RT-Thread、BSP、应用和链接目标。 |
| `firmware/**/SConscript` | 子目录构建定义，声明各模块源码。 |
| `firmware/rtconfig.py` | ARM GCC、Cortex-M7 编译参数、链接参数和工具链前缀。 |
| `firmware/Kconfig`、`.config`、`rtconfig.h` | 选择 RT-Thread 组件和生成配置宏。 |
| `firmware/bsp/weact_h743/` | 板级时钟、启动、GPIO、MPU、链接脚本。 |
| `compile_commands.json` | 供 CLion/clangd 索引使用；不是正式构建定义。 |
| `tools/check_memory_map.ps1` | 校验向量表、Flash、AXI SRAM、D2 DMA 区是否符合策略。 |

## SCons 与 CMake 对照

| 关注点 | SCons / RT-Thread | CMake |
|---|---|---|
| 顶层入口 | `SConstruct` | `CMakeLists.txt` |
| 子目录 | `SConscript` | `add_subdirectory()` + 子目录 `CMakeLists.txt` |
| 目标定义 | `env.Program()`、`env.Object()` | `add_executable()`、`add_library()` |
| 头文件、宏 | `CPPPATH`、`CPPDEFINES` | `target_include_directories()`、`target_compile_definitions()` |
| 编译、链接参数 | `CFLAGS`、`LFLAGS` | `target_compile_options()`、`target_link_options()` |
| 工具链 | `rtconfig.py` | `toolchain.cmake` |
| 功能开关 | Kconfig、`.config`、`rtconfig.h` | `option()`、`set()`、`CMakeCache.txt` |
| 常见调用 | `scons -C firmware` | `cmake -S . -B build` 后 `cmake --build build` |
| 编译数据库 | SCons 生成 `compile_commands.json` | `CMAKE_EXPORT_COMPILE_COMMANDS=ON` |

## 为什么选择 SCons，而不是改成 CMake

1. **遵循上游生态。** RT-Thread 的 BSP、Kconfig 配置流程和 Env 工具天然围绕 SCons。
2. **降低移植风险。** 不重新实现 RT-Thread 内核、HAL、软件包和 Kconfig 的依赖关系。
3. **IDE 与构建解耦。** CLion 不需要成为构建系统；导入编译数据库即可正确索引代码。
4. **保留迁移空间。** 如果后续需要 CMake，可为主机工具或独立模块引入 CMake；不应在
   BSP 启动阶段为了“点按钮”而替换官方构建链。

## 高频面试问答

### 1. SCons 是编译器吗？

不是。SCons 是构建编排器，负责决定编译哪些源文件、传递哪些参数、如何链接；实际编译
器是 `arm-none-eabi-gcc`，链接器也是 GCC 工具链中的 `ld`。

### 2. ELF、BIN、MAP 有什么区别？

- **ELF**：包含段、符号、调试信息等，供调试器和链接分析使用。
- **BIN**：原始二进制烧录镜像，通常由 `objcopy` 从 ELF 导出。
- **MAP**：链接器报告，能检查代码、数据、堆、栈和自定义段的地址与占用。

### 3. `compile_commands.json` 能替代 SCons 吗？

不能。它只描述每个源文件“应如何编译”，主要服务 IDE/静态分析；它不负责完整依赖图、
链接、后处理和产物校验。正式构建仍必须走 SCons。

### 4. 为什么要在构建后检查 MAP？

嵌入式错误常表现为链接成功但内存放置错误。项目通过 MAP 检查确认中断向量在
`0x08000000`、常规 RAM 使用 AXI SRAM、DMA 缓冲位于 32 字节对齐的 D2 SRAM，避免
烧录后才因地址或 Cache/DMA 一致性问题失败。

### 5. 为什么 CLion 的绿色运行按钮不能直接等同于“编译成功”？

按钮只是入口。正确配置时它应调用 `build_firmware.ps1`；如果改用默认 CMake Build，
会绕开 RT-Thread 的 SCons/Kconfig 构建链，结果不具备项目正式构建的可信度。

## 当前仍需实践

- 使用 ST-Link 烧录生成的 BIN，并保存烧录和串口日志。
- 在实物板验证时钟、LED、串口、复位与两小时稳定性。
- 打开 `transport_recorder.map`，手工定位 `.isr_vector`、`.data`、`.bss`、`.dma_buffer`。
- 解释一次从 `main.c` 到 `.o`、ELF、BIN 的完整路径。
