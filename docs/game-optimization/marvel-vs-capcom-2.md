# Marvel vs. Capcom 2 (US v1.001) Vita optimization ledger

Status: branch prepared; no performance baseline has been collected yet.

## Fixed target identity

- Branch: `codex/game-marvel-vs-capcom-2`
- Neutral base: `origin/loop-instrumentation` at
  `178e408df1d7553c236fa52d770d64a0005a6920`
- Guest content:
  `ux0:data/marvel/Marvel vs. Capcom 2 v1.001 (2000)(Capcom)(NTSC)(US)[!].gdi`
- Disc description: Dreamcast, NTSC-US, revision v1.001. This description comes
  from the supplied filename and must be confirmed by the first clean boot.
- Runtime product ID: **pending**. Record the exact `Game ID is [...]` boot log
  before adding product-specific code. Do not infer it from another dump or
  revision.
- Target Manager DevKit:
  `39829e00451207db324731441501004d` (local default target)
- Vita title/process ID: `FLYCASTDC`

All game addresses, product IDs, controller automation and destructive
compatibility assumptions belong on this branch only.

## Reproducible build

Iteration builds use Release mode, symbols enabled and LTO disabled. The first
build creates the worktree-private vitaGL copy with the required flags embedded
by `scripts/build-vita.ps1`:

```powershell
.\scripts\build-vita.ps1 -BuildType Release -BuildDir build-vita `
  -VitaGLDir F:\Ubuntu\vitaGL -SDLDir F:\Ubuntu\SDL `
  -BuildVitaGL -Symbols
```

Pinned build dependency:

- `core/deps/libchdr`: `7972daa6ca6bf631bf54d60c5ed09eb76472bb79`

Enable `VITA_JIT_RUN_PROFILE` only for runs intended to collect guest block
entry counts. Preserve the exact unstripped ELF and its SHA-256 beside every
profile. Test LTO only as a final A/B comparison.

## Baseline setup

Before changing the target configuration, copy the current
`ux0:data/flycast/emu.cfg` into the run artifacts. Then set exactly:

```ini
[config]
Dreamcast.AutoStartContent = ux0:data/marvel/Marvel vs. Capcom 2 v1.001 (2000)(Capcom)(NTSC)(US)[!].gdi
```

No DevKit configuration was changed while preparing this branch.

The initial workload is not yet fixed. Prefer a Target Manager controller
recording that reaches and plays a representative match over an uncontrolled
attract sequence. The recording identity, playback trigger, completion marker
or fixed observation duration must be added here before comparing builds.

Full speed means guest/game time tracks wall time for the same scene, with
continuous correctly paced audio, stable animation/input cadence and no visual
corruption or delayed failure. Host frame rate alone is not sufficient.

## Retained measurements

| Candidate | Build/ELF SHA-256 | Workload | Cold/repeated runs | Guest vs wall time | Audio/game speed | Visual/XML outcome | Profiles | Decision |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Neutral baseline | pending | pending | pending | pending | pending | pending | pending | not measured |

## Next evidence gate

1. Back up the target configuration and apply the exact autostart path above.
2. Build with symbols and LTO off.
3. Boot the supplied GDI and record the product ID/revision evidence.
4. Create a repeatable controller recording for a representative match.
5. Capture a cold baseline with console, XML exit log, screenshot, exact ELF
   hash, guest-time progression and wall time.
6. State the first falsifiable bottleneck hypothesis only after that baseline.
