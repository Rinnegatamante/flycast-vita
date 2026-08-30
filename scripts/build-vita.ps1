[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$BuildType = 'Release',

    [ValidateRange(1, 128)]
    [int]$Jobs = [Environment]::ProcessorCount,

    [string]$BuildDir = 'build-vita',
    [string]$VitaGLDir = '..\vitaGL',
    [string]$SDLDir = '..\SDL',

    [switch]$BuildVitaGL,
    [switch]$BuildSDL,
    [switch]$Clean,
    [switch]$Symbols,
    [switch]$LTO
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Native {
    param(
        [Parameter(Mandatory)] [string]$Command,
        [Parameter()] [string[]]$Arguments = @()
    )

    Write-Host (('[build] > {0} {1}' -f $Command, ($Arguments -join ' ')).TrimEnd())
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Command"
    }
}

function Resolve-InputPath {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$BasePath
    )

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Assert-Command {
    param([Parameter(Mandatory)] [string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "Required command '$Name' is not on PATH."
    }
    return $command.Source
}

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$rootPrefix = $root.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$buildPath = Resolve-InputPath -Path $BuildDir -BasePath $root
if (-not $buildPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "BuildDir must stay inside the repository: $buildPath"
}

if (-not $env:VITASDK) {
    throw 'VITASDK is not set. Point it at the local VitaSDK installation (for example C:\vitasdk).'
}
$vitaSdk = [IO.Path]::GetFullPath($env:VITASDK)
$toolchain = Join-Path $vitaSdk 'share\vita.toolchain.cmake'
$sdkPrefix = Join-Path $vitaSdk 'arm-vita-eabi'
if (-not (Test-Path -LiteralPath $toolchain -PathType Leaf)) {
    throw "Invalid VITASDK: missing $toolchain"
}

$cmake = Assert-Command 'cmake'
$make = Assert-Command 'make'
$nm = Assert-Command 'arm-vita-eabi-nm'
$ar = Assert-Command 'arm-vita-eabi-ar'

$libchdrSource = Join-Path $root 'core\deps\libchdr\src\libchdr_chd.c'
if (-not (Test-Path -LiteralPath $libchdrSource -PathType Leaf)) {
    throw 'core/deps/libchdr is not initialized. Run: git submodule update --init core/deps/libchdr'
}

$vitaGLSeedPath = Resolve-InputPath -Path $VitaGLDir -BasePath $root
$sdlPath = Resolve-InputPath -Path $SDLDir -BasePath $root
$dependencyBuildRoot = Join-Path $root 'build-vita-deps'
$vitaGLPath = Join-Path $dependencyBuildRoot 'src\vitaGL'
$privatePrefix = Join-Path $dependencyBuildRoot 'prefix'
$sdlBuildPath = Join-Path $dependencyBuildRoot 'SDL'
$vitaGLLibrary = Join-Path $privatePrefix 'lib\libvitaGL.a'
$vitaGLHeader = Join-Path $privatePrefix 'include\vitaGL.h'
$sdlLibrary = Join-Path $sdkPrefix 'lib\libSDL2.a'
$sdlConfig = Join-Path $sdkPrefix 'include\SDL2\SDL_config.h'

$vitaGLFlags = @(
    'NO_DEBUG=1',
    'HAVE_WRAPPED_ALLOCATORS=1',
    'DEPTH_STENCIL_HACK=1',
    'CIRCULAR_VERTEX_POOL=2',
    'HAVE_SHADER_CACHE=1'
)

if ($BuildVitaGL) {
    if (-not (Test-Path -LiteralPath (Join-Path $vitaGLPath '.git') -PathType Container)) {
        if (-not (Test-Path -LiteralPath (Join-Path $vitaGLSeedPath 'Makefile') -PathType Leaf)) {
            throw "No vitaGL seed checkout found at $vitaGLSeedPath"
        }
        $git = Assert-Command 'git'
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $vitaGLPath) | Out-Null
        Write-Host "[build] Creating private vitaGL checkout from $vitaGLSeedPath"
        Invoke-Native $git @('clone', '--local', '--no-hardlinks', $vitaGLSeedPath, $vitaGLPath)
    }

    $vitaGLMakefile = Join-Path $vitaGLPath 'Makefile'
    if (-not (Test-Path -LiteralPath $vitaGLMakefile -PathType Leaf)) {
        throw "Private vitaGL checkout is incomplete at $vitaGLPath"
    }

    $makefileText = Get-Content -Raw -LiteralPath $vitaGLMakefile
    $legacyCircularPool = $makefileText -match 'CIRCULAR_VERTEX_POOL'
    $defaultCircularPool = $makefileText -match 'NO_CIRCULAR_POOL'
    if (-not $legacyCircularPool -and -not $defaultCircularPool) {
        throw 'This vitaGL checkout exposes neither CIRCULAR_VERTEX_POOL nor the newer default circular pool.'
    }
    if (-not $legacyCircularPool) {
        Write-Host '[build] vitaGL uses the newer default circular pool; CIRCULAR_VERTEX_POOL=2 is still passed for reproducibility.'
    }

    # make does not track changes to command-line variables. A clean rebuild is
    # mandatory or the archive can contain objects built with different flags.
    # The upstream clean target also descends into every sample and assumes Unix
    # rm is on PATH, so clean exactly the library products with PowerShell.
    $vitaGLSource = Join-Path $vitaGLPath 'source'
    Get-ChildItem -LiteralPath $vitaGLSource -Recurse -File -Filter '*.o' |
        Remove-Item -Force
    $builtVitaGLLibrary = Join-Path $vitaGLPath 'libvitaGL.a'
    if (Test-Path -LiteralPath $builtVitaGLLibrary) {
        Remove-Item -LiteralPath $builtVitaGLLibrary -Force
    }
    Invoke-Native $make (@('-C', $vitaGLPath, "-j$Jobs") + $vitaGLFlags)
    if (-not (Test-Path -LiteralPath $builtVitaGLLibrary -PathType Leaf)) {
        throw 'vitaGL build completed without producing libvitaGL.a.'
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $privatePrefix 'lib') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $privatePrefix 'include') | Out-Null
    Copy-Item -LiteralPath $builtVitaGLLibrary -Destination $vitaGLLibrary -Force
    Copy-Item -LiteralPath (Join-Path $vitaGLSource 'vitaGL.h') -Destination $vitaGLHeader -Force
}

if (-not (Test-Path -LiteralPath $vitaGLLibrary -PathType Leaf)) {
    throw "Private vitaGL is missing at $vitaGLLibrary. Run this script with -BuildVitaGL."
}
if (-not (Test-Path -LiteralPath $vitaGLHeader -PathType Leaf)) {
    throw "Private vitaGL header is missing at $vitaGLHeader. Run this script with -BuildVitaGL."
}
$vitaGLUndefined = (& $nm -u $vitaGLLibrary 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect libvitaGL.a with arm-vita-eabi-nm.'
}
if ($vitaGLUndefined -notmatch '__real_malloc' -or $vitaGLUndefined -notmatch '__real_free') {
    throw 'Private vitaGL lacks HAVE_WRAPPED_ALLOCATORS=1. Re-run with -BuildVitaGL.'
}
$vitaGLDefined = (& $nm -g --defined-only $vitaGLLibrary 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0 -or $vitaGLDefined -notmatch 'vgl_shader_cache_path') {
    throw 'Private vitaGL lacks HAVE_SHADER_CACHE=1. Re-run with -BuildVitaGL.'
}
if ($vitaGLDefined -match '\blegacy_pool_end\b') {
    throw 'Private vitaGL was built without NO_DEBUG=1. Re-run with -BuildVitaGL.'
}
if ($vitaGLDefined -notmatch '(circular_data_pool|vertex_data_pool)') {
    throw 'Private vitaGL lacks the circular vertex/data pool.'
}

if ($BuildSDL) {
    $sdlCMake = Join-Path $sdlPath 'CMakeLists.txt'
    $sdlVGLSource = Join-Path $sdlPath 'src\video\vita\SDL_vitagles_vgl.c'
    if (-not (Test-Path -LiteralPath $sdlCMake -PathType Leaf) -or
        -not (Test-Path -LiteralPath $sdlVGLSource -PathType Leaf)) {
        throw "SDL at $sdlPath is not the vitagl branch (SDL_vitagles_vgl.c is missing)."
    }
    if ((Get-Content -Raw -LiteralPath $sdlCMake) -notmatch 'VIDEO_VITA_VGL') {
        throw "SDL at $sdlPath does not expose VIDEO_VITA_VGL."
    }
    $sdlVGLText = Get-Content -Raw -LiteralPath $sdlVGLSource
    $sdlGLES2Text = Get-Content -Raw -LiteralPath (Join-Path $sdlPath 'src\render\opengles2\SDL_render_gles2.c')
    if ($sdlVGLText -match '\bvglEnd\s*\(' -or $sdlGLES2Text -match '\bvglCgShaderSource\s*\(') {
        throw 'This SDL vitagl branch targets obsolete vitaGL APIs. Use the vitagl branch from Northfear/SDL.'
    }

    New-Item -ItemType Directory -Force -Path $dependencyBuildRoot | Out-Null
    Invoke-Native $cmake @(
        '-S', $sdlPath,
        '-B', $sdlBuildPath,
        '-G', 'Unix Makefiles',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
        "-DCMAKE_INSTALL_PREFIX=$sdkPrefix",
        '-DVIDEO_VITA_VGL=ON',
        '-DVIDEO_VITA_PVR=OFF',
        '-DVIDEO_VITA_PIB=OFF',
        '-DSDL_SHARED=OFF',
        '-DSDL_STATIC=ON',
        '-DSDL_TEST=OFF'
    )
    Invoke-Native $cmake @('--build', $sdlBuildPath, '--parallel', "$Jobs")
    Invoke-Native $cmake @('--install', $sdlBuildPath)
}

if (-not (Test-Path -LiteralPath $sdlConfig -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sdlLibrary -PathType Leaf)) {
    throw 'SDL2 is not installed in VitaSDK. Run this script with -BuildSDL.'
}
$sdlConfigText = Get-Content -Raw -LiteralPath $sdlConfig
if ($sdlConfigText -notmatch '#define\s+SDL_VIDEO_OPENGL_ES2\s+1' -or
    $sdlConfigText -notmatch '#define\s+SDL_VIDEO_VITA_VGL\s+1') {
    throw 'Installed SDL2 lacks the vitaGL backend. Re-run with -BuildSDL using the SDL vitagl branch.'
}
$sdlMembers = (& $ar -t $sdlLibrary 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0 -or $sdlMembers -notmatch 'SDL_vitagles_vgl') {
    throw 'libSDL2.a does not contain the vitaGL video backend. Re-run with -BuildSDL.'
}
$sdlUndefined = (& $nm -u $sdlLibrary 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect libSDL2.a with arm-vita-eabi-nm.'
}
if ($sdlUndefined -match '\b(vglCgShaderSource|vglEnd)\b') {
    throw 'Installed SDL2 references obsolete vitaGL APIs. Rebuild it from Northfear/SDL with -BuildSDL.'
}

if ($Clean -and (Test-Path -LiteralPath $buildPath)) {
    $resolvedBuild = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $buildPath).Path)
    if (-not $resolvedBuild.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase) -or $resolvedBuild -eq $root) {
        throw "Refusing to remove unsafe build path: $resolvedBuild"
    }
    Write-Host "[build] Removing $resolvedBuild"
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

if (($BuildVitaGL -or $BuildSDL) -and (Test-Path -LiteralPath $buildPath)) {
    # The linker sees these dependencies through -l flags and cannot detect that
    # the dependency archives changed. Remove only the downstream link products.
    $linkProducts = @('flycast', 'flycast.exe', 'flycast.velf', 'flycast.velf.tmp', 'eboot.bin', 'eboot.bin.out')
    foreach ($name in $linkProducts) {
        $path = Join-Path $buildPath $name
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force
        }
    }
    Get-ChildItem -LiteralPath $buildPath -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @('.vpk', '.self') -or $_.Name.EndsWith('.vpk.out') } |
        Remove-Item -Force
}

$symbolOption = if ($Symbols) { 'ON' } else { 'OFF' }
$ltoOption = if ($LTO) { 'ON' } else { 'OFF' }
Invoke-Native $cmake @(
    '-S', $root,
    '-B', $buildPath,
    '-G', 'Unix Makefiles',
    '-DVITA=ON',
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DVITA_VELF_SYMBOLS=$symbolOption",
    "-DVITA_LTO=$ltoOption",
    "-DVITA_VITAGL_ROOT=$privatePrefix",
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
)
Invoke-Native $cmake @('--build', $buildPath, '--parallel', "$Jobs")
Invoke-Native $cmake @('--build', $buildPath, '--target', 'Flycast.vpk', '--parallel', "$Jobs")

$vpk = Join-Path $buildPath 'Flycast.vpk'
$eboot = Join-Path $buildPath 'eboot.bin'
if (-not (Test-Path -LiteralPath $vpk -PathType Leaf) -or
    -not (Test-Path -LiteralPath $eboot -PathType Leaf)) {
    throw 'Build completed without producing Flycast.vpk and eboot.bin.'
}

Write-Host '[build] Environment verified.'
Get-Item -LiteralPath $vpk, $eboot |
    Select-Object FullName, Length, LastWriteTime |
    Format-Table -AutoSize
