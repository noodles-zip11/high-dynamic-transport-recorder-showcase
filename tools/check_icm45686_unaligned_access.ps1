param(
    [Parameter(Mandatory = $true)]
    [string]$ElfPath,
    [Parameter(Mandatory = $true)]
    [string]$ObjdumpPath
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

foreach ($path in @($ElfPath, $ObjdumpPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "ICM45686 alignment-gate input not found: $path"
    }
}

function Get-FunctionInstructions {
    param([string]$Name)

    $dump = (& $ObjdumpPath -d ("--disassemble={0}" -f $Name) $ElfPath) -join "`n"
    $functionMatch = [regex]::Match(
        $dump,
        ("(?ms)<{0}>:\s*(.*?)(?=\r?\n\r?\nDisassembly of section|\z)" -f [regex]::Escape($Name))
    )
    if (-not $functionMatch.Success) {
        return $null
    }

    return $functionMatch.Groups[1].Value
}

function Get-ByteLoadCount {
    param([string]$Instructions)

    return [regex]::Matches($Instructions, '(?im)\bldrb(?:\.w)?\b').Count
}

function Assert-NoHalfwordLoads {
    param(
        [string]$Name,
        [string]$Instructions
    )

    if ($Instructions -match '(?im)\bldrh(?:\.w)?\b') {
        throw "$Name contains a halfword load; SPI response bytes must be read individually."
    }
}

$fifoInstructions = Get-FunctionInstructions 'icm45686_read_fifo_count'
if ($null -eq $fifoInstructions) {
    throw 'Unable to disassemble icm45686_read_fifo_count for the alignment gate.'
}
Assert-NoHalfwordLoads 'icm45686_read_fifo_count' $fifoInstructions

$symbolTable = (& $ObjdumpPath -t $ElfPath) -join "`n"
$readRegSymbol = $symbolTable -match '(?m)\bicm45686_read_reg\s*$'
if ($readRegSymbol) {
    $readRegInstructions = Get-FunctionInstructions 'icm45686_read_reg'
    if ($null -eq $readRegInstructions) {
        throw 'The icm45686_read_reg symbol exists but cannot be disassembled.'
    }

    Assert-NoHalfwordLoads 'icm45686_read_reg' $readRegInstructions
    $readRegByteLoadCount = Get-ByteLoadCount $readRegInstructions
    if ($readRegByteLoadCount -lt 1) {
        throw 'icm45686_read_reg does not contain the expected byte-sized response copy.'
    }

    $fifoByteLoadCount = Get-ByteLoadCount $fifoInstructions
    if ($fifoByteLoadCount -lt 2) {
        throw 'icm45686_read_fifo_count does not contain the expected byte-sized FIFO decode loads.'
    }

    Write-Output "ICM45686 alignment gate: PASS (out-of-line read_reg: $readRegByteLoadCount byte loads; FIFO decode: $fifoByteLoadCount; no halfword loads)"
}
else {
    $fifoByteLoadCount = Get-ByteLoadCount $fifoInstructions
    if ($fifoByteLoadCount -lt 4) {
        throw 'Inlined icm45686_read_reg path does not contain four byte-sized SPI-copy/decode loads.'
    }

    Write-Output "ICM45686 alignment gate: PASS (inlined read_reg: $fifoByteLoadCount byte loads; no halfword loads)"
}
