param(
    [switch]$List,
    [switch]$Program,
    [string]$TerpPort,
    [double]$TerpReadyTimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

. (Join-Path $PSScriptRoot 'project_env.ps1')
. (Join-Path $PSScriptRoot 'cubeprogrammer_paths.ps1')

if (-not (Test-Path -LiteralPath $ProgrammerCli)) {
    throw "STM32CubeProgrammer not found: $ProgrammerCli"
}

if ($List) {
    & $ProgrammerCli -l
    exit $LASTEXITCODE
}

if (-not $Program) {
    throw 'Refusing to write hardware without -Program. Use -List to inspect connected probes.'
}

& (Join-Path $PSScriptRoot 'build_firmware.ps1') -RequireElf

$image = Join-Path $ProjectRoot 'firmware\build\transport_recorder.bin'
$stagedImage = New-CubeProgrammerArtifact -SourcePath $image
$stagingDirectory = Split-Path -Parent $stagedImage
$programLog = Join-Path $stagingDirectory 'program.log'

Write-Output "CubeProgrammer artifact: $stagedImage"
Write-Output "CubeProgrammer log: $programLog"
& $ProgrammerCli -c port=SWD -w $stagedImage 0x08020000 -v -rst -log $programLog

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Output 'SWD program and verify: PASS'

if ([string]::IsNullOrWhiteSpace($TerpPort)) {
    Write-Output 'TERP readiness: SKIP (pass -TerpPort to verify the application protocol)'
    exit 0
}
if ($TerpReadyTimeoutSeconds -le 0) {
    throw 'TerpReadyTimeoutSeconds must be positive.'
}
if (-not (Test-Path -LiteralPath $PythonExe)) {
    throw "Python runtime not found for TERP readiness check: $PythonExe"
}

Write-Output ("Waiting for TERP readiness on {0} (timeout {1}s)..." -f
    $TerpPort, $TerpReadyTimeoutSeconds)
& $PythonExe -m host.transport_recorder.cli info --port $TerpPort `
    --startup-timeout $TerpReadyTimeoutSeconds
if ($LASTEXITCODE -ne 0) {
    throw "TERP readiness check failed on $TerpPort."
}
Write-Output 'TERP readiness: PASS'
