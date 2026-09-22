[CmdletBinding()]
param(
    [string]$BuildDir = 'build-vita-cache-normal',
    [Parameter(Mandatory)] [string]$PortableCache,
    [string]$Config = 'scripts/mvc2-pal-beta-collector.cfg',
    [string]$Version = 'beta6-portable2-return01-canonicalskip-r2',
    [string]$Output = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$rootPrefix = $root.TrimEnd([IO.Path]::DirectorySeparatorChar) +
    [IO.Path]::DirectorySeparatorChar

function Resolve-RepoFile([string]$Path) {
    $candidate = if ([IO.Path]::IsPathRooted($Path)) {
        [IO.Path]::GetFullPath($Path)
    } else {
        [IO.Path]::GetFullPath((Join-Path $root $Path))
    }
    if (-not $candidate.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Required repository file is missing or outside the repository: $candidate"
    }
    return $candidate
}

function Get-SfoInt32Value([string]$Path, [string]$Key) {
    [byte[]]$bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 20 -or $bytes[0] -ne 0x00 -or
            $bytes[1] -ne 0x50 -or $bytes[2] -ne 0x53 -or
            $bytes[3] -ne 0x46) {
        throw "Invalid PARAM.SFO header: $Path"
    }
    $keyTableOffset = [BitConverter]::ToUInt32($bytes, 8)
    $dataTableOffset = [BitConverter]::ToUInt32($bytes, 12)
    $entryCount = [BitConverter]::ToUInt32($bytes, 16)
    for ($index = 0; $index -lt $entryCount; ++$index) {
        $entryOffset = 20 + 16 * $index
        if ($entryOffset + 16 -gt $bytes.Length) {
            throw "Truncated PARAM.SFO index table: $Path"
        }
        $keyOffset = [BitConverter]::ToUInt16($bytes, $entryOffset)
        $dataFormat = [BitConverter]::ToUInt16($bytes, $entryOffset + 2)
        $dataLength = [BitConverter]::ToUInt32($bytes, $entryOffset + 4)
        $dataOffset = [BitConverter]::ToUInt32($bytes, $entryOffset + 12)
        $keyStart = [int64]$keyTableOffset + $keyOffset
        if ($keyStart -ge $bytes.Length) {
            throw "Invalid PARAM.SFO key offset: $Path"
        }
        $keyEnd = $keyStart
        while ($keyEnd -lt $bytes.Length -and $bytes[$keyEnd] -ne 0) {
            ++$keyEnd
        }
        if ($keyEnd -ge $bytes.Length) {
            throw "Unterminated PARAM.SFO key: $Path"
        }
        $entryKey = [Text.Encoding]::UTF8.GetString(
            $bytes, [int]$keyStart, [int]($keyEnd - $keyStart))
        if ($entryKey -eq $Key) {
            if ($dataFormat -ne 0x0404 -or $dataLength -lt 4) {
                throw "PARAM.SFO key '$Key' is not a 32-bit integer: $Path"
            }
            $valueOffset = [int64]$dataTableOffset + $dataOffset
            if ($valueOffset + 4 -gt $bytes.Length) {
                throw "Invalid PARAM.SFO data offset for '$Key': $Path"
            }
            return [BitConverter]::ToInt32($bytes, [int]$valueOffset)
        }
    }
    throw "PARAM.SFO is missing required key '$Key': $Path"
}

$build = if ([IO.Path]::IsPathRooted($BuildDir)) {
    [IO.Path]::GetFullPath($BuildDir)
} else {
    [IO.Path]::GetFullPath((Join-Path $root $BuildDir))
}
$buildPrefix = $build.TrimEnd([IO.Path]::DirectorySeparatorChar) +
    [IO.Path]::DirectorySeparatorChar
if (-not $build.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $build -PathType Container)) {
    throw "Build directory is missing or outside the repository: $build"
}
$portable = Resolve-RepoFile $PortableCache
$configPath = Resolve-RepoFile $Config
$eboot = Resolve-RepoFile (Join-Path $build 'eboot.bin')
$elf = Resolve-RepoFile (Join-Path $build 'flycast')
$vpk = Resolve-RepoFile (Join-Path $build 'Flycast.vpk')
$sfo = Resolve-RepoFile (Join-Path $build 'sce_sys/param.sfo')
$inspector = Resolve-RepoFile 'build-vita-tools/test-mvc2-native-cache.exe'
if ((Get-SfoInt32Value -Path $sfo -Key 'ATTRIBUTE2') -ne 12) {
    throw "Collector PARAM.SFO must contain ATTRIBUTE2=12: $sfo"
}

if ($Version -notmatch '^[A-Za-z0-9._-]+$') {
    throw 'Version must contain only letters, digits, dot, underscore or dash'
}
$outputPath = if ($Output) {
    if ([IO.Path]::IsPathRooted($Output)) {
        [IO.Path]::GetFullPath($Output)
    } else {
        [IO.Path]::GetFullPath((Join-Path $root $Output))
    }
} else {
    Join-Path $build "mvc2-beta-collector-$Version.zip"
}
if (-not $outputPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Output must stay inside the repository: $outputPath"
}
if (Test-Path -LiteralPath $outputPath) {
    throw "Refusing to overwrite existing archive: $outputPath"
}

$configText = Get-Content -LiteralPath $configPath -Raw
$requiredConfig = @(
    '(?m)^Dreamcast\.AutoStartContent\s*=\s*ux0:data/marvel/Marvel vs\. Capcom 2 v1\.001 \(2000\)\(Capcom\)\(NTSC\)\(US\)\[!\]\.gdi\s*$',
    '(?m)^Dynarec\.smcChecks\s*=\s*1\s*$',
    '(?m)^Mvc2NativeCache\s*=\s*no\s*$',
    '(?m)^Mvc2AotPack\s*=\s*no\s*$',
    '(?m)^Mvc2PortableCache\s*=\s*yes\s*$',
    '(?m)^Mvc2PortableWarmBudgetUs\s*=\s*2000\s*$',
    '(?m)^Mvc2PortableInitialWarmBudgetUs\s*=\s*4000000\s*$',
    '(?m)^Mvc2PortableWarmMaxRenders\s*=\s*45\s*$',
    '(?m)^Mvc2NativeCacheRecord\s*=\s*no\s*$',
    '(?m)^Mvc2NativeCacheMerge\s*=\s*no\s*$',
    '(?m)^Mvc2PortableCacheRecord\s*=\s*yes\s*$',
    '(?m)^Mvc2PortableTrainingSourceLimitBytes\s*=\s*10485760\s*$',
    '(?m)^Mvc2PortableExitOnSaturation\s*=\s*yes\s*$',
    '(?m)^Mvc2PunchThroughMultiDraw\s*=\s*yes\s*$',
    '(?m)^rend\.WideScreen\s*=\s*no\s*$',
    '(?m)^rend\.WidescreenGameHacks\s*=\s*no\s*$',
    '(?m)^rend\.ScreenStretching\s*=\s*100\s*$'
)
foreach ($pattern in $requiredConfig) {
    if ($configText -notmatch $pattern) {
        throw "Collector config is missing required setting: $pattern"
    }
}

$inspection = (& $inspector $portable | Out-String)
if ($LASTEXITCODE -ne 0 -or
        $inspection -notmatch 'portable_version=2 records=(\d+).*with_source=(\d+) source_bytes=(\d+).*duplicate_variants=0') {
    throw 'Portable cache failed format validation'
}
$portableRecords = $Matches[1]
$portableSourceRecords = $Matches[2]
$portableSourceBytes = $Matches[3]
$readelf = (Get-Command arm-vita-eabi-readelf -ErrorAction Stop).Source
$buildNote = (& $readelf -n $elf | Out-String)
if ($LASTEXITCODE -ne 0 -or $buildNote -notmatch 'Build ID:\s*([0-9a-f]{40})') {
    throw 'Could not read the Flycast GNU build ID'
}
$buildId = $Matches[1]

$hashes = @{}
foreach ($item in @($vpk, $eboot, $elf, $sfo, $configPath, $portable)) {
    $hashes[$item] = (Get-FileHash -Algorithm SHA256 -LiteralPath $item).Hash
}
$stage = Join-Path $build ('.mvc2-package-' + [Guid]::NewGuid().ToString('N'))
if (-not $stage.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($stage) -notmatch '^\.mvc2-package-[0-9a-f]{32}$') {
    throw "Unsafe staging path: $stage"
}

try {
    New-Item -ItemType Directory -Path $stage | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $stage 'sce_sys') | Out-Null
    Copy-Item -LiteralPath $vpk -Destination (Join-Path $stage "Flycast-MVC2-$Version.vpk")
    Copy-Item -LiteralPath $eboot -Destination (Join-Path $stage 'eboot.bin')
    Copy-Item -LiteralPath $elf -Destination (Join-Path $stage 'flycast.elf')
    Copy-Item -LiteralPath $sfo -Destination (Join-Path $stage 'sce_sys/param.sfo')
    Copy-Item -LiteralPath $configPath -Destination (Join-Path $stage 'emu.cfg')
    Copy-Item -LiteralPath $portable -Destination (Join-Path $stage 'mvc2_portable_cache.bin')

    $readme = @"
Marvel vs. Capcom 2 - Flycast Vita portable cache collector $Version
Build ID: $buildId
Portable coverage: $portableRecords SH4 source records
Stored writable variants: $portableSourceRecords records / $portableSourceBytes bytes

Required game image
  ux0:data/marvel/Marvel vs. Capcom 2 v1.001 (2000)(Capcom)(NTSC)(US)[!].gdi

What is build-independent
- mvc2_portable_cache.bin contains guest SH4 identity plus bounded raw SH4
  bytes for dynamic writable variants. It contains no ARM code, host address
  or Flycast build ID.
- A changed Flycast executable recompiles those entries with its current
  dynarec. Gameplay does not need to be replayed merely because Flycast changed.
- mvc2_portable_training.bin is an executable-independent incremental shard.
  Multiple tester shards are merged into the portable coverage file offline.

Install
1. Back up any existing mvc2_portable_training.bin and flycast.log. If that
   shard was already returned and merged into this package's canonical cache,
   remove it from the Vita before first launch; otherwise return it for merge
   before replacing it.
2. Install Flycast-MVC2-$Version.vpk.
3. Copy emu.cfg to ux0:data/flycast/emu.cfg.
4. Copy mvc2_portable_cache.bin to
   ux0:data/flycast/data/mvc2_portable_cache.bin.

For a loose SELF launch, keep eboot.bin and the supplied sce_sys directory in
the same directory. sce_sys/param.sfo contains ATTRIBUTE2=12.

Test procedure
- Start through the supplied emu.cfg; it cold-boots the exact ROM.
- Play varied teams, stages, opponents and between-fight transitions.
- Portable prewarm is limited to 2 ms in intervals below 46 guest renders.
  Stable 50 Hz fights do no background compilation. A single block can
  slightly exceed the soft time budget during an eligible transition.
- The MVC2 build forces the original 4:3 presentation and disables the
  widescreen game-memory cheat, including when an older config enabled it.
- The portable v2 shard is checkpointed atomically every 120 seconds and on a
  clean stop/reset. Existing on-disk data must match the loaded checkpoint and
  a save that would shrink it is refused.
- Collection stops at 100,000 records or 10 MiB of stored SH4 source. At
  either limit Flycast performs an immediate final checkpoint and closes only
  after that save succeeds. Copy the shard before starting the collector again.
- The 10 MiB limit counts only stored SH4 source, not the whole file. Each
  record also has fixed metadata, so the 100,000-record limit can close the app
  while mvc2_portable_training.bin is still well below 10 MiB. Do not use the
  file size as a capacity meter; the automatic exit is the completion signal.
- If the app has not closed by itself, after the last scene either exit Flycast
  normally or leave it running for at least 140 seconds.

Return these files
  ux0:data/flycast/data/mvc2_portable_training.bin
  ux0:data/flycast/data/flycast.log
  any crash XML/core dump or savestate reproducing a visible stutter
  a short note with stage, teams and transitions tested

Safety retained
- Dynarec SMC checks: Reduced
- writable-SMC source variants: exact guest-byte fingerprint
- portable file bounds, game identity and payload/header checksums
- ASLR disabled; sce_sys/param.sfo ATTRIBUTE2=12

This beta collects coverage. It is not evidence that every scene is full speed.

Artifact SHA256
  Flycast-MVC2-$Version.vpk
    $($hashes[$vpk])
  eboot.bin
    $($hashes[$eboot])
  flycast.elf
    $($hashes[$elf])
  sce_sys/param.sfo
    $($hashes[$sfo])
  emu.cfg
    $($hashes[$configPath])
  mvc2_portable_cache.bin
    $($hashes[$portable])
"@
    Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Value $readme -Encoding ASCII
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $outputPath -CompressionLevel Optimal
} finally {
    if (Test-Path -LiteralPath $stage) {
        $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
        if (-not $resolvedStage.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase) -or
                [IO.Path]::GetFileName($resolvedStage) -notmatch '^\.mvc2-package-[0-9a-f]{32}$') {
            throw "Refusing to remove unsafe staging path: $resolvedStage"
        }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}

$archiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $outputPath).Hash
Write-Host "Package: $outputPath"
Write-Host "SHA256:  $archiveHash"
Write-Host $inspection.Trim()
