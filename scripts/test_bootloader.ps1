$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

. (Join-Path $PSScriptRoot 'project_env.ps1')

$bootloader = Join-Path $ProjectRoot 'bootloader'
& $SConsExe -C (Join-Path $bootloader 'tests\native') -Q
& (Join-Path $bootloader 'tests\native\build\test_bootloader_core.exe')
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $SConsExe -C $bootloader -Q
& $PythonExe (Join-Path $ProjectRoot 'tools\check_bootloader_image.py') `
    --elf (Join-Path $bootloader 'build\bootloader.elf') `
    --map (Join-Path $bootloader 'build\bootloader.map') `
    --objdump $ArmObjdumpExe `
    --nm (Join-Path $ArmToolchainBin 'arm-none-eabi-nm.exe')

Write-Output 'Bootloader tests and image checks: PASS'
