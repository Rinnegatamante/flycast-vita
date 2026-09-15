[CmdletBinding()]
param(
    [string]$Output = 'build-vita-tools/mvc2-aot-pack.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputPath = if ([IO.Path]::IsPathRooted($Output)) {
    [IO.Path]::GetFullPath($Output)
} else {
    [IO.Path]::GetFullPath((Join-Path $root $Output))
}
$rootPrefix = $root.TrimEnd([IO.Path]::DirectorySeparatorChar) +
    [IO.Path]::DirectorySeparatorChar
if (-not $outputPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Output must stay inside the repository: $outputPath"
}
$compiler = (Get-Command g++ -ErrorAction Stop).Source
$outputDirectory = Split-Path -Parent $outputPath
$inspectorPath = Join-Path $outputDirectory 'test-mvc2-native-cache.exe'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$arguments = @(
    '-std=c++11',
    '-O2',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-pedantic',
    "-I$(Join-Path $root 'core')",
    "-I$(Join-Path $root 'core/deps/xxHash')",
    (Join-Path $root 'scripts/mvc2-aot-pack.cpp'),
    (Join-Path $root 'core/deps/xxHash/xxhash.c'),
    '-o',
    $outputPath
)
Write-Host "[build] > g++ $($arguments -join ' ')"
& $compiler @arguments
if ($LASTEXITCODE -ne 0) {
    throw "mvc2-aot-pack build failed with exit code $LASTEXITCODE"
}
& $outputPath --self-test
if ($LASTEXITCODE -ne 0) {
    throw "mvc2-aot-pack self-test failed with exit code $LASTEXITCODE"
}
$inspectorArguments = @(
    '-std=c++11',
    '-O2',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-pedantic',
    "-I$(Join-Path $root 'core')",
    "-I$(Join-Path $root 'core/deps/xxHash')",
    (Join-Path $root 'scripts/test-mvc2-native-cache.cpp'),
    (Join-Path $root 'core/deps/xxHash/xxhash.c'),
    '-o',
    $inspectorPath
)
Write-Host "[build] > g++ $($inspectorArguments -join ' ')"
& $compiler @inspectorArguments
if ($LASTEXITCODE -ne 0) {
    throw "native-cache inspector build failed with exit code $LASTEXITCODE"
}
& $inspectorPath
if ($LASTEXITCODE -ne 0) {
    throw "native-cache inspector self-test failed with exit code $LASTEXITCODE"
}
Write-Host "[build] AOT tools ready: $outputPath, $inspectorPath"
