param(
    [Parameter(Mandatory = $false)]
    [string]$GitRevision,
    [Parameter(Mandatory = $true)]
    [string]$DeviceSerial,
    [string]$ReliabilityEvidenceManifest,
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

. (Join-Path $PSScriptRoot 'project_env.ps1')
$projectRoot = $ProjectRoot
Push-Location $projectRoot
try {
    $headRevision = (git rev-parse --verify HEAD).Trim().ToLowerInvariant()
    if ($headRevision -notmatch '^[0-9a-f]{40}$') {
        throw "当前 HEAD 不是完整 40 位 Git SHA：$headRevision"
    }
    if ($GitRevision -and $GitRevision.ToLowerInvariant() -ne $headRevision) {
        throw "GitRevision 必须等于当前 HEAD：expected=$headRevision actual=$GitRevision"
    }
    $resolvedGitRevision = $headRevision

    if ($ReliabilityEvidenceManifest -and -not [System.IO.Path]::IsPathFullyQualified(
            $ReliabilityEvidenceManifest
        )) {
        throw 'ReliabilityEvidenceManifest must be an absolute path'
    }

    if (-not $AllowDirty) {
        $status = @(git status --porcelain)
        if ($status.Count -ne 0) {
            throw 'Release gate requires a clean working tree; use -AllowDirty only for local diagnostics'
        }
    }

    $testArguments = @{
        WithFirmware = $true
        FirmwareProfile = 'Release'
        FirmwareRevision = $resolvedGitRevision
        FirmwareSerial = $DeviceSerial
    }
    if ($ReliabilityEvidenceManifest) {
        $testArguments['ReliabilityEvidence'] = $true
        $testArguments['ReliabilityEvidenceManifest'] = $ReliabilityEvidenceManifest
    }
    & (Join-Path $PSScriptRoot 'run_tests.ps1') @testArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Release gate test suite failed with exit code: $LASTEXITCODE"
    }

    $elf = Join-Path $projectRoot 'firmware\build\transport_recorder.elf'
    $bin = Join-Path $projectRoot 'firmware\build\transport_recorder.bin'
    if (-not (Test-Path -LiteralPath $elf) -or -not (Test-Path -LiteralPath $bin)) {
        throw 'Release gate artifacts are missing after the firmware build'
    }
    $artifactStrings = & $ArmStringsExe $elf
    if (-not ($artifactStrings -contains $resolvedGitRevision)) {
        throw "ELF does not contain the current HEAD revision: $resolvedGitRevision"
    }
    if (-not ($artifactStrings -contains $DeviceSerial)) {
        throw "ELF does not contain the requested device serial: $DeviceSerial"
    }
    & (Join-Path $PSScriptRoot 'check_fault_injection_absent.ps1') `
        -ElfPath $elf `
        -MapPath (Join-Path $projectRoot 'firmware\build\transport_recorder.map') `
        -CompileCommandsPath (Join-Path $projectRoot 'firmware\compile_commands.json')
    if ($LASTEXITCODE -ne 0) {
        throw 'Release gate FaultInjection absence check failed'
    }
    $elfSha256 = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash.ToLowerInvariant()
    $binSha256 = (Get-FileHash -LiteralPath $bin -Algorithm SHA256).Hash.ToLowerInvariant()

    git diff --check
    if ($LASTEXITCODE -ne 0) {
        throw 'Release gate diff check failed'
    }
    if ($AllowDirty) {
        Write-Output "Release gate: DIAGNOSTIC ONLY / NOT RC PASS (revision=$resolvedGitRevision serial=$DeviceSerial)"
    }
    else {
        $finalStatus = @(git status --porcelain)
        if ($finalStatus.Count -ne 0) {
            throw 'Release gate changed the working tree during verification'
        }
        Write-Output "Release gate: PASS (revision=$resolvedGitRevision serial=$DeviceSerial)"
    }
    Write-Output "Release artifact SHA256: elf=$elfSha256 bin=$binSha256"
}
finally {
    Pop-Location
}
