# Windows开发环境与工程骨架 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在Windows上建立以CLion为主要编辑和调试界面、以SCons脚本为唯一真实构建入口的可复现开发环境。

**Architecture:** SCons生成真实固件和`compile_commands.json`，CLion通过编译数据库完成索引并通过Custom Build Target调用同一组PowerShell脚本。Python使用项目虚拟环境；硬件延后期间只配置编译和本机测试，板卡到货后再配置OpenOCD或Embedded GDB Server。

**Tech Stack:** CLion、PowerShell 7、Git、Python 3.12、venv、SCons 4.x、GNU Arm Embedded Toolchain、RT-Thread Standard、STM32CubeProgrammer、后期OpenOCD或ST-LINK GDB Server。

---

## 0. 开发前Worktree门禁

在安装项目依赖、创建脚本或修改工程骨架前，先阅读仓库根目录的[AGENTS.md](../../../../../../AGENTS.md)，并创建本阶段专用工作树：

```powershell
git rev-parse --is-inside-work-tree
git status --short
git worktree add ..\高动态运输事件记录器-worktrees\dev-environment -b feature/dev-environment main
Set-Location ..\高动态运输事件记录器-worktrees\dev-environment
git branch --show-current
git status --short
```

只有第一条输出`true`、当前分支为`feature/dev-environment`且两次状态检查均无输出时，才允许开发。若Git无效、`main`不存在、目录或分支已存在，立即停止并先运行`git worktree list`排查；不得靠删除、硬重置或切回`main`绕过。所有脚本与CLion配置验证都在该工作树完成，提交、推送和PR仍需显式授权。

## 1. 目录原则

项目根目录最终包含：

```text
firmware/       # MCU应用与目标端测试
host/           # CLI和GUI
  ai/             # 数据集、训练和导出
hardware/       # 原理图、引脚和BOM证据
scripts/        # 一条命令入口
docs/           # 设计、计划、报告和学习记录
```

不要把构建输出、Python虚拟环境和下载的第三方SDK提交到项目文件中；它们分别放入`build/`、`.venv/`和外部工具目录。

## 2. 安装顺序

- [ ] 安装CLion并首次启动，确认能打开当前项目目录。
- [ ] 安装PowerShell 7，并确认`pwsh --version`可运行。
- [ ] 安装Git，并确认`git --version`可运行。
- [ ] 安装Python 3.12 x64，并勾选或手动配置PATH。
- [ ] 安装GNU Arm Embedded Toolchain，确认`arm-none-eabi-gcc --version`。
- [ ] 安装STM32CubeProgrammer并把CLI目录加入用户PATH。
- [ ] 安装RT-Thread Env作为配置辅助工具；RT-Thread Studio保持可选，不作为主IDE。

工具小版本不写“永远使用最新版”。安装当天把完整版本输出保存到`docs/test-reports/environment/versions.txt`。

## 3. 创建Python虚拟环境

- [ ] **Step 1: 创建环境**

```powershell
py -3.12 -m venv .venv
```

- [ ] **Step 2: 激活**

```powershell
.\.venv\Scripts\Activate.ps1
```

- [ ] **Step 3: 安装构建和测试工具**

```powershell
python -m pip install --upgrade pip
python -m pip install "scons>=4.8,<5" "pytest>=8,<9"
```

- [ ] **Step 4: 记录解析后的版本**

```powershell
python -m pip freeze | Set-Content requirements-dev-lock.txt
```

Expected：新PowerShell激活`.venv`后，`scons --version`与`pytest --version`均成功。

## 4. 手敲环境自检脚本

**Files:**
- Create: `scripts/selfcheck.ps1`

```powershell
param(
    [ValidateSet('HostOnly', 'Full')]
    [string]$Mode = 'HostOnly'
)

$ErrorActionPreference = 'Stop'

$commands = @(
    'git',
    'python',
    'scons',
    'arm-none-eabi-gcc',
    'arm-none-eabi-size'
)

if ($Mode -eq 'Full') {
    $commands += 'STM32_Programmer_CLI.exe'
}

foreach ($command in $commands) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Missing command: $command"
    }
}

$pythonVersion = python -c "import sys; print('.'.join(map(str, sys.version_info[:3])))"
if (-not $pythonVersion.StartsWith('3.12.')) {
    throw "Python 3.12 required, found $pythonVersion"
}

git --version
python --version
scons --version
arm-none-eabi-gcc --version | Select-Object -First 1

if ($Mode -eq 'Full') {
    STM32_Programmer_CLI.exe --version
}
```

- [ ] 故意临时移除一个工具PATH，确认脚本以非零状态指出缺少命令。
- [ ] 恢复PATH，重新运行并确认退出0。
- [ ] 硬件延后期间运行`pwsh scripts/selfcheck.ps1 -Mode HostOnly`。
- [ ] 准备下单前安装CubeProgrammer，并运行`pwsh scripts/selfcheck.ps1 -Mode Full`。

## 5. 创建固件构建入口

**Files:**
- Create: `scripts/build_firmware.ps1`
- Create: `scripts/clean_firmware.ps1`
- Create: `firmware/SConstruct`

`build_firmware.ps1`：

```powershell
$ErrorActionPreference = 'Stop'
pwsh -File "$PSScriptRoot\selfcheck.ps1"
scons -C "$PSScriptRoot\..\firmware" -j4

$elf = "$PSScriptRoot\..\firmware\build\transport_recorder.elf"
if (-not (Test-Path $elf)) {
    throw "ELF not generated: $elf"
}

arm-none-eabi-size $elf
```

`clean_firmware.ps1`：

```powershell
$ErrorActionPreference = 'Stop'
scons -C "$PSScriptRoot\..\firmware" -c
```

第一版`SConstruct`只负责调用已适配的BSP和输出`build/transport_recorder.elf`。不要在SCons文件里自动下载工具链或修改系统PATH。

## 6. 创建统一测试入口

**Files:**
- Create: `scripts/run_tests.ps1`

```powershell
$ErrorActionPreference = 'Stop'
pwsh -File "$PSScriptRoot\selfcheck.ps1" -Mode HostOnly
scons -C "$PSScriptRoot\..\firmware\tests\native" -j4
python -m pytest "$PSScriptRoot\..\host\tests" -q
python -m pytest "$PSScriptRoot\..\ai\tests" -q
```

在host和ml尚未建立测试时，先创建一个明确的冒烟测试，不允许pytest因为“没有收集到测试”而被误认为通过。

```python
def test_environment_smoke() -> None:
    assert 2 + 2 == 4
```

## 7. 让SCons生成CLion编译数据库

CLion可以从`compile_commands.json`获得每个C文件的编译器、宏、头文件路径和参数，但该文件本身不负责完整构建。因此仍由SCons构建，CLion只消费数据库。

**Files:**

- Modify: `firmware/SConstruct`
- Create: `scripts/generate_compdb.ps1`

在`SConstruct`已经创建`program`目标、所有C/汇编源都加入环境之后添加：

```python
env.Tool("compilation_db")
compilation_db = env.CompilationDatabase("compile_commands.json")
Default(program, compilation_db)
```

`generate_compdb.ps1`：

```powershell
$ErrorActionPreference = 'Stop'

$firmwareDir = Join-Path $PSScriptRoot '..\firmware'
scons -C $firmwareDir compile_commands.json

$database = Join-Path $firmwareDir 'compile_commands.json'
if (-not (Test-Path -LiteralPath $database)) {
    throw "Compilation database not generated: $database"
}

Get-Item -LiteralPath $database | Select-Object FullName, Length, LastWriteTime
```

- [ ] 运行`pwsh scripts/generate_compdb.ps1`。
- [ ] 打开JSON，确认至少一个项目`.c`文件使用`arm-none-eabi-gcc`。
- [ ] 确认命令包含实际使用的`-I`、`-D`、`-mcpu=cortex-m7`和浮点ABI参数。
- [ ] 更改一个SCons宏后重新生成，确认数据库内容随真实构建配置变化。

不要为了让CLion变绿而另写一套脱离SCons的`CMakeLists.txt`，否则IDE索引配置可能与真实固件不同。

## 8. 在CLion中打开项目

手动配置一次：

1. 运行`pwsh scripts/generate_compdb.ps1`；
2. 在CLion选择`File | Open`，打开`firmware/compile_commands.json`并选择`Open as Project`；
3. 使用`Tools | Compilation Database | Change Project Root`，把根目录改为整个项目根目录；
4. 进入`Settings | Build, Execution, Deployment | Toolchains`，建立名为`ARM-GCC`的System工具链；
5. C编译器选择`arm-none-eabi-gcc.exe`，C++编译器选择同一工具链中的`arm-none-eabi-g++.exe`；
6. 进入`Settings | Build, Execution, Deployment | Compilation Database`，选择`ARM-GCC`；
7. 在Build Tools设置中选择数据库变化后自动重新加载，或手动执行Reload Compilation Database Project；
8. 打开一个项目源文件，检查RT-Thread、STM32 HAL和项目头文件能跳转。

若CLion对ARM编译器探测不完整，先检查`compile_commands.json`中的编译器绝对路径和参数，不要通过删除`-mcpu`、`-mfloat-abi`等真实参数来消除索引提示。

## 9. 配置CLion Custom Build Target

进入`Settings | Build, Execution, Deployment | Custom Build Targets`，新建`Firmware-SCons`。

Build工具字段：

```text
Program: pwsh.exe
Arguments: -NoProfile -File "$ProjectFileDir$\scripts\build_firmware.ps1"
Working directory: $ProjectFileDir$
```

Clean工具字段：

```text
Program: pwsh.exe
Arguments: -NoProfile -File "$ProjectFileDir$\scripts\clean_firmware.ps1"
Working directory: $ProjectFileDir$
```

- [ ] 在CLion按`Ctrl+F9`，确认调用的是PowerShell脚本而不是CMake。
- [ ] 故意制造一个C语法错误，确认Build失败且能跳到对应行。
- [ ] 修复错误并重新Build，确认生成ELF和size输出。
- [ ] 执行Clean，确认构建产物删除但源文件、虚拟环境和下载工具未被删除。

本机测试可再建立`Native-Tests`目标，Build工具调用：

```text
pwsh.exe -NoProfile -File "$ProjectFileDir$\scripts\run_tests.ps1"
```

## 10. 硬件到货后再配置CLion调试

现在不创建一个无法验证的调试配置。硬件验收通过后，根据实际调试器二选一：

- OpenOCD可稳定识别板卡：使用`OpenOCD Download & Run`；
- 使用ST-LINK GDB Server、st-util或其他GDB Server：使用`Embedded GDB Server`。

届时手动确认：

- GDB客户端使用ARM兼容GDB或CLion支持的bundled GDB；
- Executable指向`firmware/build/transport_recorder.elf`；
- Before launch绑定`Firmware-SCons`；
- 下载地址来自ELF和链接脚本，不在CLion里另填一个冲突地址；
- reset方式、SWD频率和芯片型号以实板验证；
- 能断在`main`、查看寄存器、内存、反汇编和RT-Thread线程。

## 11. CLion、CubeMX和命令行的分工

CLion负责：编辑、跳转、重构、静态提示、调用脚本和后期GDB调试。

SCons/PowerShell负责：真实构建、测试、清理、环境检查和可复现证据。

CubeMX只用于查看时钟树和生成板级初始化初稿。生成后必须在版本差异中检查PLL、GPIO、DMA、NVIC和Cache设置。

RT-Thread Env用于menuconfig和软件包配置；不要求日常打开RT-Thread Studio。

可以手动操作一次RT-Thread参考BSP：

若需要熟悉RT-Thread的配置层，可以临时打开一次RT-Thread Studio或官方参考工程做对照：

- 新建或导入H743参考BSP；
- 打开RT-Thread配置；
- 查看UART、SPI、USB和SFUD组件位置；
- 导出或观察它修改了哪些配置文件；
- 回到命令行重新构建，确认IDE没有隐藏必要步骤。

CubeMX只用于查看时钟树和生成板级初始化初稿。生成后必须在版本差异中检查PLL、GPIO、DMA、NVIC和Cache设置。

## 12. 新终端和CLion复现测试

- [ ] 关闭当前PowerShell。
- [ ] 打开新的PowerShell窗口。
- [ ] 进入项目目录。
- [ ] 激活`.venv`。
- [ ] 运行`scripts/selfcheck.ps1`。
- [ ] 运行`scripts/run_tests.ps1`。
- [ ] 运行`scripts/build_firmware.ps1`。
- [ ] 在CLion内置Terminal重复运行selfcheck和测试。
- [ ] 在CLion执行`Firmware-SCons`，比较命令与外部PowerShell一致。

Expected：外部PowerShell和CLion调用同一脚本并得到一致结果；命令行不依赖CLion已打开，也不依赖临时环境变量。

## 13. 常见错误

- PowerShell禁止激活脚本：只为当前用户设置合理ExecutionPolicy，不关闭整个系统安全策略。
- 同时存在多个Python：使用`py -0p`列出，并在虚拟环境中确认`Get-Command python`。
- SCons找到错误GCC：打印`Get-Command arm-none-eabi-gcc`的完整路径。
- CLion索引正常但Build失败：编译数据库只负责索引，检查Custom Build Target是否真正调用SCons脚本。
- CLion能构建、外部PowerShell不能：检查CLion工具链环境是否隐藏注入了PATH，修复系统或脚本配置。
- CLion全是红色头文件：重新生成数据库，检查编译器路径、`-I`和`-D`，再Reload Compilation Database。
- 路径含中文导致旧工具失败：把第三方工具装到短英文路径，但项目文件仍保留在当前工作区，并记录具体失败工具。

## 14. 验收门槛

- [ ] 新PowerShell能运行selfcheck、测试和构建入口。
- [ ] 缺工具时脚本可靠失败，不静默跳过。
- [ ] Python依赖锁定文件已生成。
- [ ] 工具版本输出已保存。
- [ ] CLion可正确跳转项目、RT-Thread和STM32头文件。
- [ ] CLion Build与外部PowerShell调用同一SCons脚本。
- [ ] 能解释CLion、编译数据库与SCons各自负责什么。
- [ ] 未购买硬件时，调试配置明确保持未实测，不写成已完成。

## 15. 学习检查

1. 为什么不能只保存一个IDE工程截图作为可复现证据？
2. 虚拟环境解决了什么问题，不能解决什么问题？
3. 为什么构建脚本必须检查ELF确实存在？
4. 如果同一电脑有两个GCC，怎样证明实际调用了哪个？
5. 为什么`compile_commands.json`能让CLion正确索引，却不能单独完成整个工程构建？
6. 为什么不应该维护一套只为CLion服务、与SCons参数不同的CMake工程？
