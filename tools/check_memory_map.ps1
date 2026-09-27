param(
    [Parameter(Mandatory = $true)]
    [string]$ElfPath,
    [Parameter(Mandatory = $true)]
    [string]$MapPath,
    [Parameter(Mandatory = $true)]
    [string]$ObjdumpPath
)

$ErrorActionPreference = 'Stop'

foreach ($path in @($ElfPath, $MapPath, $ObjdumpPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Memory-map input not found: $path"
    }
}

$sections = (& $ObjdumpPath -h $ElfPath) -join "`n"
$map = Get-Content -LiteralPath $MapPath -Raw

if ($sections -notmatch '(?m)^\s*\d+\s+\.text\s+\S+\s+08020000\s+') {
    throw 'The linked image does not start in the application Flash at 0x08020000.'
}

if ($map -notmatch '(?m)^\s*\.isr_vector\s+0x0*8020000\s+') {
    throw 'The interrupt vector table is not linked at 0x08020000.'
}

$textDump = (& $ObjdumpPath -s -j .text $ElfPath) -join "`n"
$vectorMatch = [regex]::Match(
    $textDump,
    '(?m)^\s*8020000\s+([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})\b'
)
if (-not $vectorMatch.Success) {
    throw 'Unable to read the first two application vector words from .text.'
}

function Convert-LittleEndianWord {
    param([string]$HexBytes)

    return [Convert]::ToUInt32(
        $HexBytes.Substring(6, 2) + $HexBytes.Substring(4, 2) +
        $HexBytes.Substring(2, 2) + $HexBytes.Substring(0, 2),
        16
    )
}

$initialMsp = Convert-LittleEndianWord $vectorMatch.Groups[1].Value
$resetHandler = Convert-LittleEndianWord $vectorMatch.Groups[2].Value
$resetAddress = $resetHandler -band 0xFFFFFFFE
if (($initialMsp % 8) -ne 0 -or $initialMsp -lt 0x24000000 -or $initialMsp -ge 0x24080000) {
    throw 'The application initial MSP is not an 8-byte-aligned AXI SRAM address.'
}
if (($resetHandler -band 1) -eq 0 -or $resetAddress -lt 0x08020000 -or $resetAddress -ge 0x081C0000) {
    throw 'The application reset vector is not a Thumb address inside the application region.'
}

foreach ($section in @('.data', '.bss')) {
    $escapedSection = [regex]::Escape($section)
    if ($map -notmatch ("(?m)^\s*{0}\s+0x240[0-9a-fA-F]+\s+" -f $escapedSection)) {
        throw "Section $section is not linked in AXI SRAM."
    }
}

function Get-MapSymbolAddress {
    param([string]$Name)

    $match = [regex]::Match(
        $map,
        ("(?m)^\s*0x([0-9a-fA-F]+)\s+{0}\s+=" -f [regex]::Escape($Name))
    )
    if (-not $match.Success) {
        throw "Expected memory symbol is missing: $Name"
    }

    return [Convert]::ToUInt32($match.Groups[1].Value, 16)
}

$heapStart = Get-MapSymbolAddress '__heap_start'
$heapEnd = Get-MapSymbolAddress '__heap_end'
$dmaStart = Get-MapSymbolAddress '__dma_buffer_start'
$dmaEnd = Get-MapSymbolAddress '__dma_buffer_end'

if ($heapStart -lt 0x24000000 -or $heapEnd -gt 0x24080000 -or $heapEnd -le $heapStart) {
    throw 'Heap placement violates the AXI SRAM policy.'
}

if (($dmaStart % 32) -ne 0 -or ($dmaEnd % 32) -ne 0 -or
    $dmaStart -lt 0x30000000 -or $dmaEnd -gt 0x30020000) {
    throw 'DMA buffer placement or alignment violates the D2 SRAM1 policy.'
}

Write-Output 'Memory map: PASS (application Flash/vector 0x08020000, AXI SRAM heap, D2 DMA 32-byte aligned)'
