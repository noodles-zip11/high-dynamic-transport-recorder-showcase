param(
    [string]$ElfPath,
    [string]$MapPath,
    [string]$CompileCommandsPath,
    [string]$ObjdumpPath,
    [string]$StringsPath
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

. (Join-Path $PSScriptRoot 'project_env.ps1')

if (-not $ElfPath) {
    $ElfPath = Join-Path $ProjectRoot 'firmware\build\transport_recorder.elf'
}
if (-not $MapPath) {
    $MapPath = Join-Path $ProjectRoot 'firmware\build\transport_recorder.map'
}
if (-not $CompileCommandsPath) {
    $CompileCommandsPath = Join-Path $ProjectRoot 'firmware\compile_commands.json'
}

if (-not $ObjdumpPath) {
    $ObjdumpPath = $ArmObjdumpExe
}
if (-not $StringsPath) {
    $StringsPath = $ArmStringsExe
}

foreach ($path in @($ElfPath, $MapPath, $CompileCommandsPath,
        $ObjdumpPath, $StringsPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "FaultInjection absence input is missing: $path"
    }
}

$forbiddenTokens = @(
    'TRANSPORT_FAULT_INJECTION_ENABLED',
    'fault_injection.c',
    'fault_injection.o',
    'fault_injection',
    'reliability_inject',
    'CONFIRM_RESET',
    'CONFIRM_EVENT',
    'hardfault',
    'memmanage',
    'busfault',
    'usagefault',
    'sequence-gap',
    'pretrigger-short',
    'duration-cap',
    'pool-pressure',
    'queue-pressure'
)

function Assert-NoFaultInjectionToken {
    param(
        [string]$Label,
        [string]$Text
    )

    foreach ($token in $forbiddenTokens) {
        if ($Text.Contains($token)) {
            throw "FaultInjection absence failed: '$token' in $Label"
        }
    }
}

function Invoke-ToolText {
    param(
        [string]$Tool,
        [string[]]$Arguments,
        [string]$Label
    )

    $output = & $Tool @Arguments 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "FaultInjection absence tool failed: $Label"
    }
    return $output
}

$compileCommands = Get-Content -LiteralPath $CompileCommandsPath -Raw
try {
    $null = $compileCommands | ConvertFrom-Json
}
catch {
    throw "FaultInjection absence compile_commands is not valid JSON"
}
Assert-NoFaultInjectionToken 'compile_commands.json' $compileCommands

$mapText = Get-Content -LiteralPath $MapPath -Raw
Assert-NoFaultInjectionToken 'linker map' $mapText

$symbols = Invoke-ToolText $ObjdumpPath @('-t', $ElfPath) 'objdump -t'
$relocations = Invoke-ToolText $ObjdumpPath @('-r', $ElfPath) 'objdump -r'
$disassembly = Invoke-ToolText $ObjdumpPath @('-d', $ElfPath) 'objdump -d'
$strings = Invoke-ToolText $StringsPath @($ElfPath) 'strings'

Assert-NoFaultInjectionToken 'objdump -t' $symbols
Assert-NoFaultInjectionToken 'objdump -r' $relocations
Assert-NoFaultInjectionToken 'objdump -d' $disassembly
Assert-NoFaultInjectionToken 'strings' $strings

Write-Output "FaultInjection absence: PASS ($ElfPath)"
