$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

. (Join-Path $PSScriptRoot 'project_env.ps1')

$firmwareDir = Join-Path $ProjectRoot 'firmware'
$database = Join-Path $firmwareDir 'compile_commands.json'

& $SConsExe -C $firmwareDir compile_commands.json -Q

if (-not (Test-Path -LiteralPath $database)) {
    throw "编译数据库未生成：$database"
}

$entries = @(Get-Content -LiteralPath $database -Raw | ConvertFrom-Json)
if ($entries.Count -eq 0) {
    throw "编译数据库为空：$database"
}

$mainEntry = $entries | Where-Object { $_.file -match 'main\.c$' } | Select-Object -First 1
if (-not $mainEntry) {
    throw '编译数据库中没有找到app/main.c'
}

$requiredTokens = @(
    'arm-none-eabi-gcc',
    '-mcpu=cortex-m7',
    '-mfpu=fpv5-d16',
    '-mfloat-abi=hard',
    '-DSTM32H743xx',
    '-I',
    'main.c'
)

foreach ($token in $requiredTokens) {
    if ($mainEntry.command -notmatch [regex]::Escape($token)) {
        throw "编译数据库缺少参数：$token"
    }
}

Get-Item -LiteralPath $database | Select-Object FullName, Length, LastWriteTime
Write-Output 'Compilation database: PASS'
