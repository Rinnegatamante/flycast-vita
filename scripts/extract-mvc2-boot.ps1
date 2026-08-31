[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$GdiPath,
    [Parameter(Mandatory)] [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$gdi = (Resolve-Path -LiteralPath $GdiPath).Path
$lines = Get-Content -LiteralPath $gdi
$line = @($lines | Where-Object { $_ -match '^3 45000 4 2352 (.+) 0$' })
if ($line.Count -ne 1 -or $lines[0] -ne '3') { throw 'Expected the verified MVC2 three-track GDI layout' }
$trackName = [regex]::Match($line[0], '^3 45000 4 2352 (.+) 0$').Groups[1].Value.Trim('"')
$track = Join-Path (Split-Path $gdi) $trackName
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
foreach ($name in @('1ST_READ.BIN','IP.BIN','directory.csv')) {
    if (Test-Path -LiteralPath (Join-Path $output $name)) { throw "Refusing to overwrite $name" }
}
$stream = [IO.File]::OpenRead($track)
function Read-Payload([long]$lba, [int]$length) {
    $result = [byte[]]::new($length)
    $sector = [byte[]]::new(2352)
    for ($offset = 0; $offset -lt $length; $offset += 2048) {
        $position = ($lba - 45000 + [long]($offset / 2048)) * 2352
        if ($position -lt 0 -or $position + 2352 -gt $stream.Length) { throw 'Extent outside track' }
        [void]$stream.Seek($position, [IO.SeekOrigin]::Begin)
        if ($stream.Read($sector, 0, 2352) -ne 2352 -or $sector[15] -ne 1) { throw 'Expected complete Mode 1 sector' }
        [Array]::Copy($sector, 16, $result, $offset, [Math]::Min(2048, $length - $offset))
    }
    return ,$result
}
try {
    $ip = Read-Payload 45000 32768
    if ([Text.Encoding]::ASCII.GetString($ip, 64, 16) -ne 'T1212N    V1.001') { throw 'Unexpected disc product/revision' }
    $pvd = Read-Payload 45016 2048
    if ($pvd[0] -ne 1 -or [Text.Encoding]::ASCII.GetString($pvd, 1, 5) -ne 'CD001') { throw 'ISO9660 PVD missing' }
    $rootLba = [BitConverter]::ToUInt32($pvd, 158)
    $rootLength = [BitConverter]::ToUInt32($pvd, 166)
    if ($rootLength -gt 1048576) { throw 'Unexpected root directory size' }
    $directory = Read-Payload $rootLba $rootLength
    $entries = [Collections.Generic.List[object]]::new()
    for ($offset = 0; $offset -lt $directory.Length;) {
        $recordLength = $directory[$offset]
        if ($recordLength -eq 0) { $offset = ([int][Math]::Floor($offset / 2048) + 1) * 2048; continue }
        if ($recordLength -lt 34 -or $offset + $recordLength -gt $directory.Length) { throw 'Invalid ISO directory record' }
        $nameLength = $directory[$offset + 32]
        if ($nameLength -gt $recordLength - 33) { throw 'Invalid ISO name length' }
        if ($nameLength -gt 1) {
            $entries.Add([pscustomobject]@{
                Name = [Text.Encoding]::ASCII.GetString($directory, $offset + 33, $nameLength)
                LBA = [BitConverter]::ToUInt32($directory, $offset + 2)
                Length = [BitConverter]::ToUInt32($directory, $offset + 10)
                Flags = $directory[$offset + 25]
            })
        }
        $offset += $recordLength
    }
    $boot = @($entries | Where-Object Name -eq '1ST_READ.BIN;1')
    if ($boot.Count -ne 1 -or $boot[0].Length -gt 16711680 -or ($boot[0].Flags -band 2)) { throw 'Unexpected boot executable' }
    $bytes = Read-Payload $boot[0].LBA $boot[0].Length
    [IO.File]::WriteAllBytes((Join-Path $output '1ST_READ.BIN'), $bytes)
    [IO.File]::WriteAllBytes((Join-Path $output 'IP.BIN'), $ip)
    $entries | Export-Csv -LiteralPath (Join-Path $output 'directory.csv') -NoTypeInformation
    $boot
    Get-FileHash -LiteralPath $gdi,(Join-Path $output '1ST_READ.BIN'),(Join-Path $output 'IP.BIN')
} finally { $stream.Dispose() }
