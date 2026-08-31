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

## Rejected iteration: integer TA vertex classification

Hypothesis: `make_index` spends meaningful critical-path time in the seven
VFP comparisons used by `is_vertex_inf`. Baseline disassembly and per-PC
Occam hits located most of its samples in or around the coordinate-validation
loop (`0x81587200..0x8158726c`).

The candidate replaced the NaN/huge-coordinate checks with unsigned IEEE-754
bit comparisons against `0x7dcca14b` (`3.4e37f`). It retained the floating-point
`z < 0` comparison to preserve negative-zero and host flush-to-zero behavior
for negative denormals. Scalar `memcpy` bit casts compiled without temporary
stack copies, leaving one VFP comparison on the ordinary vertex path instead
of seven. The surrounding function nevertheless needed more integer-register
spills. No invalid-vertex handling was removed.

A host differential test, compiled with `g++ -std=c++11 -O3 -fno-fast-math`,
passed 1,017,576 triples per mode with denormals both preserved and flushed.
It includes all combinations of 26 edge bit patterns plus one million
deterministic random triples. This is predicate evidence, not a substitute
for the Vita gameplay run.

Both target state and config were re-read before the candidate runs and
still matched the fixed workload hashes. Results:

| Build | Guest/wall ratios | Mean | Population standard deviation | Decision |
| --- | --- | ---: | ---: | --- |
| Integer-classification candidate | 0.818498, 0.821440, 0.821102 | 0.820347 | 0.001314 | Reject: no measured improvement |
| Rebuilt NEON control after reverting candidate | 0.823766, 0.823172, 0.824275 | 0.823738 | 0.000451 | Keep as current branch build |

The candidate is 0.412% slower than the contemporary control. All six runs
restored RTC `1988906533`, advanced 16 guest seconds between measured markers,
ended with the expected `ProcessKilled`, and produced coherent match frames.
Audio was not captured. The source change was fully reverted; reducing this
local instruction count did not improve this workload's critical path.

Candidate artifacts:
`build-vita/devkit-logs/mvc2-state-integer-validation-a/20260830-221134/`.
The folder preserves the test source, candidate `ta_vtx.cpp`, exact config,
raw ELF, VELF, SELF, SFO, target log, console/XML and screenshots.

- raw ELF: `D9671AE24D64465B1CF3A1F47AC33E9B1D77A84FC3B014A7152B64A710159EDB`
- VELF: `0A8767856F6FCE68873856C1FAA53136F70B424853E8E5E27C55DD69E2FCD73B`
- SELF: `4F925300E0006FDFB2CBD62F435DECECAA61ECB878B7DCA6A8A496F26785C47D`
- candidate source: `2AA93B2AFE14FBAF7BF8FD47C50FFBCAF4F0F5B2A1EE5D26047DD6E08B427021`
- target log: `B10ADD261BD2C9F5D430D8B60A05C32E53F4DCAA2783529F2DCD84C6B28E22F7`

Control artifacts:
`build-vita/devkit-logs/mvc2-state-neon-control/20260830-221516/`.

- raw ELF: `273353FAD3B3D01BDE3DFEF9EED7631608EF930DA385E08B6BA148BC97F0FADA`
- VELF: `EEA17C10508DF099769C838D6C1E0A1B38C29B8499B4E08D04B65247F20C5D20`
- SELF: `32A5718E6F80A9752C34BC9E1C7C9C1B5ECEBE3194E0881B3A8BA34716B5E0B5`
- target log: `1B522AD2DD4F4F9B5632DD5BCD35A5A176C9A854AF925181C73806CAE5CA849E`

## Rejected iteration: vertex-emission block HLE

Native replacements of `0x8C12FE5A` (44 bytes, CRC32 `F2923FBC`) and
`0x8C12FE5C` (42 bytes, CRC32 `6EE40942`) covered exactly one decoded block,
through the PREF delay slot at `0x8C12FE84`. The existing entry debit was
reused as the complete block cost; no scheduler boundary was removed. The
handlers preserved both ordered SQ flushes, including RAM destinations, and
all register/flag residue. Full-MMU compilation was excluded; unsupported
runtime state used the original instruction handlers.

The initial TA-only guard fell back frequently when the game used this same
block to write a RAM command buffer. The revised version handled both routes
and kept direct RAM reads after the first flush, including possible overlap.
A diagnostic shadow mode compared the actual original interpreter handlers
against the native implementation without dispatching either shadow's SQ
packets. It compared all GPR/FPRs, PC/PR/FPUL/SR/FPSCR, cycle fields, both SQ
buffers and the ordered packet addresses/data. Suppressed RAM flushes were
allowed only when they could not affect the subsequent reads. The final
candidate passed 4,096 real calls for each entry/route combination (16,384
total), with both CRCs verified. This is workload coverage, not an exhaustive
float/mode correctness claim.

Three alternating off/on pairs used the **same executable**, diagnostics off,
35-second launch windows, and exactly RTC `1988906533..1988906549` (16 guest
seconds) for every measurement. Configs differed only in `HlePatches` and were
read back and hash-checked before each launch. State/config identities were
also confirmed before the experiment.

| Mode | Guest/wall ratios | Mean | Population standard deviation |
| --- | --- | ---: | ---: |
| HLE off | 0.820176, 0.820555, 0.822073 | 0.820935 | 0.000819 |
| Vertex HLE on | 0.793454, 0.793611, 0.794439 | 0.793835 | 0.000432 |

Decision: **reject**, 3.301% slower. No fallback occurred in the measured HLE
runs. The wrapper/context traffic and dynamic exit were not offset by the
native block; this causal explanation is an inference from generated code,
not a separately measured cost breakdown. All six runs ended with the
expected `ProcessKilled`; inspected screenshots remained coherent. Audio was
not captured. The source was removed from the active build and is recoverable
from the artifact copy below.

Evidence root: `build-vita/devkit-logs/mvc2-hle-vertex-prepare/`.
`shadow-final.log` contains all four validation passes. `ab-final/` preserves
the final candidate source, exact raw ELF/VELF/SELF/SFO, six read-back configs
and six append-only logs. Screenshot/XML launch folders are
`build-vita/devkit-logs/mvc2-hle-vertex-ab-{1,2,3}-{off,on}/`.

- raw ELF: `6155DF50A722F99B3194404AA8E771CE83DFCB9A47699011C9BFD7A9F7C114D4`
- VELF: `756E36F0B39A93222EC1E976F2F2CAA40CBD19EF96DB654D68269BF6C348FE84`
- SELF: `D1BA3B299D30B0FC22FF779AC091F8E2525C11A7B216607BFD81D3AD1C9E8A55`
- candidate source: `7EF58FEC93ABECBA5E4779BB95E482B5E6CA014180FD3E4DAD10E7A51204D56A`

## Retained iteration: unsigned-division HLE

`core/hw/sh4/hle/mvc2_us_division_1001.cpp` replaces only the nonzero-divisor
block at `0x8C1291E2..0x8C12926C` (140 bytes, CRC32 `5598C45C`, product
`T1212N`). The guest profile recorded 1,897,073 calls in its 15-guest-second
measurement. The existing decoder already collapses the DIV0U/ROTCL/DIV1
sequence, but its generic `shop_div32u` computes a 64-bit quotient/remainder.
Here the high word is provably zero and the remainder register is restored
from the stack before return. The HLE therefore needs only unsigned 32-bit
division; object disassembly confirms a call to `__aeabi_uidiv`.

Contract:

- R0/R1 receive the quotient; R2 is restored from the guest stack; R15 += 4.
- SR.M = 0, SR.Q = !quotient.bit0, SR.T = 0. Other status bits, all FPRs,
  FPSCR, FPUL and PR remain unchanged; next PC is the preserved PR.
- The only memory operation is the original stack read. Direct access is
  guarded by RAM range, alignment, no-wrap and MMU-state checks. Nonstandard
  state executes the original instructions, including divisor zero.
- Full-MMU builds decline this patch through the new optional registry
  eligibility callback. CRC failure still selects the normal JIT.
- The entire region is exactly one decoded block. The handler adds back the
  complete pre-debited block cost, including the decoder's 64 collapsed guest
  cycles (70 total with the current configuration). No scheduler boundary or
  guest time is removed. The existing downclock/floatHack cost is reused,
  not re-estimated from native instruction count.

Validation: 1,589,914 host cases compared quotient and Q/M/T residue with an
independent 32-step DIV1 model. The on-target `Mvc2HleValidate=yes` diagnostic
then passed 16,384 real entries against the actual interpreter opcode
handlers, comparing every GPR/FPR, PC/PR/FPUL/SR/FPSCR and cycle fields. That
diagnostic was disabled for timing. The initial process-cold diagnostic launch
is retained separately; every A/B launch also starts a fresh process/JIT,
without deleting the persistent shader cache.

The same-executable A/B protocol used for the rejected vertex candidate gave:

| Mode | Guest/wall ratios | Mean | Population standard deviation |
| --- | --- | ---: | ---: |
| HLE off | 0.824657, 0.823257, 0.824402 | 0.824106 | 0.000609 |
| Division HLE on | 0.829359, 0.829962, 0.829574 | 0.829632 | 0.000249 |

Decision: **retain for this workload**, +0.671% throughput, from 82.41% to
82.96% of real time. All three pairs improve; no measured HLE run enters the
fallback. All use RTC `1988906533..1988906549`, complete the fixed observation
window and end with the expected `ProcessKilled`. Inspected frames remain
coherent. This is still below full speed; audio has not been captured or
auditioned and additional match/stage coverage is required.

Evidence root: `build-vita/devkit-logs/mvc2-hle-divide-prepare/`.
`division-contract-test.cpp` is the reproducible host test (`g++ -std=c++11
-O3`); `shadow-final.log` records the target differential pass. `ab-final/`
contains the source/executable snapshots, six byte-verified configs and six
logs. Launch screenshots/XML are in
`build-vita/devkit-logs/mvc2-hle-divide-ab-{1,2,3}-{off,on}/`.

- raw ELF: `5706A0EFA7684AA2BD6E415C46B5CB925F22D307E3EA66FEA3A0E028C8DDC90E`
- VELF: `D7A1939E1AF33D4598006DA673E67411E28A41D5E1FBDE43044A8ACBBF7E7A04`
- SELF: `1FECD798427D8A6C08697B9490514447A00B77B8F00E4691F4F44293568EB030`
- HLE source: `03443F6C69DB37B5FC6A9B086E702816ECF86E6B5C46B7915A3BB420CFC19FBE`
- host test source: `9EC8C47DC24D4010457E862D81BFC26CB4A2FF1EF66034A56375854A5EBDC2A6`
- shadow log: `8BBAAEF683E15BB1435847D0A612B8E48D90A229D4430313AC877F37D2BBC835`
- HLE-off config: `371BAA650FEDF081164FB2B28B5D725E4E4307EB8BA633BEF2611D0E60DDE626`
- HLE-on config: original fixed-workload config, unchanged.

The build retains symbols, LTO off, and the adjacent
`build-vita/sce_sys/param.sfo` with integer `ATTRIBUTE2=12` and the original
SFO hash. No other game-specific patch set is active.

Post-change PC sample capture:
`build-vita/devkit-logs/occam/mvc2-state-hle-divide-pc/`. The 20-second
host-triggered capture completed with 4,924,738 application-range samples;
its exact raw ELF and VELF match the A/B hashes above. RZR SHA-256:
`E9C0C2AFA966320AD4B2E86B2E8CD743D46B9EE9B96B284FA88E6F4AE3EBBE54`.
`__udivmoddi4` falls from 2.41% of samples in the NEON profile to 0.66%;
`__udivsi3` rises from 0.67% to 1.43%, and the new HLE entry accounts for
0.97%. Remaining leaders are `SH4_TCB` 41.11%, TA polygon parsing 9.85% and
`make_index` 6.63%. These are waypoint-based function rankings in wall-time
captures, not a precise cost decomposition or a separate speedup estimate.
The longer 65-second profiling launch completed normally.

After profiling, target config and slot-6 state were read back again and
matched their original SHA-256 values exactly. Diagnostic shadow checking is
off and `HlePatches=yes` is restored. The DevKit process is stopped after the
completed loop; the preserved user savestate is unchanged.

## Rejected probes: resolution and emulation-thread affinity

The accepted division-HLE executable was compared at internal resolution 720
and 480 with the same slot-6 state: ratios 0.831687 and 0.830824. Lowering
resolution did not help, so 720 is retained. Evidence:
`build-vita/devkit-logs/mvc2-resolution-ab/20260830-231206/`.

The scheduling bars in the post-division Razor capture show the emulation
worker running for 98.55% of capture time (14.70% on core 0, 53.45% on core 1,
30.40% on core 2), versus 62.83% for the main/render thread. These sums use
only Running bars and the capture's own time units. Configuration keys
`vita.PinThreads`, `aica.Threaded`, `aica.ThreadCore`, `rend.AsyncTaParse`
are not implemented in this branch; their presence in emu.cfg is not proof
that the corresponding features run.

A narrowly scoped affinity probe, applied after SDL audio-thread creation,
pinned only the emulation worker to core 2. Readback confirmed mask 0x00040000
and the device reported 500 MHz in both runs; no clocks were changed. Ratios
were 0.832683 with automatic placement and 0.809307 pinned. Reject; the source
change is fully removed. Its exact source and binary survive under
`build-vita/devkit-logs/mvc2-affinity-ab/20260830-231911/`.

## Retained iteration: scheduler-bounded cache-loop HLE

The eight bytes at `0x8C02DEC6..0x8C02DECC`, CRC32 `9139F3E8`, encode
OCBI R0 / ADD #32,R0 / DT R5 / BF loop. OCBI is already a NOP in this branch's
non-STRICT interpreter and JIT; no cache operation is newly discarded.
STRICT and Full-MMU builds decline the patch.

The HLE native signature now receives the live remaining JIT cycle budget,
after the entry debit and any entry scheduler call. At the measured four-cycle
block cost, a batch executes at most `1 + (remaining - 1) / 4` iterations,
clipped to R5 (initial zero means 2^32 iterations). Unusual cost/budget/MMU
state executes one iteration. The next JIT entry still performs the exact
debit that reaches zero, with the same R0/R5/T and cycle residue as the
original loop. No scheduler or interrupt observation point is crossed inside
the native batch. The full n-block guest cost is added to the wrapper's
negative entry debit; R0 wraps modulo 2^32; all unrelated state is unchanged.

The host test `scripts/test-mvc2-cache-loop.cpp` passed 291,464 cases, including
scheduler-visible sequences, count-zero wrap and address wrap. The on-device
shadow run compared 16,384 batches against the real opcode handlers without
a mismatch; division shadow checks also passed again. Diagnostic artifact:
`build-vita/devkit-logs/mvc2-cache-hle-shadow/20260830-232621/`.

Three same-executable off/on pairs kept division HLE enabled, restored the
same state, measured RTC `1988906533..1988906549`, and disabled diagnostics:

| Cache-loop HLE | Ratios | Mean | Population standard deviation |
| --- | --- | ---: | ---: |
| Off | 0.827344, 0.827986, 0.826745 | 0.827358 | 0.000507 |
| On | 0.834899, 0.836339, 0.835858 | 0.835699 | 0.000599 |

Retain: +1.008% throughput. All six XML exits are the expected ProcessKilled;
the shadow and measured screenshots show coherent match frames. Audio has
not been captured/listened to, and this is not full speed. During continued
experimentation the patch is selected with `[perf] Mvc2CacheLoopHle=yes`
(default off); the original target configuration is restored after each run.

Evidence: `build-vita/devkit-logs/mvc2-cache-hle-ab/20260830-232744/` preserves
the source, exact binaries, six read-back configs and six target logs. Launch
artifacts are in `mvc2-cache-hle-ab-{1,2,3}-{off,on}/` sibling directories.

- raw ELF: `2115BEDFEDEE0AFF8FB96AE745D02F25BE879181C2F607C577F5FD14668FBBAC`
- VELF: `B4A1D787DB5419414760225B6869779BDC2AD5400525AD2949E9923BFF49F500`
- SELF: `3CE0E62955B9CE9C55B328AB04DFF151BDAF87B007D8CA9D2F173C61DA6BFC1B`
- adjacent SFO remains ATTRIBUTE2=12 with the original hash.

Post-change host-triggered PC profile:
`build-vita/devkit-logs/occam/mvc2-cache-hle-pc/`, exact ELF matching above,
4,894,098 application-range samples. SH4_TCB remains 41.17%; TA polygon
parsing 9.47%, make_index 6.40%, ARM7 5.03%, AICA_Sample32 2.43%. These are
waypoint-based rankings, not a precise time decomposition. RZR SHA-256:
`BDE66653F8AEF08C3C98A6A153463D77846F7A4C15C26A5C080028168A157525`.
The 65-second profiling launch completed normally.

`scripts/measure-mvc2.ps1` now automates config backup/upload/readback/restore,
binary preservation, exact guest-interval measurement and result CSVs. It
refuses to continue if the SELF changes during a same-executable A/B.

## Rejected iteration: bounded LZ HLE (2026-08-31)

The 70-byte region at `8C0354D4`, CRC32 `9F73A69F`, was translated with
the actual optimized JIT block boundaries and costs (2/5/3/22/8/6/3 cycles).
The read-only SSA pass folds BRA/NOP at 550A, adding its two cycles to both
conditional paths. Batches returned before the next debit could reach zero.
The host decoder comparison passed 210,000 cases; on-device comparisons
passed 16,384 batches, including ordered writes, all registers and flags.

Initial A/B: off 0.844951, on 0.735801. The initial write guard confused SMC
eligibility with actual page locks. A second implementation tracked real
lock/unlock/reset operations and declined GGPO. Its on-device differential
also passed 16,384 batches, but A/B still regressed: off 0.841751, on 0.746303.
Thus the guard error was not the whole performance problem. Reject both;
all LZ and page-tracking changes are removed from the active source. No gain
is claimed from the unprofiled hypothesis that C++ dispatch overhead dominates.

Initial evidence: `build-vita/devkit-logs/mvc2-lz-hle-ab/20260831-103659/`.
Corrected guard: `build-vita/devkit-logs/mvc2-lz-lock-ab/20260831-104523/`;
both preserve source snapshots, binaries, read-back configs and logs.
Corrected shadow: `build-vita/devkit-logs/mvc2-lz-lock-shadow/20260831-104347/`.
All fixed-window launches report expected ProcessKilled exits.
Corrected raw ELF SHA-256:
`EADA6DBF7AA3C3E0C9C67EE8785E66F5E04499555E03C4723ABCFCAAB22CD32F`.
Corrected SELF SHA-256:
`81F989E62AC09C8113D7CFC6B0F01D790915FFAC1CDD1E24380ED39C1A177DF2`.
The original config and slot-6 state were re-read at preflight and matched
their recorded hashes. The user state was not changed.

## Retained iteration: direct FPU pair moves in the ARM dynarec

For the target game, 64-bit floating-point memory transfers no longer bridge
through integer scratch registers when the two SH4 registers are allocated
in s16..s31. Three VMOVs become two bit-preserving VFP register moves. The
single VLDR/VSTR d0 and its three-instruction fault-rewrite window are
unchanged; the transfer is outside that window. No guest opcode, guest debit,
memory access width/order, or scheduler check is removed. Adjacent and
non-adjacent allocated pairs are both supported; s0/s1 never alias them.
Experimental switch: `[perf] Mvc2DirectFpuPairs=yes`, default off.

Shared emitter helpers were executed on the Vita over all 240 ordered
distinct allocator-register pairs, with 4,096 cases each (983,040 total).
Tests compare both old/new load and store paths against input bits, poison
candidate destinations, and check FPSCR preservation across rounding modes,
DN/FZ and cumulative flags. Cases include signed zeros, denormals, infinities,
signalling/quiet NaNs and random payloads. Diagnostic run passed:
`build-vita/devkit-logs/mvc2-fpu-pair-test/20260831-105613/`.

Three cold-start, same-executable A/B pairs with diagnostics disabled:

| FPU pair optimization | Guest/wall ratios | Mean | Population SD |
| --- | --- | ---: | ---: |
| Off | 0.840160, 0.841529, 0.839763 | 0.840484 | 0.000757 |
| On | 0.873792, 0.874030, 0.872648 | 0.873490 | 0.000603 |

Retain: +3.927% throughput, 84.05% to 87.35% real-time speed. All use the same
RTC interval and preserve cache/division HLE. All six fixed windows complete
with expected ProcessKilled. Diagnostic screenshot is coherent; audio is
still not captured or auditioned. This does not establish full speed.

Evidence: `build-vita/devkit-logs/mvc2-fpu-pair-ab/20260831-105829/`, containing
source, executable, read-back config and log snapshots; per-launch XML/BMP
files are in the similarly named sibling directories.

- ELF: `FD888F7070154A4612E1C7DA8F91CCD21894BAFCC49AF1D80E921CE78C9A94BA`
- VELF: `08D074FD583EFB20A6AF150F83DB70F01FA2859210FFA960892B92989242E02D`
- SELF: `875489795E67EB600199DBF4BEE2BE345A125FA3CA77891119AA3338112E8ACA`
- Adjacent SFO remains ATTRIBUTE2=12 with its original hash.

Disc extraction and the IDA function contracts are documented separately in
`marvel-vs-capcom-2-functions.md`. The first profiling attempt started too
late and produced no trace; it is not performance evidence. A fresh capture
uses a longer observation window.

The successful post-change 20-second PC sample is in
`build-vita/devkit-logs/occam/mvc2-fpu-pair-pc2/`, with the exact A/B ELF.
It contains 5,005,728 application-range samples: SH4_TCB 38.27%, TA polygon
parsing 9.40%, make_index 6.47%, pal4 texture conversion 4.95%, ARM7 4.82%,
ta_vtx_data32 4.00%, AICA_Sample32 2.50%. These are waypoint-based function
rankings in a later portion of the same match, not instruction timing or
another estimate of the speedup. The launch used a 120-second fixed window.

## Rejected follow-up: direct allocated-D-register memory operations

After the retained FPU transfer change, a candidate sent VLDR/VSTR directly
to an already aligned contiguous pair in d8..d15. Other pairs retained the
accepted d0 bridge. Full-MMU and non-nvmem paths were unchanged. The same
983,040-case device test was expanded to include both memory paths and passed.
One diagnostic-free A/B pair measured off 0.867068 versus on 0.862394. There
is no demonstrated benefit; reject and restore the accepted emitter. This
single pair is not a precise estimate of the regression size.

Evidence/source: `build-vita/devkit-logs/mvc2-fpu-memory-ab/20260831-111435/`.
Raw ELF: `0929B5C5113C74F47E7B9F0BA12F94C43C1FB269D8B6EFB22B47A74EC75B6F29`.
Device test: `build-vita/devkit-logs/mvc2-fpu-memory-test/20260831-111254/`.
Both fixed-window A/B exits were expected ProcessKilled.

## SH4 downclock pacing probe (2026-08-31)

Added a per-emulated-second snapshot of guest render submissions, before host
frameskipping. This counter is read and updated on the emulation thread; it
does not race the render thread's separate completed-frame counter. It checks
game cadence, not presented FPS or audio continuity. Each probe cold-starts
slot 6 and measures the same RTC interval 1988906533..1988906549.

| SH4 cycle factor | Guest/wall ratio | Guest render submissions / 16 seconds |
| --- | ---: | ---: |
| 1.00 | 0.867961 | 959 |
| 1.25 | 0.884662 | 959 |
| 1.50 | 0.912305 | 958 |
| 1.75 | 0.942008 | 913 |
| 2.00 | 0.952438 | 899 |

All variants keep the accepted division and FPU-pair changes. Factors above
1.00 explicitly disable cache-loop HLE because its batching contract accepts
only the original four-cycle block; this is therefore a coupled configuration
probe, not an isolated measurement of the clock parameter. Each has one run.
All XML outcomes are expected fixed-window ProcessKilled. Target configuration
is restored afterwards; no savestate or physical Vita clock is changed.

Reject factors 1.75 and 2.00: the apparent speed increase includes a material
drop in game animation/render cadence. Factors 1.25 and 1.50 remain unaccepted
pending longer gameplay, input and audio checks. The one-frame difference at
1.50 alone is not sufficient to distinguish phase offset from slowdown.
Continue code optimization against the unchanged 1.00 factor.

Evidence: `build-vita/devkit-logs/mvc2-downclock-pacing/20260831-112015/` and
`build-vita/devkit-logs/mvc2-downclock-limit/20260831-112839/`; the corresponding
per-launch sibling directories contain XML logs and screenshots.
Both sets use ELF `AC80157C6EEF7A67319BB1C88ACA2E4475263E5E25D5C5271810C9BE2E267701`
and SELF `DDA4F2DB42249FB5A99647771AE3FE49FE549803E0912409269909F9D52246D2`.
The adjacent SFO still has ATTRIBUTE2=12 and its original hash.

## Retained iteration: scheduler-bounded RAM-to-VRAM copy HLE

The aligned copy hot block at 8C17A6F0 (ten bytes, CRC32 538D50CB) has five
guest cycles. The initial RAM-to-RAM hypothesis was falsified on-device: its
guard never entered the fast path because the observed destination is VRAM.
That unsuccessful diagnostic is preserved at
`build-vita/devkit-logs/mvc2-copy-hle-shadow/20260831-113519/`, including source.
Its extra RAM-page bookkeeping is removed from the active source.

The corrected handler batches aligned RAM-to-linear-64-bit-VRAM transfers,
bounded by both physical pages and the original scheduler debit. Ordered
eight-byte loads/stores retain the final raw DR0 value, R4/R5/R6, SR.T and PC.
It uses the lockable VRAM mapping; the unchanged native-fault path still
invalidates texture locks. 32-bit-swizzled VRAM, MMU, STRICT_MODE, GGPO and
other destinations remain outside the fast path. No PREF operation is elided.
See the separate function-contract notes for address and callback details.

Host differential/scheduler tests pass 383,280 cases, including overlap,
counter zero/wrap and successive scheduler residues. Device validation passes
16,384 batches, comparing all registers/FPU bits, flags, PC, cycles and output
against original handlers. Diagnostic native writes go to private memory;
the real VRAM writes occur exactly once, via the original handlers.
Evidence: `build-vita/devkit-logs/mvc2-copy-vram-shadow-reboot/20260831-114315/`.
The diagnostic screenshot is coherent and its cadence is 959 frames / 16
guest seconds. Division and cache shadow checks also pass again.

An earlier launch failed in the target loader with `Invalid system file`
before guest execution and left empty XML. Following the user's instruction
after a power dip, the exact DevKit was rebooted. The identical SELF hash
then launched successfully. This is not evidence of a patch-induced crash.
The loop script now handles empty XML and reports the loader error clearly.

Three diagnostic-free cold-start pairs, same binary/config except
`Mvc2CopyLoopHle` (default off), no SH4 downclock, accepted FPU/cache/division
changes enabled throughout:

| Copy HLE | Guest/wall ratios | Mean | Population SD |
| --- | --- | ---: | ---: |
| Off | 0.876424, 0.875657, 0.875465 | 0.875849 | 0.000414 |
| On | 0.881640, 0.882175, 0.882418 | 0.882077 | 0.000325 |

Retain: +0.711% throughput. All six runs submit exactly 959 guest frames in
the reference interval; all exits are expected fixed-window ProcessKilled.
No full-speed or audio-continuity claim is made. Original target configuration
is restored after each experiment, with the user's savestate unchanged.

Evidence/source snapshots: `build-vita/devkit-logs/mvc2-copy-vram-ab/20260831-114508/`.

- ELF: `926DC03C1818D9E0E6ADDF316C057CEC668E053525C1FCABE40726E7CE330934`
- VELF: `3622000B71992D12305F9D655C89E0F1DAAAD1DDD8D182C001D9D131FC407769`
- SELF: `3E8469F8A9818C72833583C42B9B93BB30B749168C379911DF728F9390E573E7`
- Adjacent SFO retains ATTRIBUTE2=12 and its original hash.

The prior accepted FPU profile's scheduler bars show the emulation `pthread`
running for 98.66% of a core-equivalent, versus 72.41% for the rendering/main
thread. The former migrated cores 747 times across 1,043 running intervals.
These ratios use the same capture timestamp units; clock metadata is not
used to infer actual MHz. `scripts/analyze-occam-threads.ps1` reproduces this
analysis. This supports prioritizing emulation-thread work, but does not
prove that changing affinity would help; the earlier core-2-only probe lost.

Post-change PC sample: `build-vita/devkit-logs/occam/mvc2-copy-vram-pc/`,
matching the A/B ELF, 5,066,234 application-range samples. SH4_TCB remains
38.67%, TA polygon parsing 10.63%, make_index 7.06%, ARM7 5.03%, pal4 texture
conversion 4.72%, and AICA_Sample32 2.30%. Copy entry/native body are 0.39% /
0.23%; the fallback FMOV read/write handlers are 0.11% / 0.07%. These are
waypoint rankings, not instruction-level measurements. The remaining copy
fallback is lower priority than the dominant emulation work.
RZR SHA-256: `524785C29EE6B51033307E77E6D75DC6D784C04FA8C078AB04CFC72525779139`.
The 120-second launch completes normally; its first-interval wall ratio is
perturbed by profiling and is not used as an A/B performance measurement.
Scheduler bars again show the emulation pthread at 98.61% of a core-equivalent
with 754 migrations, versus 68.82% for the rendering/main thread.

## Next evidence gate

1. Use the post-division profile for the next candidate. The vertex cluster
   still needs an algorithmic/vector or data-movement improvement; merely
   translating its small emission block to C++ lost performance. Any broader
   HLE region must retain intermediate scheduler/interrupt checks as well as
   complete register/memory residue and dynamic cycle accounting.
2. Capture audio or perform a supervised listening pass before claiming that
   game-speed improvement preserves pacing.
3. Exercise additional heavy matches/stages; this one savestate is not a
   whole-game correctness result.
4. Re-profile every retained material change and perform final LTO-on versus
   LTO-off A/B only after representative gameplay is full speed.
