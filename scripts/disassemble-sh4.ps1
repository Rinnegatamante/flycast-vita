[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BinaryPath,
    [Parameter(Mandatory = $true)]
    [string]$StartAddress,
    [Parameter(Mandatory = $true)]
    [ValidateRange(2, 1048576)]
    [int]$ByteCount,
    [string]$BaseAddress = '0x8C000000',
    [string]$OpcodeTablePath = 'core\hw\sh4\sh4_opcode_list.cpp'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Convert-ToU32Address([string]$Value) {
    if ($Value -match '^0[xX]([0-9A-Fa-f]+)$') {
        return [Convert]::ToUInt32($Matches[1], 16)
    }
    return [Convert]::ToUInt32($Value, 10)
}

$StartAddress = Convert-ToU32Address $StartAddress
$BaseAddress = Convert-ToU32Address $BaseAddress

function Resolve-FromRoot([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $root $Path))
}

$resolvedBinary = Resolve-FromRoot $BinaryPath
$resolvedTable = Resolve-FromRoot $OpcodeTablePath
if (-not (Test-Path -LiteralPath $resolvedBinary -PathType Leaf)) {
    throw "Missing SH4 binary: $resolvedBinary"
}
if (-not (Test-Path -LiteralPath $resolvedTable -PathType Leaf)) {
    throw "Missing Flycast opcode table: $resolvedTable"
}
if (($StartAddress -band 1) -ne 0 -or ($ByteCount -band 1) -ne 0) {
    throw 'SH4 addresses and byte counts must be 16-bit aligned.'
}

$tableSource = Get-Content -LiteralPath $resolvedTable -Raw
$masks = @{}
foreach ($match in [regex]::Matches($tableSource, '(?m)^#define\s+(Mask_[A-Za-z0-9_]+)\s+(0x[0-9A-Fa-f]+)\s*$')) {
    $masks[$match.Groups[1].Value] = [Convert]::ToUInt32($match.Groups[2].Value.Substring(2), 16)
}

$entries = [Collections.Generic.List[object]]::new()
$entryPattern = '\{.*?,.*?,\s*(Mask_[A-Za-z0-9_]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,.*?,\s*"([^"]*)"\s*,\s*(\d+)\s*,\s*(\d+)'
foreach ($line in Get-Content -LiteralPath $resolvedTable) {
    if ($line -match $entryPattern) {
        $maskName = $Matches[1]
        if (-not $masks.ContainsKey($maskName)) {
            throw "Unknown opcode mask $maskName"
        }
        $entries.Add([pscustomobject]@{
            Mask = [uint32]$masks[$maskName]
            Value = [Convert]::ToUInt32($Matches[2].Substring(2), 16)
            Format = $Matches[3]
            IssueCycles = [int]$Matches[4]
            LatencyCycles = [int]$Matches[5]
        })
    }
}
if ($entries.Count -eq 0) {
    throw "No SH4 opcodes parsed from $resolvedTable"
}

$bytes = [IO.File]::ReadAllBytes($resolvedBinary)
$startOffset = [uint64]$StartAddress - [uint64]$BaseAddress
if ($StartAddress -lt $BaseAddress -or $startOffset + [uint64]$ByteCount -gt [uint64]$bytes.Length) {
    throw ('Range 0x{0:X8}+0x{1:X} is outside {2} (base 0x{3:X8}, size 0x{4:X}).' -f `
        $StartAddress, $ByteCount, $resolvedBinary, $BaseAddress, $bytes.Length)
}

function Get-U16([uint32]$Address) {
    $offset = [uint64]$Address - [uint64]$BaseAddress
    if ($Address -lt $BaseAddress -or $offset + 2 -gt [uint64]$bytes.Length) { return $null }
    return [uint16]([uint16]$bytes[$offset] -bor ([uint16]$bytes[$offset + 1] -shl 8))
}

function Get-U32([uint32]$Address) {
    $offset = [uint64]$Address - [uint64]$BaseAddress
    if ($Address -lt $BaseAddress -or $offset + 4 -gt [uint64]$bytes.Length) { return $null }
    return [uint32]([uint32]$bytes[$offset] -bor
        ([uint32]$bytes[$offset + 1] -shl 8) -bor
        ([uint32]$bytes[$offset + 2] -shl 16) -bor
        ([uint32]$bytes[$offset + 3] -shl 24))
}

function Format-Opcode([string]$Format, [uint16]$Opcode, [uint32]$Pc) {
    $n = ($Opcode -shr 8) -band 0xF
    $m = ($Opcode -shr 4) -band 0xF
    $imm4 = $Opcode -band 0xF
    $imm8 = $Opcode -band 0xFF
    $signed8 = if (($imm8 -band 0x80) -ne 0) { $imm8 - 0x100 } else { $imm8 }
    $imm12 = $Opcode -band 0xFFF
    $signed12 = if (($imm12 -band 0x800) -ne 0) { $imm12 - 0x1000 } else { $imm12 }
    $pcDispW = [uint32]($Pc + 4 + $imm8 * 2)
    $pcDispD = [uint32](($Pc -band 0xFFFFFFFC) + 4 + $imm8 * 4)
    $branch8 = [uint32]([int64]$Pc + 4 + $signed8 * 2)
    $branch12 = [uint32]([int64]$Pc + 4 + $signed12 * 2)

    $replacements = [ordered]@{
        '<FREG_M_SD_F>' = "FR$m"
        '<GBRdisp8dw>' = ('0x{0:X}' -f ($imm8 * 4))
        '<GBRdisp8w>' = ('0x{0:X}' -f ($imm8 * 2))
        '<GBRdisp8b>' = ('0x{0:X}' -f $imm8)
        '<PCdisp8d>' = ('0x{0:X8}' -f $pcDispD)
        '<PCdisp8w>' = ('0x{0:X8}' -f $pcDispW)
        '<simm8hex>' = if ($signed8 -lt 0) { ('-0x{0:X}' -f (-$signed8)) } else { ('0x{0:X}' -f $signed8) }
        '<disp8dw>' = ('0x{0:X}' -f ($imm8 * 4))
        '<disp8w>' = ('0x{0:X}' -f ($imm8 * 2))
        '<disp8b>' = ('0x{0:X}' -f $imm8)
        '<disp4dw>' = ('0x{0:X}' -f ($imm4 * 4))
        '<disp4w>' = ('0x{0:X}' -f ($imm4 * 2))
        '<disp4b>' = ('0x{0:X}' -f $imm4)
        '<bdisp12>' = ('0x{0:X8}' -f $branch12)
        '<bdisp8>' = ('0x{0:X8}' -f $branch8)
        '<FREG_0>' = 'FR0'
        '<FREG_N>' = "FR$n"
        '<FREG_M>' = "FR$m"
        '<REG_N>' = "R$n"
        '<REG_M>' = "R$m"
        '<RM_BANK>' = ('R{0}_BANK' -f ($m -band 7))
        '<DR_N>' = ('DR{0}' -f ($n -shr 1))
        '<FV_N>' = ('FV{0}' -f (($n -band 0xC) -shr 2))
        '<FV_M>' = ('FV{0}' -f (($m -band 0xC) -shr 2))
        '<imm8>' = ('0x{0:X}' -f $imm8)
        '<simm8>' = [string]$signed8
    }
    $text = $Format
    foreach ($replacement in $replacements.GetEnumerator()) {
        $text = $text.Replace($replacement.Key, $replacement.Value)
    }
    return [pscustomobject]@{ Text = $text; PcDispW = $pcDispW; PcDispD = $pcDispD }
}

for ($pc64 = [uint64]$StartAddress; $pc64 -lt [uint64]$StartAddress + [uint64]$ByteCount; $pc64 += 2) {
    $pc = [uint32]$pc64
    $opcode = Get-U16 $pc
    $entry = $null
    foreach ($candidate in $entries) {
        if (($opcode -band $candidate.Mask) -eq $candidate.Value) {
            # BuildOpcodeTables() allows later entries to replace earlier ones.
            $entry = $candidate
        }
    }
    if ($null -eq $entry) {
        '{0:X8}: {1:X4}  missing' -f $pc, $opcode
        continue
    }
    $formatted = Format-Opcode $entry.Format $opcode $pc
    $literal = ''
    if ($entry.Format.Contains('<PCdisp8w>')) {
        $value = Get-U16 $formatted.PcDispW
        if ($null -ne $value) { $literal = ' ; [0x{0:X8}]=0x{1:X4}' -f $formatted.PcDispW, $value }
    } elseif ($entry.Format.Contains('<PCdisp8d>')) {
        $value = Get-U32 $formatted.PcDispD
        if ($null -ne $value) { $literal = ' ; [0x{0:X8}]=0x{1:X8}' -f $formatted.PcDispD, $value }
    }
    '{0:X8}: {1:X4}  {2,-36} ; issue={3} latency={4}{5}' -f `
        $pc, $opcode, $formatted.Text, $entry.IssueCycles, $entry.LatencyCycles, $literal
}
