[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$GoodCache,
    [string]$BuildDir = 'build-vita',
    [string]$Target = '39829e00451207db324731441501004d',
    [string]$ReplayConfig = 'build-vita/workloads/marvel-vs-capcom-2/experiments/native-reject.cfg',
    [string]$ValidationConfig = 'build-vita/workloads/marvel-vs-capcom-2/experiments/native-reject-validate.cfg',
    [string]$Mutator = 'build-vita/test-mvc2-native-cache.exe',
    [string]$RunPrefix = 'mvc2-native-reject',
    [uint64]$ExpectedFirstRtc = 1988906533,
    [int]$ExpectedGuestRenders = -1,
    [ValidateSet('bad-build','truncate','bad-offset','bad-page','bad-native')]
    [string[]]$Faults = @('bad-build','truncate','bad-offset','bad-page','bad-native')
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$good = (Resolve-Path -LiteralPath (Join-Path $root $GoodCache)).Path
$ctrl = 'C:\Program Files (x86)\SCE\PSP2\Tools\Target Manager Server\bin\psp2ctrl.exe'
$targetCache = 'ux0:data/flycast/data/mvc2_native_cache.bin'
$workload = Join-Path $BuildDir 'workloads/marvel-vs-capcom-2/experiments'
New-Item -ItemType Directory -Path (Join-Path $root $workload) -Force | Out-Null
$mutatorPath = (Resolve-Path -LiteralPath (Join-Path $root $Mutator)).Path
if ($RunPrefix -notmatch '^[A-Za-z0-9._-]+$' -or $ExpectedFirstRtc -eq 0) { throw 'Explicit test identity required' }
$validationText = Get-Content -LiteralPath (Join-Path $root $ValidationConfig) -Raw
if ($validationText -notmatch '(?m)^Mvc2NativeCacheValidate\s*=\s*yes\s*$') { throw 'Native-byte corruption test requires regeneration validation' }
function Install-Cache([string]$source) {
    $connected = $false
    for ($attempt = 0; $attempt -lt 6; ++$attempt) {
        & $ctrl force-disconnect $Target | Out-Null
        Start-Sleep -Milliseconds 1000
        & $ctrl default $Target | Out-Null
        & $ctrl connect $Target | Out-Null
        if ($LASTEXITCODE -eq 0) { $connected = $true; break }
    }
    if (-not $connected) { throw 'Target connection failed' }
    try {
        & $ctrl cp $source $targetCache
        if ($LASTEXITCODE -ne 0) { throw 'Cache copy failed' }
    } finally { & $ctrl disconnect $Target | Out-Null }
}
try {
    foreach ($fault in $Faults) {
        $mutated = Join-Path $root "$workload/native-$fault.bin"
        & $mutatorPath $good "--$fault" $mutated
        if ($LASTEXITCODE -ne 0) { throw 'Cache mutation failed' }
        Install-Cache $mutated
        # Mutated executable bytes MUST be checked by regeneration before use.
        $config = if ($fault -eq 'bad-native') { $ValidationConfig } else { $ReplayConfig }
        $runName = "$RunPrefix-$fault"
        & (Join-Path $PSScriptRoot 'measure-mvc2.ps1') -RunName $runName -Configs $config -Labels $fault -BuildDir $BuildDir -Target $Target -ObservationSeconds 45 -ExpectedFirstRtc $ExpectedFirstRtc -ExpectedGuestRenders $ExpectedGuestRenders | Out-Host
        $latest = Get-ChildItem -LiteralPath (Join-Path $root "$BuildDir/devkit-logs/$runName") -Directory | Sort-Object Name | Select-Object -Last 1
        $log = Get-Content -LiteralPath (Join-Path $latest.FullName "1-$fault.log") -Raw
        $last = ([regex]::Split($log, '(?m)(?=^\d\d:\d\d:\d\d\d .*Auto-starting content ux0:data/marvel/)'))[-1]
        $expected = switch ($fault) {
            'bad-build' { 'cache rejected \(build/layout/config\)' }
            'truncate' { 'cache rejected \(records/checksum\)' }
            'bad-offset' { 'cache rejected \(records/checksum\)' }
            'bad-page' { 'report hits=0 misses=\d+ source_reject=[1-9]\d* .*failures=0' }
            'bad-native' { 'regeneration mismatch pc=.*cache disabled' }
        }
        if ($last -notmatch $expected) { throw "Rejection guard not observed: $fault" }
        Write-Host "PASS: $fault rejected; ordinary JIT completed the workload"
    }
} finally { Install-Cache $good }
