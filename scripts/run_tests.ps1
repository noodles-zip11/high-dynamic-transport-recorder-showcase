param(
    [switch]$WithFirmware,
    [ValidateSet('Debug', 'Release', 'FaultInjection')]
    [string]$FirmwareProfile = 'Debug',
    [string]$FirmwareRevision = 'local',
    [string]$FirmwareSerial = 'pending',
    [switch]$ReliabilityEvidence,
    [string]$ReliabilityEvidenceManifest
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

. (Join-Path $PSScriptRoot 'project_env.ps1')

& (Join-Path $PSScriptRoot 'selfcheck.ps1') -Mode HostOnly
& (Join-Path $PSScriptRoot 'tests\test_cubeprogrammer_paths.ps1')
& $PythonExe (Join-Path $ProjectRoot 'protocol\generate_messages.py') --check
& $PythonExe (Join-Path $ProjectRoot 'protocol\golden\generate.py') --check
& $PythonExe -m pytest (Join-Path $ProjectRoot 'scripts\tests') -q
& (Join-Path $PSScriptRoot 'test_native.ps1')
& (Join-Path $PSScriptRoot 'test_bootloader.ps1')
${previousQtPlatform} = $env:QT_QPA_PLATFORM
try {
    $env:QT_QPA_PLATFORM = 'offscreen'
    & $PythonExe -m pytest (Join-Path $ProjectRoot 'host\tests') -q
}
finally {
    if ($null -eq $previousQtPlatform) {
        Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    }
    else {
        $env:QT_QPA_PLATFORM = $previousQtPlatform
    }
}
& $PythonExe -m pytest (Join-Path $ProjectRoot 'ai\tests') -q

if ($WithFirmware) {
    $firmwareArguments = @{
        BuildProfile = $FirmwareProfile
        GitRevision = $FirmwareRevision
        DeviceSerial = $FirmwareSerial
        ReliabilityEvidence = $ReliabilityEvidence
        RequireElf = $true
    }
    if ($ReliabilityEvidenceManifest) {
        $firmwareArguments['ReliabilityEvidenceManifest'] = $ReliabilityEvidenceManifest
    }
    & (Join-Path $PSScriptRoot 'build_firmware.ps1') @firmwareArguments
}

Write-Output 'All host-side tests: PASS'
