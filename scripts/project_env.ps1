$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$VenvRoot = if ($env:TRANSPORT_VENV_ROOT) {
    [System.IO.Path]::GetFullPath($env:TRANSPORT_VENV_ROOT)
} else {
    Join-Path $ProjectRoot '.venv'
}
$VenvScripts = Join-Path $VenvRoot 'Scripts'
$PythonExe = Join-Path $VenvScripts 'python.exe'
$SConsExe = Join-Path $VenvScripts 'scons.exe'

$RttRoot = if ($env:RTT_ROOT) {
    $env:RTT_ROOT
} else {
    Join-Path $env:USERPROFILE 'Desktop\RT-Thread\rt-thread-standard'
}

$ArmToolchainBin = if ($env:RTT_EXEC_PATH) {
    $env:RTT_EXEC_PATH
} else {
    'C:\ST\STM32CubeCLT_1.18.0\GNU-tools-for-STM32\bin'
}

$ArmGccExe = Join-Path $ArmToolchainBin 'arm-none-eabi-gcc.exe'
$ArmSizeExe = Join-Path $ArmToolchainBin 'arm-none-eabi-size.exe'
$ArmObjdumpExe = Join-Path $ArmToolchainBin 'arm-none-eabi-objdump.exe'
$ArmStringsExe = Join-Path $ArmToolchainBin 'arm-none-eabi-strings.exe'
$NativeToolchainBin = if ($env:NATIVE_TOOLCHAIN_PATH) {
    $env:NATIVE_TOOLCHAIN_PATH
} else {
    'C:\Program Files\JetBrains\CLion 2026.1.3\bin\mingw\bin'
}
$NativeGccExe = Join-Path $NativeToolchainBin 'gcc.exe'
$ProgrammerCli = if ($env:STM32_PROGRAMMER_CLI) {
    $env:STM32_PROGRAMMER_CLI
} else {
    'C:\ST\STM32CubeCLT_1.18.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
}

$env:RTT_ROOT = $RttRoot
$env:RTT_EXEC_PATH = $ArmToolchainBin
$env:NATIVE_TOOLCHAIN_PATH = $NativeToolchainBin

if (-not $env:PROCESSOR_ARCHITECTURE) {
    $env:PROCESSOR_ARCHITECTURE = 'AMD64'
}

$pathEntries = @(
    $VenvScripts,
    $ArmToolchainBin,
    $NativeToolchainBin,
    (Split-Path -Parent $ProgrammerCli)
)

$env:Path = (($pathEntries + ($env:Path -split ';')) |
    Where-Object { $_ } |
    Select-Object -Unique) -join ';'
