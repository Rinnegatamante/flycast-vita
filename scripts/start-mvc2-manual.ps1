[CmdletBinding()]
param(
    [string]$BuildDir = 'build-vita-cache-normal',
    [string]$Config = 'build-vita/workloads/marvel-vs-capcom-2/experiments/manual-pal-coldboot.cfg',
    [string]$Target = '39829e00451207db324731441501004d'
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = (Resolve-Path -LiteralPath (Join-Path $root $BuildDir)).Path
$configPath = (Resolve-Path -LiteralPath (Join-Path $root $Config)).Path
$configText = Get-Content -LiteralPath $configPath -Raw
foreach ($setting in @('Dreamcast.AutoLoadState', 'Dreamcast.AutoSaveState')) {
    if ($configText -notmatch ('(?m)^' + [regex]::Escape($setting) + '\s*=\s*no\s*$')) {
        throw "Manual cold boot requires $setting = no"
    }
}
$eboot = Join-Path $build 'eboot.bin'
$sfo = Join-Path $build 'sce_sys/param.sfo'
if (-not (Test-Path -LiteralPath $eboot) -or -not (Test-Path -LiteralPath $sfo)) {
    throw 'Build and adjacent sce_sys/param.sfo are required. Run build-vita.ps1 first.'
}
$ctrl = 'C:\Program Files (x86)\SCE\PSP2\Tools\Target Manager Server\bin\psp2ctrl.exe'
$runner = 'C:\Program Files (x86)\SCE\PSP2\Tools\Target Manager Server\bin\psp2run.exe'
function Invoke-Ctrl([string[]]$Arguments) {
    & $ctrl @Arguments
    if ($LASTEXITCODE -ne 0) { throw "psp2ctrl failed: $($Arguments -join ' ')" }
}
Invoke-Ctrl @('default', $Target)
Invoke-Ctrl @('connect', $Target)
$processes = Invoke-Ctrl @('plist', $Target)
if (($processes -join "`n") -match '\bFLYCASTDC\b') {
    throw 'Flycast is already running. Manual launch will not terminate it.'
}
$evidence = Join-Path $build ('devkit-logs/mvc2-pal-coldboot/' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $evidence | Out-Null
Invoke-Ctrl @('cp', 'ux0:data/flycast/emu.cfg', (Join-Path $evidence 'target-before.cfg'))
Copy-Item -LiteralPath (Join-Path $build 'flycast') -Destination (Join-Path $evidence 'flycast.elf')
Copy-Item -LiteralPath $eboot, $sfo -Destination $evidence
Invoke-Ctrl @('cp', $configPath, 'ux0:data/flycast/emu.cfg')
$readback = Join-Path $evidence 'launched.cfg'
Invoke-Ctrl @('cp', 'ux0:data/flycast/emu.cfg', $readback)
if ((Get-FileHash -LiteralPath $readback).Hash -ne (Get-FileHash -LiteralPath $configPath).Hash) {
    throw 'Configuration readback mismatch; not launching.'
}
$runArgs = @(
    "/t:$Target", '/console:all',
    ('/log:"' + (Join-Path $evidence 'run.xml') + '"'),
    '/logFormat:xml', '/debug', '/elf', ('"' + $eboot + '"')
)
# Deliberately no timeout, automated input, kill, or disconnect: the user
# controls the match and creates the next state while host file serving lives.
$run = Start-Process -FilePath $runner -ArgumentList $runArgs -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $evidence 'console.txt') `
    -RedirectStandardError (Join-Path $evidence 'psp2run.stderr.txt')
[pscustomobject]@{ HostRunnerPid = $run.Id; Evidence = $evidence; Executable = $eboot }
