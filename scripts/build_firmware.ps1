param(
    [ValidateSet('Debug', 'Release', 'FaultInjection')]
    [string]$BuildProfile = 'Debug',
    [string]$GitRevision = 'local',
    [string]$DeviceSerial = 'pending',
    [switch]$RequireElf,
    [switch]$ReliabilityEvidence,
    [switch]$SaveReliabilityOffBaseline,
    [string]$ReliabilityEvidenceManifest,
    [string]$ArtifactOutputDirectory
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

. (Join-Path $PSScriptRoot 'project_env.ps1')

if ($BuildProfile -eq 'Release') {
    if ($GitRevision -notmatch '^[0-9a-f]{40}$') {
        throw 'Release builds require a full 40-character lowercase Git revision'
    }
    if ($DeviceSerial -eq 'pending') {
        throw 'Release builds require an explicit DeviceSerial value'
    }
    if ($ReliabilityEvidence) {
        $actualHead = (git -C $ProjectRoot rev-parse --verify HEAD).Trim().ToLowerInvariant()
        if ($actualHead -notmatch '^[0-9a-f]{40}$') {
            throw 'Release reliability evidence requires a complete Git HEAD'
        }
        if ($GitRevision.ToLowerInvariant() -ne $actualHead) {
            throw 'Release reliability evidence revision must match current HEAD'
        }
        $workingTree = @(git -C $ProjectRoot status --porcelain)
        if ($workingTree.Count -ne 0) {
            throw 'Release reliability evidence requires a clean working tree'
        }
        if (-not $ReliabilityEvidenceManifest) {
            throw 'Release reliability evidence remains locked until Phase 4 manifest validation'
        }
        if (-not [System.IO.Path]::IsPathFullyQualified(
                $ReliabilityEvidenceManifest
            )) {
            throw 'Release reliability evidence manifest must be an existing absolute path'
        }
        $resolvedManifest = [System.IO.Path]::GetFullPath(
            $ReliabilityEvidenceManifest
        )
        $manifestExists = Test-Path -LiteralPath $resolvedManifest
        if (-not $manifestExists) {
            throw 'Release reliability evidence manifest must be an existing absolute path'
        }
        & $PythonExe (Join-Path $ProjectRoot 'scripts\phase4_evidence.py') `
            verify `
            --input (Split-Path -Parent $resolvedManifest) `
            --for-release `
            --source-revision $actualHead
        if ($LASTEXITCODE -ne 0) {
            throw 'Release reliability evidence manifest validation failed'
        }
    }
}

if ($SaveReliabilityOffBaseline -and $ReliabilityEvidence) {
    throw 'SaveReliabilityOffBaseline is only valid for default-off builds'
}
if ($SaveReliabilityOffBaseline -and $BuildProfile -ne 'Debug') {
    throw 'SaveReliabilityOffBaseline is only valid for Debug default-off builds'
}

function Seal-FirmwareArtifacts {
    param(
        [string]$OutputDirectory,
        [string[]]$SourceArtifacts,
        [string]$CompileCommands,
        [string]$Profile,
        [string]$Revision,
        [string]$Serial
    )

    $resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
    $resolvedProject = [System.IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
    $projectPrefix = $resolvedProject + '\'
    $outputIsProject = $resolvedOutput.Equals(
        $resolvedProject,
        [System.StringComparison]::OrdinalIgnoreCase
    )
    $outputIsProjectChild = $resolvedOutput.StartsWith(
        $projectPrefix,
        [System.StringComparison]::OrdinalIgnoreCase
    )
    if ($outputIsProject -or $outputIsProjectChild) {
        throw 'ArtifactOutputDirectory must be outside the project root'
    }

    New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null
    $copied = @()
    foreach ($source in $SourceArtifacts + @($CompileCommands)) {
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Cannot seal missing firmware artifact: $source"
        }
        $destination = Join-Path $resolvedOutput ([System.IO.Path]::GetFileName($source))
        if (Test-Path -LiteralPath $destination) {
            throw "Refusing to overwrite sealed firmware artifact: $destination"
        }
        Copy-Item -LiteralPath $source -Destination $destination
        $item = Get-Item -LiteralPath $destination
        $copied += [ordered]@{
            path = $item.Name
            size_bytes = [int64]$item.Length
            sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }

    $metadataPath = Join-Path $resolvedOutput 'artifact-manifest.json'
    [ordered]@{
        schema_version = 'phase4-sealed-firmware-v1'
        profile = $Profile
        source_revision = $Revision
        device_serial = $Serial
        build_id = ('{0}:{1}:{2}' -f $Profile, $Revision, $Serial)
        artifacts = $copied
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $metadataPath -Encoding utf8

    foreach ($path in (Get-ChildItem -LiteralPath $resolvedOutput -File)) {
        Set-ItemProperty -LiteralPath $path.FullName -Name IsReadOnly -Value $true
    }
    Write-Output "Sealed firmware artifacts: PASS ($resolvedOutput)"
}

& (Join-Path $PSScriptRoot 'selfcheck.ps1') -Mode HostOnly

$previousBuildProfile = $env:TRANSPORT_BUILD_PROFILE
$previousGitRevision = $env:TRANSPORT_GIT_REVISION
$previousDeviceSerial = $env:TRANSPORT_DEVICE_SERIAL
$previousReliabilityManifest = $env:TRANSPORT_RELIABILITY_EVIDENCE_MANIFEST
$env:TRANSPORT_BUILD_PROFILE = $BuildProfile.ToLowerInvariant()
$env:TRANSPORT_GIT_REVISION = $GitRevision
$env:TRANSPORT_DEVICE_SERIAL = $DeviceSerial
if ($ReliabilityEvidenceManifest) {
    if ($resolvedManifest) {
        $env:TRANSPORT_RELIABILITY_EVIDENCE_MANIFEST = $resolvedManifest
    }
    else {
        $env:TRANSPORT_RELIABILITY_EVIDENCE_MANIFEST =
            [System.IO.Path]::GetFullPath($ReliabilityEvidenceManifest)
    }
}

try {
    $firmwareDir = Join-Path $ProjectRoot 'firmware'
    $sconsArguments = @('-C', $firmwareDir, '-j4', '-Q')
    if ($ReliabilityEvidence -or $BuildProfile -eq 'FaultInjection') {
        $sconsArguments += 'reliability_evidence=1'
    }
    & $SConsExe @sconsArguments
    if ($LASTEXITCODE -ne 0) {
        throw "固件构建失败，SCons exit code: $LASTEXITCODE"
    }

    $elf = Join-Path $firmwareDir 'build\transport_recorder.elf'
    $bin = Join-Path $firmwareDir 'build\transport_recorder.bin'
    $map = Join-Path $firmwareDir 'build\transport_recorder.map'

    if (Test-Path -LiteralPath $elf) {
        foreach ($artifact in @($elf, $bin, $map)) {
            if (-not (Test-Path -LiteralPath $artifact)) {
                throw "固件构建产物不存在：$artifact"
            }
            if ((Get-Item -LiteralPath $artifact).Length -eq 0) {
                throw "固件构建产物为空：$artifact"
            }
        }

        & (Join-Path $ProjectRoot 'tools\check_memory_map.ps1') `
            -ElfPath $elf `
            -MapPath $map `
            -ObjdumpPath $ArmObjdumpExe

        & (Join-Path $ProjectRoot 'tools\check_icm45686_unaligned_access.ps1') `
            -ElfPath $elf `
            -ObjdumpPath $ArmObjdumpExe

        & $ArmSizeExe $elf

        $compileCommands = Join-Path $firmwareDir 'compile_commands.json'
        if ($SaveReliabilityOffBaseline -or $ArtifactOutputDirectory) {
            if (-not (Test-Path -LiteralPath $compileCommands)) {
                throw "编译命令数据库不存在：$compileCommands"
            }
            if ((Get-Item -LiteralPath $compileCommands).Length -eq 0) {
                throw "编译命令数据库为空：$compileCommands"
            }

            if ($SaveReliabilityOffBaseline) {
                $baselineDir = Join-Path $firmwareDir 'build'
                $offElf = Join-Path $baselineDir 'transport_recorder.off.elf'
                $offBin = Join-Path $baselineDir 'transport_recorder.off.bin'
                $offMap = Join-Path $baselineDir 'transport_recorder.off.map'
                $offCompileCommands = Join-Path $baselineDir 'compile_commands.off.json'

                Copy-Item -LiteralPath $elf -Destination $offElf -Force
                Copy-Item -LiteralPath $bin -Destination $offBin -Force
                Copy-Item -LiteralPath $map -Destination $offMap -Force
                Copy-Item `
                    -LiteralPath $compileCommands `
                    -Destination $offCompileCommands `
                    -Force
                Write-Output (
                    'Reliability off baseline: SAVED ({0}, {1}, {2}, {3})' -f
                    $offElf,
                    $offBin,
                    $offMap,
                    $offCompileCommands
                )
            }
        }

        if ($ArtifactOutputDirectory) {
            Seal-FirmwareArtifacts `
                -OutputDirectory $ArtifactOutputDirectory `
                -SourceArtifacts @($elf, $bin, $map) `
                -CompileCommands $compileCommands `
                -Profile $BuildProfile `
                -Revision $GitRevision `
                -Serial $DeviceSerial
        }

        Write-Output "Firmware artifacts: PASS ($elf, $bin, $map)"
        Write-Output "Build identity: profile=$BuildProfile revision=$GitRevision serial=$DeviceSerial"
        Write-Output 'Application vector and memory map: PASS'
        exit 0
    }

    if ($RequireElf) {
        throw "尚未生成ELF；需要先完成实际开发板BSP、启动文件和链接脚本：$elf"
    }

    throw "固件ELF不存在：$elf"
}
finally {
    if ($null -eq $previousBuildProfile) {
        Remove-Item Env:TRANSPORT_BUILD_PROFILE -ErrorAction SilentlyContinue
    }
    else {
        $env:TRANSPORT_BUILD_PROFILE = $previousBuildProfile
    }
    if ($null -eq $previousGitRevision) {
        Remove-Item Env:TRANSPORT_GIT_REVISION -ErrorAction SilentlyContinue
    }
    else {
        $env:TRANSPORT_GIT_REVISION = $previousGitRevision
    }
    if ($null -eq $previousDeviceSerial) {
        Remove-Item Env:TRANSPORT_DEVICE_SERIAL -ErrorAction SilentlyContinue
    }
    else {
        $env:TRANSPORT_DEVICE_SERIAL = $previousDeviceSerial
    }
    if ($null -eq $previousReliabilityManifest) {
        Remove-Item Env:TRANSPORT_RELIABILITY_EVIDENCE_MANIFEST `
            -ErrorAction SilentlyContinue
    }
    else {
        $env:TRANSPORT_RELIABILITY_EVIDENCE_MANIFEST =
            $previousReliabilityManifest
    }
}
