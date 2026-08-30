[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$XmlPath,
    [string]$ElfPath = 'build-vita\flycast',
    [ValidateRange(1, 1000)]
    [int]$Top = 40
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedXml = if ([IO.Path]::IsPathRooted($XmlPath)) {
    [IO.Path]::GetFullPath($XmlPath)
} else {
    [IO.Path]::GetFullPath((Join-Path $root $XmlPath))
}
$resolvedElf = if ([IO.Path]::IsPathRooted($ElfPath)) {
    [IO.Path]::GetFullPath($ElfPath)
} else {
    [IO.Path]::GetFullPath((Join-Path $root $ElfPath))
}

if (-not (Test-Path -LiteralPath $resolvedXml -PathType Leaf)) {
    throw "Missing Occam XML export: $resolvedXml"
}
if (-not (Test-Path -LiteralPath $resolvedElf -PathType Leaf)) {
    throw "Missing symbol ELF: $resolvedElf"
}

$nm = Get-Command arm-vita-eabi-nm -ErrorAction Stop
$symbols = @(
    foreach ($line in (& $nm.Source -n -S -C --defined-only $resolvedElf)) {
        if ($line -match '^([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})\s+([tTwW])\s+(.+)$') {
            [pscustomobject]@{
                Address = [Convert]::ToUInt64($Matches[1], 16)
                Size = [Convert]::ToUInt64($Matches[2], 16)
                Name = $Matches[4]
            }
        }
    }
)
if ($symbols.Count -eq 0) {
    throw "No text symbols found in $resolvedElf"
}

# Occam's XML can be tens of MiB. HitCounts is the first compact section, so
# stop streaming as soon as it closes instead of materialising the full export.
$hits = [Collections.Generic.List[object]]::new()
$reader = [IO.StreamReader]::new($resolvedXml)
try {
    while (($line = $reader.ReadLine()) -ne $null) {
        if ($line -match '<HitCount Address="0x([0-9a-fA-F]+)" HitCount="(\d+)"') {
            $hits.Add([pscustomobject]@{
                Address = [Convert]::ToUInt64($Matches[1], 16)
                Count = [int64]$Matches[2]
            })
        }
        if ($line -match '</HitCounts>') {
            break
        }
    }
} finally {
    $reader.Dispose()
}
if ($hits.Count -eq 0) {
    throw "No CPU HitCounts found in $resolvedXml"
}

$imageStart = $symbols[0].Address
$imageEnd = [uint64](($symbols | ForEach-Object { $_.Address + $_.Size } |
        Measure-Object -Maximum).Maximum)
$imageHits = @($hits | Where-Object {
        $_.Address -ge $imageStart -and $_.Address -lt $imageEnd
    } | Sort-Object Address)
$total = ($imageHits | Measure-Object Count -Sum).Sum
if ($total -eq 0) {
    throw ('No samples fall in ELF range 0x{0:X8}..0x{1:X8}' -f $imageStart, $imageEnd)
}

$aggregate = @{}
$symbolIndex = 0
foreach ($hit in $imageHits) {
    while ($symbolIndex + 1 -lt $symbols.Count -and
        $symbols[$symbolIndex + 1].Address -le $hit.Address) {
        $symbolIndex++
    }
    $symbol = $symbols[$symbolIndex]
    $name = if ($hit.Address -ge $symbol.Address -and
        $hit.Address -lt $symbol.Address + $symbol.Size) {
        $symbol.Name
    } else {
        '<unmapped app PC>'
    }
    if ($aggregate.ContainsKey($name)) {
        $aggregate[$name] += $hit.Count
    } else {
        $aggregate[$name] = $hit.Count
    }
}

Write-Host ('ELF range: 0x{0:X8}..0x{1:X8}' -f $imageStart, $imageEnd)
Write-Host "Samples in ELF range: $total"
$aggregate.GetEnumerator() | ForEach-Object {
    [pscustomobject]@{
        Samples = $_.Value
        Percent = [math]::Round(100.0 * $_.Value / $total, 2)
        Function = $_.Key
    }
} | Sort-Object Samples -Descending | Select-Object -First $Top
