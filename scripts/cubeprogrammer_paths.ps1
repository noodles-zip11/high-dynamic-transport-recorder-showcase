$ErrorActionPreference = 'Stop'

function Test-AsciiOnlyPath {
    param(
        [Parameter(Mandatory)]
        [AllowEmptyString()]
        [string]$Path
    )

    return [System.Text.Encoding]::ASCII.GetByteCount($Path) -eq $Path.Length
}

function Get-CubeProgrammerStagingRoot {
    param(
        [string]$PreferredRoot
    )

    $candidate = if ([string]::IsNullOrWhiteSpace($PreferredRoot)) {
        Join-Path $env:TEMP 'transport-recorder-cubeprogrammer'
    } else {
        $PreferredRoot
    }

    if (-not (Test-AsciiOnlyPath -Path $candidate)) {
        $candidate = Join-Path $env:SystemDrive 'transport-recorder-cubeprogrammer'
    }
    if (-not (Test-AsciiOnlyPath -Path $candidate)) {
        throw "CubeProgrammer staging root must be ASCII-only: $candidate"
    }

    New-Item -ItemType Directory -Force -Path $candidate | Out-Null
    return (Resolve-Path -LiteralPath $candidate).Path
}

function New-CubeProgrammerArtifact {
    param(
        [Parameter(Mandatory)]
        [string]$SourcePath,
        [string]$StagingRoot
    )

    if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
        throw "CubeProgrammer source artifact not found: $SourcePath"
    }

    $root = Get-CubeProgrammerStagingRoot -PreferredRoot $StagingRoot
    $session = Join-Path $root ("session-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $session | Out-Null

    $destination = Join-Path $session ([System.IO.Path]::GetFileName($SourcePath))
    Copy-Item -LiteralPath $SourcePath -Destination $destination -Force
    return (Resolve-Path -LiteralPath $destination).Path
}
