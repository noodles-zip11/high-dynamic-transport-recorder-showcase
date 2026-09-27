param(
    [string]$ElfPath,
    [string]$OffElfPath,
    [string]$BinPath,
    [string]$OffBinPath,
    [string]$MapPath,
    [string]$ObjdumpPath,
    [string]$CompileCommandsPath,
    [string]$OffCompileCommandsPath
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

. (Join-Path $PSScriptRoot 'project_env.ps1')

$firmwareRoot = Join-Path $ProjectRoot 'firmware'
$buildRoot = Join-Path $firmwareRoot 'build'
if ([string]::IsNullOrWhiteSpace($ElfPath)) {
    $ElfPath = Join-Path $buildRoot 'transport_recorder.elf'
}
if ([string]::IsNullOrWhiteSpace($OffElfPath)) {
    $OffElfPath = Join-Path $buildRoot 'transport_recorder.off.elf'
}
if ([string]::IsNullOrWhiteSpace($BinPath)) {
    $BinPath = Join-Path $buildRoot 'transport_recorder.bin'
}
if ([string]::IsNullOrWhiteSpace($OffBinPath)) {
    $OffBinPath = Join-Path $buildRoot 'transport_recorder.off.bin'
}
if ([string]::IsNullOrWhiteSpace($MapPath)) {
    $MapPath = Join-Path $buildRoot 'transport_recorder.map'
}
if ([string]::IsNullOrWhiteSpace($ObjdumpPath)) {
    $ObjdumpPath = $ArmObjdumpExe
}
if ([string]::IsNullOrWhiteSpace($CompileCommandsPath)) {
    $CompileCommandsPath = Join-Path $firmwareRoot 'compile_commands.json'
}
if ([string]::IsNullOrWhiteSpace($OffCompileCommandsPath)) {
    $OffCompileCommandsPath = Join-Path $buildRoot 'compile_commands.off.json'
}

foreach ($path in @(
        $ElfPath,
        $OffElfPath,
        $BinPath,
        $OffBinPath,
        $MapPath,
        $ObjdumpPath,
        $CompileCommandsPath,
        $OffCompileCommandsPath
    )) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Crash fault context input not found: $path"
    }
}

function Invoke-Objdump {
    param([string[]]$Arguments)

    $output = & $ObjdumpPath @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "objdump failed ($LASTEXITCODE): $($Arguments -join ' ')"
    }
    return ($output -join "`n")
}

function Convert-LittleEndianWord {
    param([string]$HexBytes)

    if ($HexBytes.Length -ne 8) {
        throw "Expected eight hex bytes, got '$HexBytes'."
    }
    return [Convert]::ToUInt32(
        $HexBytes.Substring(6, 2) + $HexBytes.Substring(4, 2) +
        $HexBytes.Substring(2, 2) + $HexBytes.Substring(0, 2),
        16
    )
}

function Get-SectionTable {
    param([string]$Path)

    $text = Invoke-Objdump @('-h', $Path)
    $sections = @{}
    $lines = $text -split "`r?`n"
    for ($index = 0; $index -lt $lines.Count; $index++) {
        $sectionPattern = '^ *[0-9]+\s+(\S+)\s+([0-9A-Fa-f]+)\s+'
        $sectionPattern += '([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+'
        $sectionPattern += '([0-9A-Fa-f]+)\s+(\S+)\s*$'
        $match = [regex]::Match(
            $lines[$index],
            $sectionPattern
        )
        if (-not $match.Success) {
            continue
        }
        $flags = if ($index + 1 -lt $lines.Count) {
            $lines[$index + 1].Trim()
        }
        else {
            ''
        }
        $sections[$match.Groups[1].Value] = [pscustomobject]@{
            Size = [Convert]::ToUInt32($match.Groups[2].Value, 16)
            Vma = [Convert]::ToUInt32($match.Groups[3].Value, 16)
            Lma = [Convert]::ToUInt32($match.Groups[4].Value, 16)
            FileOffset = [Convert]::ToUInt32($match.Groups[5].Value, 16)
            Alignment = $match.Groups[6].Value
            Flags = $flags
        }
    }
    return ,$sections
}

function Get-SymbolAddress {
    param(
        [string]$Symbols,
        [string]$Name
    )

    $pattern = '(?m)^\s*([0-9A-Fa-f]+)\s+'
    $pattern += '(?:\S+\s+){2,3}[0-9A-Fa-f]+\s+'
    $pattern += [regex]::Escape($Name) + '\s*$'
    $match = [regex]::Match($Symbols, $pattern)
    if (-not $match.Success) {
        throw "Expected symbol is missing from ELF: $Name"
    }
    return [Convert]::ToUInt32($match.Groups[1].Value, 16)
}

function Get-VectorWords {
    param([string]$Path)

    $dump = Invoke-Objdump @(
        '-s',
        '--start-address=0x0802000c',
        '--stop-address=0x0802001c',
        $Path
    )
    $line = $dump -split "`r?`n" |
        Where-Object { $_ -match '^\s*[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}' } |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($line)) {
        throw "Unable to read four fault vector words from $Path."
    }
    $match = [regex]::Match(
        $line,
        '^\s*[0-9A-Fa-f]+\s+((?:[0-9A-Fa-f]{8}\s+){4})'
    )
    if (-not $match.Success) {
        throw "Fault vector line is not four 32-bit words in $Path."
    }
    $words = @(
        [regex]::Matches($match.Groups[1].Value, '[0-9A-Fa-f]{8}') |
            ForEach-Object { Convert-LittleEndianWord $_.Value }
    )
    if ($words.Count -ne 4) {
        throw "Expected four fault vector words in $Path."
    }
    return ,$words
}

function Assert-VectorTargets {
    param(
        [string]$Path,
        [string[]]$ExpectedNames,
        [string]$Label
    )

    $symbols = Invoke-Objdump @('-t', $Path)
    $words = Get-VectorWords $Path
    for ($index = 0; $index -lt $ExpectedNames.Count; $index++) {
        $expected = Get-SymbolAddress $symbols $ExpectedNames[$index]
        $word = [uint64]$words[$index]
        $address = [uint64]($word -band [uint64]4294967294)
        $thumbBit = [uint64]($word -band [uint64]1)
        if ($thumbBit -ne [uint64]1 -or $address -ne $expected) {
            throw (
                "$Label vector {0} targets 0x{1:X8}; expected " +
                "{2} at 0x{3:X8}." -f
                $index,
                $word,
                $ExpectedNames[$index],
                $expected
            )
        }
        Write-Output (
            "$Label vector {0}: 0x{1:X8} -> {2} (0x{3:X8})" -f
            $index,
            $word,
            $ExpectedNames[$index],
            $expected
        )
    }
}

function Get-UndefinedSymbols {
    param([string]$Path)

    $symbols = Invoke-Objdump @('-t', $Path)
    $undefinedPattern = '^\s*[0-9A-Fa-f]+\s+\*UND\*\s+'
    $undefinedPattern += '[0-9A-Fa-f]+\s+(\S+)'
    return @(
        $symbols -split "`r?`n" |
            ForEach-Object {
                $match = [regex]::Match(
                    $_,
                    $undefinedPattern
                )
                if ($match.Success) {
                    $match.Groups[1].Value
                }
            } |
            Sort-Object -Unique
    )
}

function Assert-LinkerBoundaries {
    param(
        [string]$EnabledSymbols,
        [string]$OffSymbols
    )

    $boundaryNames = @(
        'g_pfnVectors',
        '_sstack',
        '_estack',
        '__heap_end',
        '__dma_buffer_start',
        '__dma_buffer_end',
        '__d2_sram1_start',
        '__d2_sram1_end'
    )
    foreach ($name in $boundaryNames) {
        $enabled = Get-SymbolAddress $EnabledSymbols $name
        $off = Get-SymbolAddress $OffSymbols $name
        if ($enabled -ne $off) {
            throw (
                'Linker boundary {0} moved: enabled=0x{1:X8}, off=0x{2:X8}.' -f
                $name,
                $enabled,
                $off
            )
        }
        if ($name -eq 'g_pfnVectors' -and $enabled -ne [uint64]0x08020000) {
            throw (
                'Application vector origin moved: enabled=0x{0:X8}; ' +
                'expected=0x08020000.' -f $enabled
            )
        }
        Write-Output (
            'Linker boundary {0}: 0x{1:X8} (unchanged)' -f
            $name,
            $enabled
        )
    }

    $heapStart = Get-SymbolAddress $EnabledSymbols '__heap_start'
    $heapEnd = Get-SymbolAddress $EnabledSymbols '__heap_end'
    $minimumHeapBytes = [uint64]0x4000
    $heapBytes = [uint64]$heapEnd - [uint64]$heapStart
    if ($heapBytes -lt $minimumHeapBytes) {
        throw (
            'Enabled heap is {0} bytes; expected at least {1} bytes.' -f
            $heapBytes,
            $minimumHeapBytes
        )
    }
    Write-Output (
        'Enabled heap: 0x{0:X8}-0x{1:X8} ({2} bytes)' -f
        $heapStart,
        $heapEnd,
        $heapBytes
    )
}

function Assert-UndefinedSymbols {
    param(
        [string]$Path,
        [string[]]$Allowed
    )

    $actual = @(Get-UndefinedSymbols $Path)
    $unexpected = @($actual | Where-Object { $_ -notin $Allowed })
    $missing = @($Allowed | Where-Object { $_ -notin $actual })
    if ($unexpected.Count -gt 0 -or $missing.Count -gt 0) {
        throw (
            "Unexpected undefined symbols in {0}; unexpected=[{1}] missing=[{2}]" -f
            $Path,
            ($unexpected -join ', '),
            ($missing -join ', ')
        )
    }
}

function Assert-NoForbiddenFaultReferences {
    param([string[]]$Paths)

    $forbidden = @(
        'rt_hw_hard_fault_exception',
        'rt_thread',
        'rt_kprintf',
        'HAL_',
        'malloc',
        'free',
        'printf',
        'snprintf',
        'vsnprintf',
        'strlen',
        'memcpy',
        'memset',
        'flash',
        'qspi',
        'terp',
        'ai_'
    )
    foreach ($path in $Paths) {
        $text = (Invoke-Objdump @('-t', '-r', '-d', $path)).ToLowerInvariant()
        foreach ($name in $forbidden) {
            if ($text.Contains($name.ToLowerInvariant())) {
                throw "Forbidden fault-context reference '$name' found in $path."
            }
        }
    }
}

function Get-CompilationDatabase {
    param([string]$Path)

    try {
        return @(Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
    }
    catch {
        throw "Unable to parse compilation database $Path`: $($_.Exception.Message)"
    }
}

function Assert-CompilationIsolation {
    param(
        [string]$EnabledPath,
        [string]$OffPath
    )

    $replacementNames = @(
        'HardFault_Handler=crash_record_HardFault_Handler',
        'MemManage_Handler=crash_record_MemManage_Handler',
        'BusFault_Handler=crash_record_BusFault_Handler',
        'UsageFault_Handler=crash_record_UsageFault_Handler'
    )
    $enabledEntries = Get-CompilationDatabase $EnabledPath
    $startupEntries = @(
        $enabledEntries |
            Where-Object { $_.file -match 'startup_stm32h743xx\.s$' }
    )
    if ($startupEntries.Count -ne 1) {
        throw "Enabled compilation database must contain exactly one startup object."
    }
    $startupCommand = [string]$startupEntries[0].command
    foreach ($replacement in $replacementNames) {
        if ($startupCommand -notmatch [regex]::Escape("-D$replacement")) {
            throw "Startup object is missing isolated substitution: $replacement"
        }
    }
    foreach ($entry in $enabledEntries) {
        $isNonStartup = $entry.file -notmatch 'startup_stm32h743xx\.s$'
        $hasReplacement = [string]$entry.command -match (
            'crash_record_(HardFault|MemManage|BusFault|UsageFault)_Handler'
        )
        if ($isNonStartup -and $hasReplacement) {
            throw "Fault vector substitution leaked into object: $($entry.file)"
        }
    }

    $offEntries = Get-CompilationDatabase $OffPath
    foreach ($entry in $offEntries) {
        $command = [string]$entry.command
        $hasFaultSubstitution = $command -match (
            'HardFault_Handler=crash_record_'
        ) -or $command -match (
            'MemManage_Handler=crash_record_'
        ) -or $command -match (
            'BusFault_Handler=crash_record_'
        ) -or $command -match (
            'UsageFault_Handler=crash_record_'
        )
        if ($hasFaultSubstitution) {
            throw "Fault vector substitution leaked into off object: $($entry.file)"
        }
        $hasCrashSource = $command -match (
            'crash_fault_entry\.S|crash_record_target\.c'
        ) -or $command -match 'app[\\/]reliability[\\/]crash_record\.c'
        if ($hasCrashSource) {
            throw "CrashRecord source leaked into off build: $($entry.file)"
        }
        if ($command -match 'TRANSPORT_RELIABILITY_EVIDENCE_ENABLED') {
            throw "Reliability macro leaked into off object: $($entry.file)"
        }
    }
}

$enabledSections = Get-SectionTable $ElfPath
if (-not $enabledSections.ContainsKey('.crash_record')) {
    throw 'Enabled ELF has no .crash_record section.'
}
$crashSection = $enabledSections['.crash_record']
$crashSectionExact = $crashSection.Size -eq 0x200 `
    -and $crashSection.Vma -eq 0x3800FE00 `
    -and $crashSection.Lma -eq 0x3800FE00 `
    -and $crashSection.Flags -match '^ALLOC$'
if (-not $crashSectionExact) {
    throw 'Enabled .crash_record is not an exact 512-byte NOLOAD retained SRAM section.'
}
$enabledMap = Get-Content -LiteralPath $MapPath -Raw
if ($enabledMap -notmatch '(?m)^\s*\.crash_record\s+0x3800fe00\s+0x200\s*$') {
    throw 'Enabled map does not retain the exact .crash_record SRAM4 range.'
}

$offSections = Get-SectionTable $OffElfPath
if ($offSections.ContainsKey('.crash_record')) {
    throw 'Default-off ELF unexpectedly contains .crash_record.'
}

$enabledSymbols = Invoke-Objdump @('-t', $ElfPath)
$offSymbols = Invoke-Objdump @('-t', $OffElfPath)
if ($offSymbols -match '(?m)\bcrash_record_[A-Za-z0-9_]+\b') {
    throw 'Default-off ELF unexpectedly contains a CrashRecord symbol.'
}

Assert-LinkerBoundaries $enabledSymbols $offSymbols

Assert-VectorTargets $ElfPath @(
    'crash_record_HardFault_Handler',
    'crash_record_MemManage_Handler',
    'crash_record_BusFault_Handler',
    'crash_record_UsageFault_Handler'
) 'enabled'
Assert-VectorTargets $OffElfPath @(
    'HardFault_Handler',
    'Default_Handler',
    'Default_Handler',
    'Default_Handler'
) 'off'

Assert-CompilationIsolation $CompileCommandsPath $OffCompileCommandsPath

$faultObjects = @(
    (Join-Path $buildRoot 'bsp\openmv4_h743\crash_fault_entry.o'),
    (Join-Path $buildRoot 'bsp\openmv4_h743\crash_record_target.o'),
    (Join-Path $buildRoot 'app\reliability\crash_record.o')
)
foreach ($path in $faultObjects) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Enabled fault-context object not found: $path"
    }
}
Assert-UndefinedSymbols $faultObjects[0] @('crash_record_fault_capture')
Assert-UndefinedSymbols $faultObjects[1] @(
    '__crash_record_slot_a',
    '__crash_record_slot_b',
    '__crash_record_end',
    '__heap_end',
    'crash_record_ack',
    'crash_record_recover',
    'crash_record_write_next'
)
Assert-UndefinedSymbols $faultObjects[2] @()
Assert-NoForbiddenFaultReferences $faultObjects

$enabledBinBytes = (Get-Item -LiteralPath $BinPath).Length
$offBinBytes = (Get-Item -LiteralPath $OffBinPath).Length
$emptyBinary = $enabledBinBytes -eq 0 -or $offBinBytes -eq 0
if ($emptyBinary) {
    throw 'Enabled or default-off firmware binary is empty.'
}
$binaryOutsideFlash = $enabledBinBytes -ge 0x00200000 `
    -or $offBinBytes -ge 0x00200000
if ($binaryOutsideFlash) {
    throw 'CrashRecord NOLOAD overlay unexpectedly expands the binary beyond Flash span.'
}

Write-Output (
    'Crash fault context: PASS ({0}; {1}; {2})' -f
    'enabled vectors redirected',
    'off vectors unchanged',
    'isolated startup substitutions; no forbidden transitive references'
)
