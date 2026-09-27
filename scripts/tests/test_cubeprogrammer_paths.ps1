$ErrorActionPreference = 'Stop'

$helper = Join-Path $PSScriptRoot '..\cubeprogrammer_paths.ps1'
. $helper

$sourceRoot = Join-Path $env:TEMP '运输记录器-路径测试'
$sourcePath = Join-Path $sourceRoot '固件.bin'
$stagingRoot = Join-Path $env:TEMP 'transport-recorder-path-test'
$payload = [byte[]](0x00, 0x01, 0x7F, 0x80, 0xFF)

New-Item -ItemType Directory -Force -Path $sourceRoot | Out-Null
[System.IO.File]::WriteAllBytes($sourcePath, $payload)

$stagedPath = New-CubeProgrammerArtifact -SourcePath $sourcePath -StagingRoot $stagingRoot

if (-not (Test-AsciiOnlyPath -Path $stagedPath)) {
    throw "staged path contains non-ASCII characters: $stagedPath"
}
if (-not (Test-Path -LiteralPath $stagedPath)) {
    throw "staged artifact does not exist: $stagedPath"
}
if (-not [System.Linq.Enumerable]::SequenceEqual(
        [System.IO.File]::ReadAllBytes($stagedPath), $payload)) {
    throw 'staged artifact bytes differ from source'
}

Write-Output 'CubeProgrammer ASCII staging: PASS'
