# Local VitaSDK build

This repository uses the host VitaSDK directly; Docker is not required. The
build is intentionally a neutral Flycast Vita baseline. Game-specific HLE,
threading, timing and UI changes from other forks are not part of this setup.

## Prerequisites

- `VITASDK` points at the local SDK (currently `C:\vitasdk`).
- `arm-vita-eabi-*`, CMake and GNU Make are on `PATH`.
- `core/deps/libchdr` is initialized.
- A vitaGL checkout exists at `..\vitaGL`; it is used only as the seed for a
  private Flycast copy.
- Northfear SDL's `vitagl` branch exists at `..\SDL`.

For a fresh checkout:

```powershell
git clone -b vitagl https://github.com/Northfear/SDL.git ..\SDL
```

If `..\SDL` is an existing clone of a different fork, preserve its current
remote and add Northfear separately:

```powershell
git -C ..\SDL remote add northfear https://github.com/Northfear/SDL.git
git -C ..\SDL fetch northfear vitagl
git -C ..\SDL switch -C vitagl --track northfear/vitagl
```

The older `Rinnegatamante/SDL` vitagl branch is not interchangeable: it still
references `vglCgShaderSource` and `vglEnd`, which current vitaGL no longer
exports. The build script rejects that mismatch before linking Flycast.

The environment was verified on 2026-08-26 with vitaGL commit `6440cf0` and
Northfear SDL commit `f99bb638`.

## First build

Run from a PowerShell prompt:

```powershell
scripts\build-vita.bat -BuildVitaGL -BuildSDL
```

vitaGL is rebuilt from a clean object tree with:

```text
NO_DEBUG=1 HAVE_WRAPPED_ALLOCATORS=1 DEPTH_STENCIL_HACK=1
CIRCULAR_VERTEX_POOL=2 HAVE_SHADER_CACHE=1
```

On vitaGL versions from April 2026 onward, the failsafe circular pool selected
by `CIRCULAR_VERTEX_POOL=2` became the default. The variable is still passed so
the same command also works with older checkouts.

The first `-BuildVitaGL` run clones the seed checkout into
`build-vita-deps\src\vitaGL` and installs only into
`build-vita-deps\prefix`. CMake links that archive by absolute path and takes
`vitaGL.h` from the same private prefix. The `libvitaGL.a` and header under
`C:\vitasdk` are neither read nor overwritten, so other projects may rebuild
their system vitaGL independently.

SDL is always built as `Release` with `VIDEO_VITA_VGL=ON`; a Debug SDL installs
`libSDL2d.a`, which Flycast does not link. SDL remains installed into the local
VitaSDK; only vitaGL is isolated in the repository-private prefix.

Output:

```text
build-vita\Flycast.vpk
build-vita\eboot.bin
```

## Incremental builds

```powershell
scripts\build-vita.bat
scripts\build-vita.bat -Jobs 8
scripts\build-vita.bat -Clean
scripts\build-vita.bat -Symbols
scripts\build-vita.bat -LTO
```

LTO is disabled by default so game-specific debugging builds and incremental
links remain fast. Non-LTO builds use function/data sections and linker garbage
collection to keep the executable within Vita's memory budget. `-LTO` enables
link-time optimization explicitly for a final performance build.

`-Symbols` keeps `.symtab` in `flycast.velf` for a future Razor workflow. It is
off by default and is not required to run the VPK on retail hardware.

The script refuses stock SDL, verifies that the archive contains the vitaGL
backend, and checks the private vitaGL archive for wrapped allocators, shader
cache and circular-pool support before configuring Flycast.
