# Marvel vs. Capcom 2 (US v1.001) Vita optimization ledger

Status: deterministic framedrop savestate established; baseline, host profile
and guest-block profile collected. The first retained branch specialization
enables the NEON SH4 FPU path for `T1212N`, improving guest-time throughput by
2.78% in this workload. This is not yet evidence of full-speed or whole-game
correctness.

## Fixed target identity

- Branch: `codex/game-marvel-vs-capcom-2`
- Neutral base: `origin/loop-instrumentation` at
  `178e408df1d7553c236fa52d770d64a0005a6920`
- Guest content:
  `ux0:data/marvel/Marvel vs. Capcom 2 v1.001 (2000)(Capcom)(NTSC)(US)[!].gdi`
- Disc description: Dreamcast, NTSC-US, revision v1.001. The supplied GDI and
  all three referenced tracks were present for the first functional boot.
- Runtime product ID: `T1212N`, confirmed by the `Game ID is [T1212N]` message
  in the first-boot console artifact. Product-specific code must match this ID
  and the supplied revision; do not infer addresses from another dump.
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

The build also copies the generated `Flycast.vpk_param.sfo` beside the SELF as
`build-vita/sce_sys/param.sfo`. It parses the copied SFO and fails unless
`ATTRIBUTE2` is the 32-bit integer `12`.

Enable `VITA_JIT_RUN_PROFILE` only for runs intended to collect guest block
entry counts. `scripts/build-vita.ps1 -JitRunProfile` makes this diagnostic
configuration explicit and reproducible. Preserve the exact pre-conversion
ELF and converted VELF beside every profile. Test LTO only as a final A/B
comparison.

## Baseline setup

Before changing the target configuration, copy the current
`ux0:data/flycast/emu.cfg` into the run artifacts. Then set exactly:

```ini
[config]
Dreamcast.AutoStartContent = ux0:data/marvel/Marvel vs. Capcom 2 v1.001 (2000)(Capcom)(NTSC)(US)[!].gdi
```

The pre-run target configuration is preserved at
`build-vita/devkit-config-backups/mvc2-first-launch/emu.cfg.before-mvc2`
(SHA-256 `BC4092934D10869568CA1EC0010839024ACBF19341660CE34352771DD954FC35`).
The applied config changed the autostart path and disabled `RazorAutoCapture`
and `RazorFrameSync` for a non-profiled functional run; its SHA-256 is
`5BEA28568B366DEB67513D348DEA993A167F09E5E50CC6DEB116868EBA3A20FB`.

The fixed workload is savestate slot 6 at a reported framedrop during the
Ruby Heart versus Captain America match on the Abyss stage:

- target state:
  `ux0:/data/flycast/data/Marvel vs. Capcom 2 v1.001 (2000)(Capcom)(NTSC)(US)[!]_6.state`
- preserved state:
  `build-vita/workloads/marvel-vs-capcom-2/framedrop-slot-6/Marvel vs. Capcom 2 v1.001 (2000)(Capcom)(NTSC)(US)[!]_6.state`
- size: 10,264,261 bytes
- SHA-256:
  `10F53EDAAB5DD2E5F0CB70115F7164C2EC305FDE63F57DC21CAA8959E77A4DA5`
- baseline autoload config SHA-256:
  `69C99B54B98C4A1308C6E420C579FFEFDB1FCB086719D4800FA1F439CC0D3C60`
- autoload settings: `Dreamcast.AutoLoadState=yes`,
  `Dreamcast.AutoSaveState=no`, `Dreamcast.SavestateSlot=6`

The state and config were copied back from the target and matched byte for
byte. Every measured run begins at RTC `1988906533`, which independently
confirms that the same guest state was restored. Timing runs use a fixed 30 s
host observation window and the `emu-second` log marker. Occam runs use the
same state but a 20 s host-driven sample window.

Full speed means guest/game time tracks wall time for the same scene, with
continuous correctly paced audio, stable animation/input cadence and no visual
corruption or delayed failure. Host frame rate alone is not sufficient.

## Retained measurements

| Candidate | Build/ELF SHA-256 | Workload | Cold/repeated runs | Guest vs wall time | Audio/game speed | Visual/XML outcome | Profiles | Decision |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Functional boot | C++ tree `0344626cd`; SFO workflow `b11ececfa`; ELF `B142C2C3FDF483CB27AB9C48D5ADE6B6ACDD6BDFF5BD6058D6956FC283C2B9A3` | Fresh process, no input, fixed 60 s window | One run; cache state not characterized | Not measured | Audio not captured; 60 FPS overlay is not a speed measurement | Reached `PRESS START BUTTON`; expected `ProcessKilled`; no crash/abort marker. Black side wedges during the title effect need an A/B check with the widescreen hack disabled. | None; Razor disabled | Boot passes only; not a performance baseline |
| Slot-6 baseline, VFP dynarec | Tree `f9e09affa`; raw ELF `6524A2E52A00FE58A8941E10F15ABA8D4CD844200BDAC2E9E0301C3C99A85049`; VELF `E54F0173D940E5B97E8627150F9BF60310F98C1CA2B816E9B2A6BE1809C3C03E` | Exact slot-6 state, `Dynarec.useNeon=no`, fixed 30 s | Three repeated runs | Guest/wall 0.8003 mean (80.03%); standard deviation 0.0035; individual measured deltas 15-16 guest seconds over 18.85-19.98 wall seconds | Audio not captured; guest speed below full speed | 48-49 FPS final overlays; coherent match frames; all three XML outcomes are expected `ProcessKilled` | Host PC sample and JIT profile below | Accepted baseline |
| NEON FPU dynarec | Same tree and binaries as baseline; config `E2236B333362C7E253FFC8FED7AA38B813F35F54FAC3073FCF4A18CFA13FD41F` | Exact slot-6 state; only delta is `Dynarec.useNeon=yes`; fixed 30 s | Three repeated runs | Guest/wall 0.82227 mean (82.23%); standard deviation 0.00047; +2.19 percentage points, +2.74% relative | Audio not captured; still below full speed | 51-52 FPS final overlays; coherent match frames; all three XML outcomes are expected `ProcessKilled` | Re-profile below | Retain provisionally; improvement exceeds baseline variance |
| `T1212N` NEON branch default | Raw ELF `4DA6724CC7B0D9B184B36EBCA1D2F604692FF921F33A6C400A2A9C5EDD57368A`; VELF `EB3F839B69D94E34C43C8B04606511E90A5C734B0FD846C1332198052A54C1DB`; SELF `99393BFC13BE0C97EE122012C1719CA5E5A1E602E412FA4151C1951CEA4836FA` | Exact slot-6 state; baseline config `69C99B54B98C4A1308C6E420C579FFEFDB1FCB086719D4800FA1F439CC0D3C60` still says `Dynarec.useNeon=no`; source sets the game default before per-game settings; fixed 30 s | Three repeated runs | Guest/wall 0.822594 mean (82.259%); standard deviation 0.000348; +2.226 percentage points, +2.782% relative to VFP baseline | Audio not captured; still below full speed | Three coherent match frames; one transient overlay sampled 0 FPS and is excluded as a metric; all three XML outcomes are expected `ProcessKilled` | Existing NEON re-profile applies to the same dynarec path | Retain; source integration reproduces the config-only A/B |

First-boot artifacts are under
`build-vita/devkit-logs/mvc2-first-boot/20260830-164948/run-001/`:

- `flycast.velf`: SHA-256
  `B142C2C3FDF483CB27AB9C48D5ADE6B6ACDD6BDFF5BD6058D6956FC283C2B9A3`
- `eboot.bin`: SHA-256
  `C0BEBB6D24870974691D268B2727DFD69D49AF1AA9246B69355A300E67A3DBDC`
- `param.sfo`: SHA-256
  `0106D568E3DC48228DE1A4AF779D2B4DAD9FE5DA393170ED826037A44A4038F0`
- `console.txt`, `run.xml`, `final.bmp` and the exact applied `emu.cfg`

## Baseline and candidate artifacts

Repeated VFP baseline:
`build-vita/devkit-logs/mvc2-state-baseline/20260830-170838/`.

Repeated NEON candidate:
`build-vita/devkit-logs/mvc2-state-neon-a/20260830-173546/`.
The copied append-only target log has SHA-256
`746B18FEBAE0569300291EE33229C636949C8915771AA64EE3F8C429665DDE3A`.

Source-integrated NEON default, tested against the byte-verified baseline
configuration:
`build-vita/devkit-logs/mvc2-state-neon-source/20260830-174515/`.
The copied append-only target log has SHA-256
`CB904DA679FECC32039ADD3762FF306B65F9D33CB2E14EAF01DDC24A6F31CCA3`.
All three sessions begin at RTC `1988906533`; the individual guest/wall ratios
are 0.822199, 0.822538 and 0.823045.

The unprofiled iteration build identities are:

- `eboot.bin`:
  `8A82BFFF332091BE543D9B6702D8595049F135B6D6A3EE0EAB3FD6257A3B50DB`
- `param.sfo`:
  `0106D568E3DC48228DE1A4AF779D2B4DAD9FE5DA393170ED826037A44A4038F0`

## Host profile

The accepted baseline PC-sample trace is under
`build-vita/devkit-logs/occam/mvc2-state-host-driven-pc/`. It contains the
exact raw ELF, converted VELF, `.rzr` capture and XML export. The 20 s capture
contains 4,923,092 samples in the application ELF range. The leading functions
are:

| Function | Samples | Percent |
| --- | ---: | ---: |
| generated `SH4_TCB` | 2,035,247 | 41.34% |
| `TAParserTempl<...>::ta_poly_data<5,2>` | 472,772 | 9.60% |
| `make_index` | 317,528 | 6.45% |
| generated ARM7/AICA code | 244,928 | 4.98% |
| paletted twiddled texture conversion | 215,889 | 4.39% |
| `ta_vtx_data32` | 189,980 | 3.86% |
| `__udivmoddi4` | 119,192 | 2.42% |
| `AICA_Sample32` | 115,733 | 2.35% |

The candidate was re-profiled under
`build-vita/devkit-logs/occam/mvc2-state-neon-pc/`. Its 4,936,064 samples keep
the same leading order (`SH4_TCB` 41.40%, TA polygon parsing 9.70%,
`make_index` 6.47%). These percentages are relative to a fixed wall-time
window, so they do not by themselves quantify the already measured throughput
gain.

Target-triggered Occam attempts are rejected evidence. With
`RazorAutoCapture`/`RazorFrameSync` enabled, the process stopped with
`STOP_REASON_EXCEPTION_DATA_ABORT` when capture began. One attach attempt also
conflicted with `psp2run /debug`. Direct Occam loading still reproduced the
abort, while host-driven capture with both hooks disabled completed normally.
The current reliable workflow therefore uses `devkit-game-loop.ps1 -NoDebug`
plus `capture-vita-profile.ps1 -NoTargetTrigger`. Do not treat the failed
captures as workload crashes.

## Guest JIT profile

The diagnostic build used `VITA_JIT_RUN_PROFILE=ON`, symbols on and LTO off.
Artifacts are under
`build-vita-jit-profile/devkit-logs/mvc2-state-jit-profile/20260830-173155/run-001/`:

- raw ELF:
  `E9E7E20FEFD512D7DBA726007E79D66D70E67B5C993889C43C4D9C07D1BD9BFC`
- converted VELF:
  `F97117F6AD996B272810054B15C28BD3BD3F11D30F8A37FABAD5C43A2EE8FF7B`
- `jit_blocks.txt`:
  `757303ECE3D02C014D9C50685064E72A215B2EAE5C3A2103C1DE561E51512F11`
- `sh4_main_ram.bin`:
  `FF9873644C62BBB98BC98B46C20870EB69239204375A304EA618F8488DAC3031`

The profiler reset at guest second 15, measured for 15 guest seconds, wrote
6,842 blocks and dumped the matching 2 MiB of main RAM. Ranking by
`runs * generated host opcodes` concentrates work in these guest clusters:

- `0x8C12FDB6..0x8C12FF9C`: vertex transformation/submission using `ftrv`,
  `fipr`, `fdiv` and store-queue `pref`;
- `0x8C02DEC6` and `0x8C0354D4..0x8C035514`: very high-frequency short blocks;
- `0x8C18F7E0` and `0x8C19161A..0x8C19162E`: high-frequency timing/control
  loops;
- `0x8C1291DC..0x8C1293FF`: unrolled SH4 integer division routines.

This ranking is a candidate locator, not instruction-level host timing.

## Next evidence gate

1. Test a single source-level TA/JIT hypothesis against the NEON baseline,
   with at least three repeats when the delta is small.
2. Capture audio or perform a supervised listening pass before claiming that
   game-speed improvement preserves pacing.
3. Exercise additional heavy matches/stages; this one savestate is not a
   whole-game correctness result.
4. Re-profile every retained material change and perform final LTO-on versus
   LTO-off A/B only after representative gameplay is full speed.
