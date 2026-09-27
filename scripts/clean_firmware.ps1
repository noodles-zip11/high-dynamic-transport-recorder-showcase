$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

. (Join-Path $PSScriptRoot 'project_env.ps1')

$firmwareDir = Join-Path $ProjectRoot 'firmware'
& $SConsExe -C $firmwareDir -c -Q

foreach ($artifact in @(
    (Join-Path $firmwareDir 'build\transport_recorder.bin'),
    (Join-Path $firmwareDir 'build\transport_recorder.map'),
    (Join-Path $firmwareDir 'compile_commands.json')
)) {
    if (Test-Path -LiteralPath $artifact) {
        Remove-Item -LiteralPath $artifact -Force
    }
}

Write-Output 'Firmware build outputs cleaned: PASS'
