[CmdletBinding()]
param(
    [string]$BuildDir = 'build-vita',
    [string]$OutputDir,
    [ValidateSet('pc-sample', 'pft')]
    [string]$Mode = 'pc-sample',
    [ValidateRange(1000, 300000)]
    [int]$LengthMs = 20000,
    [ValidateRange(1000, 900000)]
    [int]$ListenTimeoutMs = 180000,
    [ValidateRange(1, 1000)]
    [int]$Top = 50,
    [switch]$NoTargetTrigger
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-FromRoot {
    param([string]$Path, [string]$Root)
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $Root $Path))
}

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedBuild = Resolve-FromRoot -Path $BuildDir -Root $root
$elf = Join-Path $resolvedBuild 'flycast'
if (-not (Test-Path -LiteralPath $elf -PathType Leaf)) {
    throw "Missing unstripped ELF: $elf"
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $resolvedBuild ('devkit-logs\occam\' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
}
$resolvedOutput = Resolve-FromRoot -Path $OutputDir -Root $root
New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null

$sceRoot = $env:SCE_ROOT_DIR
if (-not $sceRoot) {
    $sceRoot = 'C:\Program Files (x86)\SCE'
}
$occam = Join-Path $sceRoot 'PSP2\Tools\Razor\bin\PSP2Occam.exe'
if (-not (Test-Path -LiteralPath $occam -PathType Leaf)) {
    throw "PSP2Occam not found at $occam"
}

# Preserve the exact ELF used for symbolization. Never analyze a capture with a
# newer build that merely happens to have the same path.
$snapshotElf = Join-Path $resolvedOutput 'flycast.elf'
Copy-Item -LiteralPath $elf -Destination $snapshotElf -Force
$capture = Join-Path $resolvedOutput 'cpu-trace.rzr'
$xml = Join-Path $resolvedOutput 'cpu-trace.xml'

$captureArgs = @(
    'cpu-trace',
    $capture,
    "--length=$LengthMs",
    "--mode=$Mode",
    "--elf=$snapshotElf",
    '--take-screenshots=no',
    '--wrap-buffer=yes'
)
if (-not $NoTargetTrigger) {
    $captureArgs += '--listen=start'
    $captureArgs += "--listen-timeout=$ListenTimeoutMs"
}

Write-Host "[occam] Waiting for $Mode capture -> $capture"
& $occam @captureArgs
if ($LASTEXITCODE -ne 0) {
    throw "PSP2Occam capture failed with exit code $LASTEXITCODE"
}
& $occam export $capture $xml '--format=xml'
if ($LASTEXITCODE -ne 0) {
    throw "PSP2Occam export failed with exit code $LASTEXITCODE"
}

if ($Mode -eq 'pc-sample') {
    & (Join-Path $PSScriptRoot 'analyze-occam-hits.ps1') `
        -XmlPath $xml -ElfPath $snapshotElf -Top $Top
}
Write-Host "[occam] Capture artifacts: $resolvedOutput"
