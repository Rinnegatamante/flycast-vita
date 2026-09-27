[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$XmlPath,
    [Parameter(Mandatory = $true)] [string]$JitMapPath,
    [Parameter(Mandatory = $true)] [string]$ElfPath,
    [string]$OutputPath,
    [ValidateRange(1, 1000)] [int]$Top = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-Input([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path (Join-Path $PSScriptRoot '..') $Path))
}

$xml = Resolve-Input $XmlPath
$jitMap = Resolve-Input $JitMapPath
$elf = Resolve-Input $ElfPath
foreach ($path in @($xml, $jitMap, $elf)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing input: $path" }
}

$nm = (Get-Command arm-vita-eabi-nm -ErrorAction Stop).Source
$jitSymbol = @(& $nm -n -S -C --defined-only $elf | Where-Object {
    $_ -match '^([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})\s+[tTwW]\s+SH4_TCB$'
} | Select-Object -First 1)
if ($jitSymbol.Count -ne 1 -or $jitSymbol[0] -notmatch '^([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})') {
    throw "SH4_TCB symbol not found in $elf"
}
[uint64]$codeStart = [Convert]::ToUInt64($Matches[1], 16)
[uint64]$codeEnd = $codeStart + [Convert]::ToUInt64($Matches[2], 16)

$blocks = @(Import-Csv -LiteralPath $jitMap | ForEach-Object {
    [uint64]$start = [Convert]::ToUInt64($_.host_rx, 16)
    [pscustomobject]@{
        Start = $start
        End = $start + [uint64]$_.host_bytes
        GuestPc = $_.guest_pc.ToUpperInvariant()
    }
} | Sort-Object Start)
if ($blocks.Count -eq 0) { throw "Empty JIT map: $jitMap" }
for ($i = 1; $i -lt $blocks.Count; ++$i) {
    if ($blocks[$i - 1].End -gt $blocks[$i].Start) {
        throw ('Overlapping JIT ranges at 0x{0:X8}' -f $blocks[$i].Start)
    }
}

$byGuest = @{}
[int64]$matched = 0
[int64]$unmatched = 0
[int64]$outside = 0
$reader = [IO.StreamReader]::new($xml)
try {
    while (($line = $reader.ReadLine()) -ne $null) {
        if ($line -match '<HitCount Address="0x([0-9a-fA-F]+)" HitCount="(\d+)"') {
            [uint64]$address = [Convert]::ToUInt64($Matches[1], 16)
            [int64]$samples = [int64]$Matches[2]
            if ($address -lt $codeStart -or $address -ge $codeEnd) {
                $outside += $samples
                continue
            }
            $low = 0
            $high = $blocks.Count - 1
            $index = -1
            while ($low -le $high) {
                $mid = ($low + $high) -shr 1
                if ($blocks[$mid].Start -le $address) {
                    $index = $mid
                    $low = $mid + 1
                } else {
                    $high = $mid - 1
                }
            }
            if ($index -lt 0 -or $address -ge $blocks[$index].End) {
                $unmatched += $samples
                continue
            }
            $matched += $samples
            $guestPc = $blocks[$index].GuestPc
            if ($byGuest.ContainsKey($guestPc)) {
                $byGuest[$guestPc] += $samples
            } else {
                $byGuest[$guestPc] = $samples
            }
        }
        if ($line -match '</HitCounts>') { break }
    }
} finally {
    $reader.Dispose()
}
if ($matched + $unmatched -eq 0) { throw "No samples in SH4_TCB range" }

$totalJit = $matched + $unmatched
$ranking = @($byGuest.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object {
    [pscustomobject]@{
        guest_pc = $_.Key
        samples = $_.Value
        percent_of_jit = [math]::Round(100 * $_.Value / $totalJit, 4)
    }
})
[pscustomobject]@{
    code_range = ('0x{0:X8}..0x{1:X8}' -f $codeStart, $codeEnd)
    map_blocks = $blocks.Count
    matched_samples = $matched
    unmatched_samples = $unmatched
    coverage_percent = [math]::Round(100 * $matched / $totalJit, 2)
    outside_samples = $outside
} | Format-List
$ranking | Select-Object -First $Top | Format-Table -AutoSize

if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $output = Resolve-Input $OutputPath
    $parent = Split-Path -Parent $output
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $ranking | Export-Csv -LiteralPath $output -NoTypeInformation
    Write-Host "[jit-samples] Ranked guest PCs: $output"
}
