param(
    [string]$Port = 'COM8',
    [int]$EventId = 2,
    [int]$Rounds = 20,
    [string]$OutputRoot,
    [switch]$SkipPhysicalPrompt  # diagnostic-only; never eligible for formal PASS
)

$ErrorActionPreference = 'Stop'
# Invoke-TransportRecorder records native exit codes in each matrix row.
# Do not let PowerShell turn those expected nonzero codes into terminating
# NativeCommandExitException errors before they can be captured.
$PSNativeCommandUseErrorActionPreference = $false
$diagnosticOnly = [bool]$SkipPhysicalPrompt

. (Join-Path $PSScriptRoot 'project_env.ps1')

# The host package is imported from the repository root. Keep the script
# independent of the directory from which the operator launched it.
Set-Location -LiteralPath $ProjectRoot

if ($Rounds -lt 1) {
    throw 'Rounds must be at least 1.'
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $ProjectRoot 'evidence\hardware-bringup\2026-08-15\main-two-hour-regression-001\results\terp-physical-matrix'
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Invoke-TransportRecorder {
    param(
        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    $output = & $PythonExe -m host.transport_recorder.cli @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $text = [string]::Join("`n", [string[]]$output)
    $json = $null
    if ($exitCode -eq 0) {
        try {
            $json = $text | ConvertFrom-Json
        }
        catch {
            $json = $null
        }
    }
    return [pscustomobject]@{
        exit_code = $exitCode
        text = $text
        json = $json
    }
}

function Find-EventSummary {
    param(
        [Parameter(Mandatory)]
        [int]$TargetEventId
    )

    $afterEventId = 0
    for ($page = 1; $page -le 256; $page++) {
        $pageResult = Invoke-TransportRecorder -Arguments @(
            'events', 'list', '--port', $Port, '--after', $afterEventId,
            '--limit', 16, '--json'
        )
        if ($pageResult.exit_code -ne 0 -or $null -eq $pageResult.json) {
            return [pscustomobject]@{
                exit_code = $pageResult.exit_code
                pages = $page
                event = $null
                found = $false
                error = 'LIST_EVENTS failed or returned invalid JSON.'
            }
        }

        $event = @($pageResult.json.events |
            Where-Object { $_.event_id -eq $TargetEventId }) |
            Select-Object -First 1
        if (@($event).Count -gt 0) {
            return [pscustomobject]@{
                exit_code = 0
                pages = $page
                event = $event
                found = $true
                error = $null
            }
        }

        $nextEventId = [uint32]$pageResult.json.next_event_id
        if ($nextEventId -eq 0) {
            break
        }
        if ($nextEventId -le $afterEventId) {
            return [pscustomobject]@{
                exit_code = 1
                pages = $page
                event = $null
                found = $false
                error = 'LIST_EVENTS cursor did not advance.'
            }
        }
        $afterEventId = $nextEventId
    }

    return [pscustomobject]@{
        exit_code = 0
        pages = 256
        event = $null
        found = $false
        error = "Event $TargetEventId was not found in the complete LIST_EVENTS pagination."
    }
}

$baseline = $null
$rows = @()
$startedAt = Get-Date

for ($round = 1; $round -le $Rounds; $round++) {
    $tag = 'round-{0:d2}' -f $round
    if (-not $SkipPhysicalPrompt) {
        $confirmation = Read-Host ("{0}/{1}: 请实际拔下并插回 USB-TTL，确认完成后输入 READY" -f $round, $Rounds)
        if ($confirmation -ne 'READY') {
            throw "Physical reconnection was not confirmed for $tag."
        }
    }

    $row = [ordered]@{
        round = $round
        info_exit = $null
        health_exit = $null
        events_list_exit = $null
        events_list_pages = $null
        event_found = $false
        event_lookup_error = $null
        download_exit = $null
        length = $null
        record_crc32 = $null
        download_info_crc32 = $null
        downloaded_bytes_crc32 = $null
        list_crc_matches_download_info = $false
        device_crc_matches_download = $false
        sha256 = $null
        physical_confirmation = -not $SkipPhysicalPrompt
        formal_pass_eligible = -not $SkipPhysicalPrompt
        status = 'FAIL'
    }

    $info = Invoke-TransportRecorder -Arguments @('info', '--port', $Port, '--json')
    $row.info_exit = $info.exit_code
    $health = Invoke-TransportRecorder -Arguments @('health', '--port', $Port, '--json')
    $row.health_exit = $health.exit_code
    $eventLookup = Find-EventSummary -TargetEventId $EventId
    $row.events_list_exit = $eventLookup.exit_code
    $row.events_list_pages = $eventLookup.pages
    $row.event_found = [bool]$eventLookup.found
    $row.event_lookup_error = $eventLookup.error
    if ($row.event_found) {
        if ($null -ne $eventLookup.event.event_crc32) {
            $row.record_crc32 = [uint32]$eventLookup.event.event_crc32
        } else {
            $row.event_lookup_error = 'LIST_EVENTS returned the target event without a CRC32.'
        }
    }

    $outputPath = Join-Path $OutputRoot ("event-{0}-{1:d2}.bin" -f $EventId, $round)
    $download = Invoke-TransportRecorder -Arguments @(
        'events', 'download', '--port', $Port, '--id', $EventId,
        '--output', $outputPath, '--json'
    )
    $row.download_exit = $download.exit_code
    if ($download.json) {
        if ($null -ne $download.json.device_event_info.event_crc32) {
            $row.download_info_crc32 = [uint32]$download.json.device_event_info.event_crc32
        }
        if ($null -ne $download.json.downloaded_crc32) {
            $row.downloaded_bytes_crc32 = [uint32]$download.json.downloaded_crc32
        }
    }
    if (Test-Path -LiteralPath $outputPath) {
        $row.length = (Get-Item -LiteralPath $outputPath).Length
        $row.sha256 = (Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash
    }

    $row.list_crc_matches_download_info = (
        $null -ne $row.record_crc32 -and
        $null -ne $row.download_info_crc32 -and
        $row.record_crc32 -eq $row.download_info_crc32
    )
    $row.device_crc_matches_download = (
        $null -ne $row.record_crc32 -and
        $null -ne $row.downloaded_bytes_crc32 -and
        $row.record_crc32 -eq $row.downloaded_bytes_crc32
    )

    $baselineReady = (
        $row.event_found -and
        $null -ne $row.record_crc32 -and
        $null -ne $row.download_info_crc32 -and
        $null -ne $row.downloaded_bytes_crc32 -and
        $row.list_crc_matches_download_info -and
        $row.device_crc_matches_download -and
        $null -ne $row.length -and
        $null -ne $row.sha256
    )
    if ($null -eq $baseline -and
        $row.info_exit -eq 0 -and
        $row.health_exit -eq 0 -and
        $row.events_list_exit -eq 0 -and
        $row.download_exit -eq 0 -and
        $baselineReady) {
        $baseline = [ordered]@{
            length = $row.length
            record_crc32 = $row.record_crc32
            download_info_crc32 = $row.download_info_crc32
            downloaded_bytes_crc32 = $row.downloaded_bytes_crc32
            sha256 = $row.sha256
        }
    }

    $readbackPass = (
        $row.info_exit -eq 0 -and
        $row.health_exit -eq 0 -and
        $row.events_list_exit -eq 0 -and
        $row.download_exit -eq 0 -and
        $row.event_found -and
        $null -ne $row.record_crc32 -and
        $null -ne $row.download_info_crc32 -and
        $null -ne $row.downloaded_bytes_crc32 -and
        $row.list_crc_matches_download_info -and
        $row.device_crc_matches_download -and
        $null -ne $baseline -and
        $row.length -eq $baseline.length -and
        $row.record_crc32 -eq $baseline.record_crc32 -and
        $row.download_info_crc32 -eq $baseline.download_info_crc32 -and
        $row.downloaded_bytes_crc32 -eq $baseline.downloaded_bytes_crc32 -and
        $row.sha256 -eq $baseline.sha256
    )
    $row.status = if (-not $readbackPass) {
        'FAIL'
    } elseif ($diagnosticOnly) {
        'DIAGNOSTIC_NOT_PHYSICAL'
    } else {
        'PASS'
    }

    $rows += [pscustomobject]$row
    $row | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $OutputRoot "$tag.json") -Encoding utf8
    Write-Output ("{0}: {1} info={2} health={3} list={4} download={5} bytes={6}" -f
        $tag, $row.status, $row.info_exit, $row.health_exit,
        $row.events_list_exit, $row.download_exit, $row.length)
    if ($row.status -eq 'FAIL') {
        throw "TERP physical matrix failed at $tag; stop physical reconnect actions."
    }
}

$passCount = @($rows | Where-Object { $_.status -eq 'PASS' }).Count
$diagnosticPassCount = @($rows | Where-Object { $_.status -eq 'DIAGNOSTIC_NOT_PHYSICAL' }).Count
$result = [ordered]@{
    schema = 'terp-physical-matrix-1'
    status = if ($diagnosticOnly) { 'DIAGNOSTIC_NOT_PHYSICAL' } elseif ($passCount -eq $Rounds) { 'PASS' } else { 'FAIL' }
    formal_pass_eligible = -not $diagnosticOnly
    port = $Port
    event_id = $EventId
    rounds = $Rounds
    physical_confirmation = -not $SkipPhysicalPrompt
    started_at = $startedAt.ToString('o')
    ended_at = (Get-Date).ToString('o')
    passes = $passCount
    diagnostic_passes = $diagnosticPassCount
    failures = $Rounds - $passCount
    baseline = $baseline
    rows = $rows
}
$result | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputRoot 'results.json') -Encoding utf8

if ($passCount -ne $Rounds) {
    if ($diagnosticOnly -and $diagnosticPassCount -eq $Rounds) {
        $diagnosticMessage = "TERP matrix DIAGNOSTIC_NOT_PHYSICAL: {0}/{1}; no physical confirmation was collected and the result is not formal PASS." -f
            $diagnosticPassCount, $Rounds
        Write-Warning $diagnosticMessage
        exit 0
    }
    throw "TERP physical matrix failed: $passCount/$Rounds rounds passed."
}
Write-Output ("TERP matrix PASS: {0}/{1}; physical_confirmation={2}" -f
    $passCount, $Rounds, (-not $SkipPhysicalPrompt))
