[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$XmlPath,
    [string]$ProcessName = 'FLYCASTDC'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$path = if ([IO.Path]::IsPathRooted($XmlPath)) { $XmlPath } else { Join-Path $root $XmlPath }
$document = [xml](Get-Content -LiteralPath $path -Raw)
$capture = $document.RazorExport.CpuCapture
$duration = [double]$capture.CaptureLength
if ($duration -le 0) { throw 'Missing positive capture duration' }
$processes = @($capture.Processes.Process | Where-Object ProcessName -eq $ProcessName)
if ($processes.Count -eq 0) { throw "Process not found: $ProcessName" }
$rows = foreach ($process in $processes) {
    foreach ($thread in $process.Threads.Thread) {
        $bars = @($thread.ThreadBars.ThreadBar | Where-Object StartThreadIncidentId -eq '0x0' |
            Sort-Object { [long]$_.Start })
        [long]$running = 0
        $byCore = @{}
        $previousCore = $null
        $migrations = 0
        foreach ($bar in $bars) {
            $ticks = [long]$bar.Stop - [long]$bar.Start
            if ($ticks -lt 0) { throw 'Negative thread execution interval' }
            $running += $ticks
            $core = [string]$bar.CpuCoreId
            $byCore[$core] += $ticks
            if ($null -ne $previousCore -and $previousCore -ne $core) { ++$migrations }
            $previousCore = $core
        }
        $cores = @($byCore.Keys | Sort-Object | ForEach-Object {
            '{0}:{1:F3}' -f $_, ($byCore[$_] / $duration)
        }) -join ' '
        [pscustomobject]@{
            Thread = $thread.ThreadName
            ThreadId = $thread.ThreadId
            # Same capture timestamp units in numerator and denominator; do
            # not infer physical CPU MHz from the capture's clock metadata.
            CoreEquivalent = [Math]::Round($running / $duration, 4)
            CoreFractions = $cores
            RunningIntervals = $bars.Count
            ObservedMigrations = $migrations
        }
    }
}
$rows | Sort-Object CoreEquivalent -Descending
