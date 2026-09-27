param(
    [ValidateSet('HostOnly', 'Full')]
    [string]$Mode = 'HostOnly'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

. (Join-Path $PSScriptRoot 'project_env.ps1')

$commands = @(
    'git',
    'pwsh'
)

foreach ($command in $commands) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "找不到命令：$command"
    }
}

$projectTools = @{
    Python = $PythonExe
    SCons = $SConsExe
    ArmGcc = $ArmGccExe
    ArmSize = $ArmSizeExe
    ArmStrings = $ArmStringsExe
    NativeGcc = $NativeGccExe
}

foreach ($tool in $projectTools.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $tool.Value)) {
        throw "找不到$($tool.Key)：$($tool.Value)"
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $RttRoot 'tools\building.py'))) {
    throw "RTT_ROOT 路径无效：$RttRoot"
}

$resolvedArmGcc = (Get-Command 'arm-none-eabi-gcc' -ErrorAction Stop).Source
if (-not [string]::Equals(
    [System.IO.Path]::GetFullPath($resolvedArmGcc),
    [System.IO.Path]::GetFullPath($ArmGccExe),
    [System.StringComparison]::OrdinalIgnoreCase
)) {
    throw "ARM GCC 路径不一致：$resolvedArmGcc"
}

$pythonVersion = & $PythonExe -c "import sys; print('.'.join(map(str, sys.version_info[:3])))"
if (-not $pythonVersion.StartsWith('3.12.')) {
    throw "需要 Python 3.12，当前版本：$pythonVersion"
}

git --version
pwsh --version
Write-Output "Python: $pythonVersion"
& $PythonExe -m pytest --version
& $SConsExe --version | Select-Object -First 2
& $ArmGccExe --version | Select-Object -First 1
& $NativeGccExe --version | Select-Object -First 1
Write-Output "ARM GCC: $ArmGccExe"
Write-Output "Native GCC: $NativeGccExe"
Write-Output "RTT_ROOT: $RttRoot"

if ($Mode -eq 'Full') {
    if (-not (Test-Path -LiteralPath $ProgrammerCli)) {
        throw "找不到STM32CubeProgrammer：$ProgrammerCli"
    }

    & $ProgrammerCli --version
}

Write-Output "Environment self-check ($Mode): PASS"
