[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$RunName,
    [Parameter(Mandatory)] [string[]]$Configs,
    [Parameter(Mandatory)] [string[]]$Labels,
    [ValidateRange(1, 100)] [int]$Iterations = 1,
    [ValidateRange(30, 600)] [int]$ObservationSeconds = 35,
    [string]$BuildDir = 'build-vita',
    [string]$Target = '39829e00451207db324731441501004d',
    [switch]$NoDebug,
    [switch]$PullFrameTrace
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($Configs.Count -ne $Labels.Count -or $Configs.Count -eq 0) { throw 'Config/label count mismatch' }
if ($RunName -notmatch '^[a-zA-Z0-9._-]+$' -or ($Labels | Where-Object { $_ -notmatch '^[a-zA-Z0-9._-]+$' })) { throw 'Invalid run name/label' }
$configPaths = @($Configs | ForEach-Object { (Resolve-Path -LiteralPath (Join-Path $root $_)).Path })
$build = Join-Path $root $BuildDir
$evidence = Join-Path $build ('devkit-logs/' + $RunName + '/' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$ctrl = 'C:\Program Files (x86)\SCE\PSP2\Tools\Target Manager Server\bin\psp2ctrl.exe'
function Connect-Devkit {
    for ($attempt = 0; $attempt -lt 6; ++$attempt) {
        & $ctrl force-disconnect $Target | Out-Null
        Start-Sleep -Milliseconds 1000
        & $ctrl default $Target | Out-Null
        & $ctrl connect $Target | Out-Null
        if ($LASTEXITCODE -eq 0) { return }
    }
    throw 'DevKit connection failed'
}
function Copy-Devkit([string]$source, [string]$destination) {
    & $ctrl cp $source $destination
    if ($LASTEXITCODE -ne 0) { throw "Target copy failed: $source" }
}
$backup = Join-Path $evidence 'target-before.cfg'
Connect-Devkit
try { Copy-Devkit 'ux0:data/flycast/emu.cfg' $backup }
finally { & $ctrl disconnect $Target | Out-Null }
Copy-Item -LiteralPath (Join-Path $build 'flycast') -Destination (Join-Path $evidence 'flycast.elf')
Copy-Item -LiteralPath (Join-Path $build 'flycast.velf'),(Join-Path $build 'eboot.bin'),(Join-Path $build 'sce_sys/param.sfo') -Destination $evidence
$binaryHash = (Get-FileHash -LiteralPath (Join-Path $build 'eboot.bin')).Hash
$results = [Collections.Generic.List[object]]::new()
try {
    for ($pair = 1; $pair -le $Iterations; ++$pair) {
        for ($variant = 0; $variant -lt $configPaths.Count; ++$variant) {
            $label = "$pair-$($Labels[$variant])"
            if ((Get-FileHash -LiteralPath (Join-Path $build 'eboot.bin')).Hash -ne $binaryHash) { throw 'Executable changed during A/B' }
            Connect-Devkit
            try {
                Copy-Devkit $configPaths[$variant] 'ux0:data/flycast/emu.cfg'
                $readback = Join-Path $evidence "$label.cfg"
                Copy-Devkit 'ux0:data/flycast/emu.cfg' $readback
                if ((Get-FileHash -LiteralPath $readback).Hash -ne (Get-FileHash -LiteralPath $configPaths[$variant]).Hash) { throw 'Config mismatch' }
            } finally { & $ctrl disconnect $Target | Out-Null }
            for ($attempt = 0; $attempt -lt 3; ++$attempt) {
                try {
                    & (Join-Path $PSScriptRoot 'devkit-game-loop.ps1') -Target $Target -BuildDir $BuildDir -RunName "$RunName-$label" -Iterations 1 -Timeout ($ObservationSeconds + 30) -ObservationSeconds $ObservationSeconds -Screenshot -NoDebug:$NoDebug
                    break
                } catch {
                    if ($attempt -eq 2 -or $_ -notmatch 'connect ') { throw }
                    Write-Host 'Retrying initial Target Manager connection'
                }
            }
            Connect-Devkit
            $logPath = Join-Path $evidence "$label.log"
            try { Copy-Devkit 'ux0:data/flycast/data/flycast.log' $logPath }
            finally { & $ctrl disconnect $Target | Out-Null }
            $lastRun = ([regex]::Split((Get-Content -LiteralPath $logPath -Raw), '(?m)(?=^\d\d:\d\d:\d\d\d .*Auto-starting content ux0:data/marvel/)'))[-1]
            if ($PullFrameTrace -and (Get-Content -LiteralPath $readback -Raw) -match '(?m)^FrameTrace\s*=\s*yes\s*$') {
                $traceStart = [regex]::Match($lastRun, 'frame-trace: start=(\d+)')
                if (-not $traceStart.Success -or $lastRun -notmatch 'frame-trace: saved .*overflow=0/0 io_error=0') {
                    throw 'Trace missing, overflowed, or not successfully saved'
                }
                $tracePath = Join-Path $evidence "$label-trace.csv"
                Connect-Devkit
                try { Copy-Devkit 'ux0:data/flycast/data/mvc2_frame_trace.csv' $tracePath }
                finally { & $ctrl disconnect $Target | Out-Null }
                if ((Get-Content -LiteralPath $tracePath -TotalCount 1) -notmatch ('origin_us=' + $traceStart.Groups[1].Value + '\b')) {
                    throw 'Stale trace: origin does not match this launch'
                }
                if ((Get-Content -LiteralPath $readback -Raw) -match '(?m)^Mvc2AsyncJit\s*=\s*yes\s*$') {
                    if ($lastRun -notmatch 'mvc2-async: report .*io_error=0') { throw 'Async job report missing' }
                    $jobsPath = Join-Path $evidence "$label-async-jobs.csv"
                    Connect-Devkit
                    try { Copy-Devkit 'ux0:data/flycast/data/mvc2_async_jobs.csv' $jobsPath }
                    finally { & $ctrl disconnect $Target | Out-Null }
                    if ((Get-Content -LiteralPath $jobsPath -TotalCount 1) -notmatch ('origin_us=' + $traceStart.Groups[1].Value + '\b')) {
                        throw 'Stale async job report'
                    }
                }
            }
            $markers = @([regex]::Matches($lastRun, '(?m)^(\d\d):(\d\d):(\d\d\d).*emu-second: rtc=(\d+)'))
            if ($markers.Count -lt 17 -or [long]$markers[0].Groups[4].Value -ne 1988906533 -or [long]$markers[16].Groups[4].Value -ne 1988906549) { throw 'Guest interval mismatch' }
            $times = @($markers[0], $markers[16]) | ForEach-Object { [double]$_.Groups[1].Value*60 + [double]$_.Groups[2].Value + [double]$_.Groups[3].Value/1000 }
            $result = [pscustomobject]@{ Pair=$pair; Variant=$Labels[$variant]; WallSeconds=$times[1]-$times[0]; Ratio=16/($times[1]-$times[0]); Evidence=$logPath }
            $results.Add($result)
            $results | Export-Csv -LiteralPath (Join-Path $evidence 'results.csv') -NoTypeInformation
            Write-Host ('RESULT {0}: wall={1:F3} ratio={2:F9}' -f $label, $result.WallSeconds, $result.Ratio)
        }
    }
} finally {
    Connect-Devkit
    try { Copy-Devkit $backup 'ux0:data/flycast/emu.cfg' }
    finally { & $ctrl disconnect $Target | Out-Null }
}
$results
Write-Host "Evidence: $evidence"
