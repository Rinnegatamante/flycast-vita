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

## Rejected iteration: emulation-thread affinity on cores 0 and 1

Following the scheduler evidence above, tested explicit single-core affinity
after SDL audio thread creation, with automatic placement as the control.
SDK documentation and installed headers were checked for the affinity API.
No priority, physical clock or guest timing setting was changed. API calls
succeeded and read-back masks were 00010000 (core 0), 00020000 (core 1); the
control reported default 00000000. All three clock reads were 500 MHz.

One same-executable cold-start probe per configuration:

| Placement | Guest/wall ratio | Guest frames / 16 seconds |
| --- | ---: | ---: |
| Automatic | 0.885005 | 959 |
| Core 0 | 0.874174 | 959 |
| Core 1 | 0.858645 | 959 |

Reject both, with no claim about precise regression size from one run. The
affinity hook and headers are removed. All exits were normal fixed-window
ProcessKilled. Original target config is restored. Evidence and source:
`build-vita/devkit-logs/mvc2-affinity-cores01/20260831-115904/`.
ELF: `9C752FE02A6B3E79ADDD3158BC65992E3EBAD78254BD42831F34DF5E4F3B9FD8`.
SELF: `CD22A6E1D0AFB07AC737782276D8E775CF924845594C25F7CCADCB43986C2312`.

## Initial diagnostic hypothesis: slowdown when taking a hit

The user reports frame drops when the character is struck. That precise
event has not yet been correlated with JIT misses or texture updates. The
current code compiles missing blocks synchronously in rdv_FailedToFindBlock
and rdv_CompilePC. The latest PC sample is dominated by execution of existing
SH4 code; compiler-specific routines have very few samples, which does not
rule out a short first-use spike outside the sampled hit sequence.

A useful next diagnostic is per-frame compilation count/time, code-cache
clears and texture-update time, while replaying the same hit repeatedly
without restarting. No asynchronous compiler is implemented. The existing
compiler reads live PC/FPSCR and guest memory, uses static decoder/emitter
state, and publishes into shared block/cache structures. A background design
would require isolated compilation state, coherent input snapshots, safe
publication/invalidation and a correctly timed interpreter fallback. Merely
calling the present compiler from another thread is not safe.

## Resolved diagnostic: large stutters include synchronous compilation bursts

The aggregate profile above did not resolve the reported short stutters.
Three cold replays of the same slot-6 state now reproduce the largest wall-time
spikes at the same guest render-submission indices. The diagnostic records the
entire `rdv_CompilePC` call, rather than inferring its duration from PC samples.

| Guest submission ending interval | Interval wall time, three runs (ms) | Time inside compiler (ms) | Compiles starting in interval |
| --- | ---: | ---: | ---: |
| 215 | 86.15--89.52 | 61.79--64.36 | 407 |
| 531 | 123.51--127.48 | 96.75--99.85 | 516 |
| 611 | 40.74--41.68 | 20.57--20.96 | 111 |
| 1385 | 43.66--45.22 | 23.00--23.84 | 83 |

All 516 compiler entry PCs in interval 531 are first-seen in that capture,
including after normalizing virtual aliases to physical addresses. For
interval 215, 367 of 407 are first-seen; a RAM-page invalidation just before
that interval accounts for some repeated code. Each capture has four RAM
invalidation callbacks, no whole/temp code-cache clears and no block-check
failure events. This is not a periodic cache-capacity flush.

In interval 531, renderer processing occupies about 10--11 ms and texture
updates about 4 ms of the same wall interval. These execute on the other
thread and overlap one another; their durations must NOT be added to the
compiler duration. None of the large texture/process/draw initialization
spikes occur at interval 531: the largest of those occur before one second.

Across the three usable captures, 43 of 44 guest-submission intervals at
least 30 ms overlap compilation. All 25 intervals at least 40 ms between
successful `Present()` returns overlap compilation. The ~78% wall-time share
inside the compiler in interval 531 is direct evidence of a large synchronous
JIT contribution, not just temporal coincidence. These scopes include
preemption and are NOT exclusive CPU time; `Present()` return is NOT a
hardware display-flip/GPU-completion measurement. Individual attacks were not
visually labeled frame-by-frame, so this diagnoses the reproducible spikes
in the supplied sequence, not every possible hit or game slowdown.

The ordinary guest-submission median remains 18.37--18.94 ms (about 16.68 ms
is needed at full speed). Removing compilation hitches alone therefore does
not establish sustained full speed. The prior ranking of sustained execution
cost remains useful, but it cannot dismiss these first-use compilation bursts.
An asynchronous compiler or verified precompilation is now a justified
candidate, not an implemented fix. Isolation, code/FPSCR coherence, safe
publication and invalidation, and interpreter timing still need contracts.

### Capture contract and reproduction

- `scripts/build-vita.ps1 -Symbols -FrameTrace` enables `VITA_FRAME_TRACE`;
  default builds compile every hook out and omit the collector entirely.
- Add `FrameTrace = yes` and `FrameTraceSeconds = 30` under `[perf]` in the
  accepted `copy-hle.cfg` configuration. `frame-trace.cfg` is the local copy.
- Run `scripts/measure-mvc2.ps1` with `-PullFrameTrace -ObservationSeconds 65`.
  It preserves ELF/SELF/SFO/config/logs, verifies the trace origin against the
  current launch, rejects failed/overflowed exports, and restores config.
- The trace starts before the emulation worker on T1212N, threaded rendering
  only. It uses `sceKernelGetProcessTimeWide`, verified in the SDK guide and
  installed header: a shared per-process microsecond clock. It does not use
  adjustable wall-clock/RTC time.
- Separate single-producer append-only buffers (65,536 emulation and 131,072
  rendering records, 6 MiB total) publish immutable prefixes. Wrong-thread
  writes and overflow are counted and make the analysis invalid. The collector
  performs no file I/O, allocation, logging, wrapping or hot-path locking during
  collection; existing emulator logs remain enabled as in the control.
- Export occurs on a later emulated-second callback, after the 30-second
  deadline. Cross-deadline scopes are omitted; the analyzer excludes the
  first and last second. Post-capture export is not a performance sample.
- `scripts/analyze-mvc2-frame-trace.py TRACE --output DIR` emits separate
  guest-submission, vblank and successful-present interval CSVs plus JSON.
  It clips interval overlaps, unions nested same-kind scopes, keeps other
  categories separate, checks metadata/counts/timestamps, and counts first
  virtual/physical compile PCs. `--test` covers boundaries, overlapping spans,
  events spanning frames, aliases and capture-edge exclusion.

All three captures have zero overflow and wrong-lane events, 959 guest
submissions in the standard first 16-guest-second interval, normal fixed-window
ProcessKilled exits and coherent final screenshots. No audio-continuity claim.
The complete runs show an existing on-screen performance overlay; its readings
are not used for the timing analysis and the overlay was not changed here.
The initial 45-second host window ended before export, because it also
included launch/load overhead; its incomplete trace is rejected, not treated
as a crash. No DevKit reboot was needed in this diagnostic.

Two same-binary capture-disabled/enabled pairs measure first-interval wall
seconds of 18.901/18.858 and 18.370/18.355. Neither pair resolves an aggregate
slowdown from enabling recording. The ~2.9% variation between pairs precludes
claiming a precise zero-overhead result or a speed improvement from diagnostics.
No physical or guest clock settings were changed. These runtime on/off tests
do not compare a compile-time-disabled executable.

Evidence (relative to this worktree):

- Initial incomplete attempt: `build-vita/devkit-logs/mvc2-frame-trace/20260831-124000/`.
- Complete first trace and runtime source snapshot:
  `build-vita/devkit-logs/mvc2-frame-trace-complete/20260831-155832/`;
  results in `analysis/`, raw `1-on-trace.csv`.
- Repeated on/off pairs and two additional traces:
  `build-vita/devkit-logs/mvc2-frame-trace-repeat/20260831-160039/`;
  results in `1-on-analysis/` and `2-on-analysis/`.
- Exact diagnostic ELF: `1370B47152A11092CFE83F190B8C975A90827AAD6C89CB5AE4A752F4525CE00F`.
- Exact diagnostic SELF: `CA2E8BEAE48E3522045034EBBF1B16BB2593A5E3AD3533BDDDA85519E84FEDC9`.
- Raw trace SHA-256, first: `7542283C26E6568E8C252370ED6B7BCF20A1135EFA7B540F1C899BB456A98D6E`.
- Raw trace SHA-256, repeat 1: `E9925AD7688B8D3808A75617FF39E5B75A065E903103B7FDBBD3DA8152A04BA0`.
- Raw trace SHA-256, repeat 2: `CE09AD3846C91B51A7B833E9275E1A39D829D34938F0A3272DEFC3EF1D4CF17F`.
- Adjacent SFO still has ATTRIBUTE2=12 and SHA-256
  `0106D568E3DC48228DE1A4AF779D2B4DAD9FE5DA393170ED826037A44A4038F0`.
- Pulled target config after restoration matches its backup:
  `69C99B54B98C4A1308C6E420C579FFEFDB1FCB086719D4800FA1F439CC0D3C60`.
- Pulled slot 6 still matches the original user state:
  `10F53EDAAB5DD2E5F0CB70115F7164C2EC305FDE63F57DC21CAA8959E77A4DA5`.
  The target process is stopped after tests.

The final local rebuild has `VITA_FRAME_TRACE=OFF`, symbols on and LTO off.
`nm` verifies no collector or presentation-wrapper symbols remain; a host
compile test verifies that disabled macros discard even undeclared argument
expressions. The normal build launches slot 6 successfully and completes the
45-second smoke window (ProcessKilled), 18.311 wall seconds / 16 guest seconds
(0.873792). This single smoke result is not an optimization claim.
Evidence: `build-vita/devkit-logs/mvc2-frame-trace-disabled-smoke/20260831-161300/`.
Normal ELF: `DCCF62A66D0FCECA45EFBDD045F779DCDC5679215014C10F8B0E6A678AA06C31`;
normal SELF: `DB460CA0BA5567FBD67D4655BEBF8B96A153AE5149E249678A1B2C4D0080CC01`.

## 2026-08-31: compiler phases and rejected asynchronous backend

The asynchronous experiment is rejected, not a retained optimization. Its
source is archived in commit `b90dee023` and beside the exact experimental ELF;
the following cleanup removes its runtime code, extra arena and compiler
context indirection. Only compiler-phase diagnostics and the failed-launch
guard are retained. No persistent native cache or prewarming is implemented
by this experiment.

### Where compilation stalls

Same disc, slot 6, fixed RTC 1988906533--1988906549 and accepted copy-HLE
configuration. Additional trace scopes separate decode, SSA analysis,
register-allocation setup and emission. `reg_alloc` measures `DoAlloc` only:
allocation also occurs during emission. These remain wall-time scopes,
including scheduling/preemption, not exclusive CPU measurements.

Phase-only baseline: 18.345 wall seconds / 16 guest seconds (0.872172),
6,863 compiler entries in the 30-second trace. Total compile wall time is
1,407.461 ms: decode 316.831, analysis 292.491, allocation setup 26.505,
emission/finalization 527.953 ms. The remainder includes setup, registration
and other compiler work. At guest-submission interval 531:

| Scope | Wall ms |
| --- | ---: |
| Complete interval | 125.956 |
| Compilation | 99.750 |
| Decode | 22.523 |
| Analysis | 21.267 |
| Allocation setup | 1.822 |
| Emission/finalization | 38.407 |

Moving only the backend cannot remove the roughly 44 ms of decode/analysis
in that interval. Evidence:
`build-vita/devkit-logs/mvc2-compile-phases/20260831-163248/`.
ELF SHA-256:
`576A37EBFF789E0F623D17A53D900E30B7DF1D5E50F0645EBB398996FF68AC45`.

### Bounded prototype and measured results

The foreground decodes/analyzes an immutable IR snapshot. A worker emits
eligible integer blocks into a separate 2 MiB arena using isolated assembler,
allocator and call-parameter state. Until ready, a temporary entry interprets
the same optimized IR and charges the block's guest cycles. Publication,
links and block-manager changes occur only on the emulation thread, with
invalidation checks and instruction-cache maintenance. FPU, HLE, interrupt,
MMU, temporary and unprotected blocks stay synchronous. This is a restricted
prototype, not a general SH4 interpreter fallback.

On-device differential checks passed 89,088 arithmetic/register-context
cases, including boundary/random inputs and register aliases. They do not
establish memory-callback, interrupt, cache-reset or whole-game correctness.
An additional audit found that worker restart during code-cache/mainloop
recreation needs an explicit gate before further use. No cache clear occurred
in these captures, so this untested lifecycle case does not explain the
observed regression. The source is not shipping-ready.

| Variant / pair | Async off wall s | Async on wall s | Result |
| --- | ---: | ---: | --- |
| Initial isolated backend, validation enabled | -- | 19.029 | Slower than phase-only baseline |
| Explicit compiler contexts, pair 1 | 18.395 | 19.161 | Regression |
| Explicit compiler contexts, pair 2 | 18.441 | 19.117 | Regression |
| Shared fallback/scheduler entry | 18.435 | 19.215 | Regression |

All rows use 16 guest seconds; no CPU/guest clocks or renderer settings
changed. The repeated pairs regress about 3.77% in guest/wall speed; the shared
entry regresses about 4.06%. The latter improves interval 531 slightly against
its same-binary disabled control (138.233 to 134.079 ms), but does not eliminate
the hitch and remains above the phase-only 125.956 ms baseline. Compiler-state
isolation itself also raises synchronous backend cost, so runtime-off is not
equivalent to the pre-prototype executable.

The final shared-entry run submits 5,654 jobs, all ready at export, with 5,414
published and zero rejected by the report point. Median queue wait is 44 us;
6,724 interpreted entries mean most pending blocks execute only once before
native publication. This is not an interpreter runaway. Decode/analysis at
interval 531 increases from roughly 44 ms in the phase-only baseline to
roughly 62 ms with the worker. Scheduling, allocator and cache contention are
possible explanations, not separately established causes. Job wall times on
the worker overlap foreground scopes and must not be added to them.

Evidence, including raw trace/job CSVs, analyzed intervals, ELF, SELF, SFO,
config, complete appended logs and current-launch origin verification:

- Initial successful 2 MiB worker and validation:
  `build-vita/devkit-logs/mvc2-async-validate-small/20260831-164927/`.
  ELF `F4A64A82C0C31172E52B4607965C78CA85CDFA73EDB2F34F47579AD3FD912A30`.
- Repeated on/off pairs and source snapshot:
  `build-vita/devkit-logs/mvc2-async-ab/20260831-165444/`.
  ELF `F90B080FD2599604D0B7E95BBD1D7DD02446E070DD960CA6492D7F0E438A09C5`.
- Shared entry, repeat validation and source snapshot:
  `build-vita/devkit-logs/mvc2-async-shared-stub/20260831-170403/`.
  ELF `44DE5AC7A1E6B196BB877FE8CD71E1202A51BF99865F091AA22245E2F377ABF9`;
  SELF `E89131B6C041D60CA40DDA1A77AE3802AB7F0E0365B7EDF1F142EC16B6D10A75`.
- `scripts/analyze-mvc2-async-jobs.py` checks job counts and timestamps and
  reports queue, worker and publication latency; it remains an offline tool
  for the archived experiment.

Successful runs end by fixed-window ProcessKilled, with zero trace overflow
or wrong-lane writes. Inspected screenshots show coherent combat; audio was
not captured/listened to. Config is restored after each run and the original
state is never saved over. ATTRIBUTE2 remains 12 in the adjacent SFO.

The first attempt with a 10 MiB extra arena failed before content startup:
physical-page commitment for `Sh4RCB` failed in `vita_vmem.cpp:71` while
requesting a 16 MiB chunk. This is not evidence of a bad SELF or voltage fault.
Reducing the additional arena to 2 MiB allowed launch. Evidence:
`build-vita/devkit-logs/mvc2-async-validate/20260831-164554/` and launch XML under
`mvc2-async-validate-1-async/20260831-164607/`. Data pulled after that failed
launch was stale and is excluded. The launch helper now throws on a target
stop/abort/error before the observation completes, preventing the measurement
helper from accepting old files as results. No reboot was needed.

### Cache alternative: feasible, not yet implemented

The existing in-memory block cache already reuses compiled addresses. The
trace's 516 first-use PCs in interval 531 show why it cannot prevent that
burst. A persistent cache of emitted ARM could avoid re-emission across
launches, but a byte dump alone is unsafe:

- ARM `DynaRBI::Relocate` is currently empty. `call`/`jump` emit relative
  branches or absolute addresses to runtime helpers and other blocks.
- Constant memory operations can embed pointers from `_vmem_read_const` and
  `_vmem_write_const`. Guest code and SSA-folded literals must still match.
- Block-manager metadata, protection/invalidation bookkeeping, FPSCR,
  branch/relink information and code/config/build identity must be restored
  or reconstructed. Save states contain guest state, not this native cache.
- Reuse requires relocation records, or a strict rejection guard whenever
  any required host layout/address differs. Do not assume identical host
  addresses across launches or builds. The existing block `hash()` is not
  a sufficient byte-and-literal identity check for this purpose.

A smaller first experiment would record a learned manifest of guest PCs,
FPSCR and code/literal fingerprints, then validate and compile matching blocks
after loading the state but before starting emulation. This still emits ARM
at load time; it is prewarming, NOT a persistent native-code cache. It must
not execute the game, advance guest time, or change architectural state.
Changed/streamed pages and unknown moves must fall back to ordinary JIT.
Coverage, extra load time and stutter improvement remain to be measured.
Existing PC-only trace/profile lists are insufficient without these guards.

### Accepted tree and normal-build verification

The current tree removes the archived asynchronous backend completely,
including the additional 2 MiB arena, temporary interpreter entries and
thread-local compiler context indirection. Compiler-phase scopes remain
behind the existing `VITA_FRAME_TRACE` build switch. Their analyzer now has
explicit phase-boundary fixtures as well as the earlier overlap tests.

Rebuilt with symbols on, LTO off, frame tracing off and JIT run profiling off.
`nm` finds no trace collector or asynchronous-worker symbols. A host fixture
with undeclared scope arguments compiles/runs when tracing is disabled;
the analyzer tests and PowerShell parser check pass. The 45-second normal
smoke run loads slot 6, advances the fixed 16 guest seconds in 18.304 wall
seconds (0.874126), and ends by the planned ProcessKilled. Its screenshot
shows coherent combat. This is a restoration smoke test, not a new speedup
or an audio/full-speed validation.

Evidence: `build-vita/devkit-logs/mvc2-async-rejected-smoke/20260831-172414/`;
launch XML/screenshot: `mvc2-async-rejected-smoke-1-normal/20260831-172427/`.
Normal ELF SHA-256:
`AF97BA51ABB925C4FC4F361C64E276B010ABAC414E5FCB7CA362669F717517C0`;
normal SELF:
`8CD12D57FD5B4F8193AF93DB6CA17234A040C7A282C4B030539C5AB5FB502832`.
Adjacent SFO still has ATTRIBUTE2=12 and SHA-256
`0106D568E3DC48228DE1A4AF779D2B4DAD9FE5DA393170ED826037A44A4038F0`.
Final target readback verifies unchanged configuration
`69C99B54B98C4A1308C6E420C579FFEFDB1FCB086719D4800FA1F439CC0D3C60`
and unchanged slot 6
`10F53EDAAB5DD2E5F0CB70115F7164C2EC305FDE63F57DC21CAA8959E77A4DA5`.
The target process is stopped after the test.

## Native ARM persistence experiment (2026-08-31)

Continues accepted tree `b9b53b966`. This is native-code persistence, not
load-time recompilation. The existing Vita SELF already uses `NOASLR`
(`vita-make-fself -na`); disabling ASLR was not a new optimization.

The file keeps emitted ARM bytes at their original CodeCache offsets, plus
plain block/relink metadata. It is accepted only for T1212N with matching
linked GNU build ID, generated helper prefix, host code/context bases,
complete memory-handler layout and code-generation settings. MMU and JIT
run-counter builds are excluded. Code checksums and bounded records are
validated before publication. Only self-generated local caches are intended;
these checks are not authentication of executable files from third parties.

At first use, full protected source pages must match, covering both decoded
instructions and SSA-folded literals. Cached page hashes are valid only while
write protection remains active and are revoked on RAM writes/cache resets.
Each native slot is consumed at most once per cache lifetime; changed pages,
already-used slots and unknown blocks fall back to the original JIT. The
three accepted HLE entry PCs are excluded, retaining their original CRC gates.
No guest execution, clock change or speculative memory callback is involved
in loading or checking a cached block. New blocks do not require allocating a
second executable arena.

Configuration under `[perf]`: `Mvc2NativeCache` enables replay;
`Mvc2NativeCacheRecord` performs separate cold training (takes precedence);
`Mvc2NativeCacheSeconds` bounds training/reporting to 5--120 seconds, default
30; `Mvc2NativeCacheValidate` regenerates each hit at its original address,
compares bytes/metadata/context, then restores the original bytes before
execution. Validation is diagnostic and must not be included in speed claims.
The file is `ux0:data/flycast/data/mvc2_native_cache.bin`. A changed executable
requires retraining. The existing slot-6 state is never overwritten.

### Rejected intermediate implementations

First implementation: ELF
`F6376EAE8AF1A460A9F14D8B8325A0F3F2C3F3002C6A3F33617D9BFDA125E7E6`,
build ID `1cd71c8afae14a70db5afa68dff1bf8cc3d8e1b0`.
Cold training saved 6,688 blocks / 717,328 native bytes, including 976 FPU
blocks, without saturation or I/O errors. Regeneration checked 6,074 hits
with zero byte/metadata/context mismatches. Reading and indexing cost about
2.14 seconds before gameplay. Evidence:
`build-vita/devkit-logs/mvc2-native-record/20260831-175431/` and
`mvc2-native-validate/20260831-175702/`.

Two same-binary pairs in `mvc2-native-ab/20260831-180003/` gave off
18.449/18.441 s versus on 18.417/18.350 s per 16 guest seconds. This small
aggregate change did not resolve stutter: interval 215 worsened from about
90.6 ms to 133.9--138.3 ms; interval 531 remained about 125--128 ms even though
all 516 blocks avoided decoding/emission. Original source is preserved in
that evidence directory's `source-v1/`. Do not retain this version.

Second implementation memoizes protected-page checks. ELF
`0E2D03FA73FEC3004B70A4D69FA60CA34AAE58D4B34525A83C9E388FF2718F61`,
build ID `31ffc9343e006cef0eb821bca892cf8ce9b6d945`.
Training: `mvc2-native-pages-record/20260831-221630/`, 6,654 records /
712,824 native bytes, 947 FPU blocks; cache SHA-256
`90A7FA490DF5CDD234793B708A9E8711EA358DE6FA5AD5E9E7E0A202E70E13FF`.
Validation: `mvc2-native-pages-validate/20260831-222315/`, 6,074 checked hits,
zero failures and only 135 page hashes. Screenshot shows coherent combat;
audio not captured. The same-binary repeated comparison is
`mvc2-native-pages-ab/20260831-222549/`: off 18.702/18.277 s, on 18.101/18.141 s.
Interval 215 still regressed (90.98--91.80 to 118.51--122.43 ms); interval 531
improved from 127.86--130.63 to 103.91--107.30 ms. This intermediate version
is not retained despite its aggregate improvement.

This exposed an independent indexing defect in the prototype: VitaSDK's
`std::hash<unsigned long long>` casts to 32-bit `size_t`, dropping the PC
half of `(PC << 32) | FPSCR`. All PCs with the same FPSCR therefore collide.
The corrected hash folds both halves; the host fixture explicitly checks
12,000 different PCs in a simulated 32-bit hash domain. Page-check wall time
at interval 531 was already below 1 ms, so repeatedly hashing RAM no longer
explained the remaining ~79 ms lookup/publication wall time.

A forced-reset diagnostic also stopped at guest second 3:
`mvc2-native-pages-reset/20260831-222032/` and corresponding launch XML.
Calling ResetCache inside UpdateSystem removes the current block before
`intc_sched` calls `rdv_DoInterrupts` to recover its guest PC. That function
dereferences the now-missing RBI; the fault handler's own dereference of the
bad address obscures the original fault. The diagnostic now requests a stop,
returns completely from native code, and only then resets/restarts the cache.
The failed test is excluded from timings; target config was restored, and no
devkit reboot was used to mask this software lifecycle error.

### Corrected index and safe reset validation

Trace ELF `2A35072BB9987CD52A7F8E39CA18D86A0E8BA96097AC17BB31CE94CA1DF2109C`;
SELF `FA57F89F6E605086539844CB5826D8BB327B0374932B8A066018012ED10434FE`;
build ID `7198ed43797cbce47df377ce41c0a882f0f34230`. Training evidence:
`mvc2-native-hash-record/20260831-223455/`, 6,688 records / 717,328 native
bytes / 976 FPU blocks. Cache SHA-256:
`4EA0BEE29C7AFEACE10B2232A26218FD451E4FE674C057506730791F219D3827`.

The corrected reset test completes the 55-second window with planned
ProcessKilled: `mvc2-native-hash-reset-clean/20260831-223932/`. It validates
4,243 hits before resetting and 5,431 afterwards, zero failures. After reset,
111 source-page rejections correctly take the ordinary compiler. Loading and
indexing take 160.1/160.6 ms, down from about 2.14 s with the colliding hash.
The fixed 16-guest-second interval still has 959 render submissions. Its wall
time (20.474 s) includes diagnostic recompilation/reset and is NOT a speed
measurement. No guest-time rewind or savestate replacement is used.

The preceding reset attempt with frame tracing on completed gameplay but
overflowed the 65,536-event emulation buffer by 11,637 events:
`mvc2-native-hash-reset/20260831-223658/`. The measurement helper rejected
that trace as intended. The repeat keeps regeneration checks enabled but
disables trace recording at runtime, avoiding incomplete timing evidence.

### Selective interpreter proposal: candidate discovery, not an allowlist

The user's suggested policy (retain pre-fight native blocks, interpret
selected newly encountered fight blocks) is implementable but not yet
measured. Removing compilation cost can increase repeated execution cost.
The decision requires both first-use compilation time and post-birth run
counts, followed by actual interpreter-vs-native timing. In particular, the
existing SH4 interpreter charges `CPU_RATIO=8` per instruction, unlike the
dynarec's block cycle accounting. Direct use of its Step/Run interface is not
a timing-preserving fallback and must not be counted as a speed improvement.

`scripts/analyze-mvc2-stutter-candidates.py` extracts PCs whose compilation
overlaps >=30 ms guest-submission intervals with >=10 ms compilation overlap.
It reuses trace validation, clips scopes to interval boundaries, and emits
an explicitly non-executable candidate CSV. The first extraction contains
2,043 PCs from `mvc2-native-ab/20260831-180003/1-off-trace.csv`.
Evidence: that directory's `stutter-candidates/`.

A historical cross-reference to the original slot-6 JIT profile (another
build, guest seconds 15--30) illustrates why a blanket exclusion is unsafe:
220 of interval 531's PCs show 12 later runs, whereas `8C12A1FA` shows 15,285,
`8C12A212` 14,669 and `8C12A224` 7,217. These are preliminary address matches,
not current-build frequency/cost measurements. Missing matches mean unknown,
not zero runs. Runtime selection would need a revision, PC, FPSCR and source
fingerprint gate, plus unchanged registers, exceptions and cycle/interrupt
semantics. No blanket interpreter rule has been enabled.

### PAL/50 Hz proposal: no configuration change in the benchmark

Flycast exposes Broadcast=PAL and non-VGA Cable choices, including TV
Composite (local `Dreamcast.Broadcast=1`, `Dreamcast.Cable=3`). A genuine
50-Hz game mode raises the per-frame budget from 16.67 to 20 ms; it does not
automatically reduce all emulation work by 20%, and a simple presentation cap
does not remove SH4 compilation bursts. Frame-based game logic without PAL
compensation could instead run at 5/6 of the intended speed. A 50-Hz source
also has uneven presentation cadence on the current 60-Hz Vita output.

Critically, `core/serialize.cpp` restores Broadcast/Cable/Region from the
state and `core/hw/pvr/pvr.cpp` restores video registers. Changing config and
reloading slot 6 is therefore not a valid PAL-mode experiment. The actual
scanout period is derived from SPG_LOAD/FB_R_CTRL in CalculateSync. Testing
the supplied USA revision would require a separate cold boot and verification
of actual refresh, timer, animation and audio pacing. Its PAL support has not
been established here; no alternate ROM, video-timing patch or PAL benchmark
has been substituted for the current workload.

### Native cache v3: repeated A/B completed before the manual PAL session

Evidence: `build-vita/devkit-logs/mvc2-native-hash-ab/20260831-224101/`.
Same slot-6 workload and 16 guest seconds (RTC 1988906533--1988906549),
959 guest renders, trace overflow zero. Off wall times: 18.336 / 18.315 s;
on: 17.710 / 17.715 s. Ratio of mean throughputs improves about 3.46%,
to about 90.3% guest-time speed, not full speed. Matched submission intervals:

| Interval | Off 1 / 2 (ms) | On 1 / 2 (ms) |
|---|---|---|
| 215 | 91.403 / 90.955 | 38.962 / 39.209 |
| 531 | 128.688 / 129.179 | 33.232 / 33.170 |
| 611 | 42.145 / 41.862 | 22.103 / 22.403 |
| 1385 | 44.537 / 44.746 | 21.391 / 21.291 |

On-1 compile-entry costs for these intervals are 15.797, 8.413, 2.053,
1.326 ms; interval 531 has 516 native hits. Remaining-candidate extraction
with >=30 ms intervals and >=10 ms ordinary-JIT overlap returns no PCs;
lowering the explicitly selected compile threshold to 1 ms finds 40 ordinary
compilations at interval 215. This is not evidence of zero JIT cost or a
validated interpreter exclusion list. See `remaining-stutter-candidates/`
and `remaining-stutter-candidates-1ms/` beneath the A/B directory.

The wrong-build negative test completed, observing guarded ordinary-JIT
fallback: `mvc2-native-reject-bad-build/20260831-224827/`. The user then
requested a manual PAL cold boot. The automated pipeline was stopped during
`mvc2-native-reject-truncate/20260831-224955/`; although rejection appears in
the log, the interrupted run is not a completed test. Bad-offset, bad-page,
bad-native and normal/non-traced cache record/replay tests remain pending.
The original configuration and verified v3 trace cache were restored before
preparing the separate manual session. Do not mark this candidate finally
accepted or resume a timed test while the user's manual session is active.

### Manual PAL cold boot, 2026-08-31 22:57 Europe/Rome

User requested a new cold boot without loading any state and with the game
left running to create a separate PAL state. The optimization skill's
configuration backup and executable/evidence requirements are retained;
the normal fixed-window helper is deliberately not used because it kills
the process. `scripts/start-mvc2-manual.ps1` refuses an already-running
Flycast, backs up and verifies configuration, launches a hidden host runner
without timeout or automatic input, and leaves the connection alive.

Build: `scripts/build-vita.ps1 -BuildDir build-vita-cache-normal -Symbols`.
LTO, frame tracing and JIT run profiling disabled. The only new diagnostic
logs the actual PVR timing when CalculateSync runs, behind the default-off
`perf.Mvc2VideoTimingLog` switch; it changes no video registers or cycles.
Native cache load/record/validation are disabled for this new workload.
Accepted HLE and FPU-pair settings are retained; SH4 downclock remains 1.0.

- ELF SHA256: `1286D6A2F7A34BB4CC94C66D2C318C54EBABFBE79468255EF0082D3516DECE88`.
- SELF SHA256: `B73BC6F3F0DEF262C579F2C19B5D0FC6CFAC367C8F12A60AA1C5677FAB27468E`.
- GNU build ID: `c11268322d7e7b5ae659c02257a6d7201afae51c`.
- Adjacent `sce_sys/param.sfo` verified `ATTRIBUTE2=12`; SHA256
  `0106D568E3DC48228DE1A4AF779D2B4DAD9FE5DA393170ED826037A44A4038F0`.
- Config template:
  `build-vita/workloads/marvel-vs-capcom-2/experiments/manual-pal-coldboot.cfg`.
- Readback config SHA256:
  `AECE5EEB4B6201C7567961E2AFD92BDBF199E9944593CE53AC46270E8EC67BAF`.
- Broadcast=1 (PAL), Cable=3 (Composite), requested Region=2. The ROM's
  region check correctly selects USA (1); Broadcast remains PAL.
- AutoLoadState=no, AutoSaveState=no. SavestateSlot=7 is **Slot 8 in the UI**,
  previously unused. Existing `_6.state` is not loaded or overwritten.
- Evidence and preserved ELF/SELF/SFO/config:
  `build-vita-cache-normal/devkit-logs/mvc2-pal-coldboot/20260831-225708/`.
- Host runner PID 38944; target process PID `0x029f2dfb` at launch.

Actual timing confirmed from the current-run section of `live-flycast.log`:
at 00:02:985, `refresh_hz=50.0000 field_cycles=4000000 lines=625 hcount=863
interlace=1 pal=1 ntsc=0 broadcast=1 cable=3 region=1`. The immediately
preceding 119.6627-Hz line is a transient during consecutive register writes,
not the final mode. Subsequent steady-scene counters advance 50 guest renders
per emulated second. Vita output remains 60 Hz. Thus this exact US disc does
enter a genuine 50-Hz guest video mode with the selected configuration;
gameplay timer, animation/audio pacing and heavy-scene performance still
require the new state and a separate comparison.

Visual checks: `startup.bmp` shows the game's save-complete screen;
`running.bmp` shows a match and Flycast's menu with Slot 8 selected. No input
was injected by the agent. Game remains open under user control, no timed
termination or configuration restoration is scheduled. Audio was not captured
or evaluated. This is a successful manual launch, not a full-speed claim.

The user subsequently confirmed the new state was generated. Acquired
`_7.state` (UI Slot 8) to
`build-vita/workloads/marvel-vs-capcom-2/pal-framedrop-slot-7/` with the
current `manual.cfg`. Size 10,060,316 bytes; SHA256
`9104C81F6D17A591D63F127B25E59FE8DDBB3F10FBB82808666007251948E50C`.
Two independent target readbacks match. The old `_6.state` was also read
back and still has SHA256
`10F53EDAAB5DD2E5F0CB70115F7164C2EC305FDE63F57DC21CAA8959E77A4DA5`.
The active 50-Hz session has not been terminated. A cold replay of the new
state and its guest-RTC interval are not yet measured; do not reuse the
slot-6 hard-coded RTC gate in `measure-mvc2.ps1` for this workload. Establish
a separate PAL baseline and keep the old 60-Hz evidence distinct.

### PAL slot-8 workload and SMC correction

The new state deterministically starts at RTC 1989022267 and produces exactly
800 guest renders over the selected 16 guest seconds. The first normal,
non-traced discovery took 16.160 s wall time (99.01%); evidence:
`build-vita-cache-normal/devkit-logs/mvc2-pal-discovery/20260831-230616/`.
The SMC-off traced baseline then showed that 50 Hz alone does not fix first-use
stutter: p50 19.317 ms, p95 25.372, p99 33.799, max 124.459 and 28 guest-frame
intervals >=30 ms. The worst interval contained 89.48 ms of compilation wall
scope. There were 6,942 ordinary compilations and 1,559.271 ms in their union
over the bounded trace. Evidence:
`build-vita/devkit-logs/mvc2-pal-trace-baseline-complete/20260831-231004/`.

The first PAL cache recorded 6,758 records / 722,468 native bytes and covered
the 2,411 unique PCs attributed to >=30 ms / >=10 ms-compile intervals. Its
SHA256 was `3D549602F9004B1500C8F49726F9668F039F2800A522014855B071F76CFB50AC`.
Regeneration validated 6,669 executed hits with zero failures. Two replays
gave 15.870 / 15.881 s for 16 guest seconds, versus off values
16.247 / 16.280. More importantly, native replay reduced max 124.459 ->
41.528/41.604 ms, p99 33.799 -> 27.356/27.191 ms, and >=30 ms intervals
28 -> 6/4. Forty remaining ordinary-JIT PCs cluster in frame 214; PC-only
coverage is insufficient because cache identity also includes FPSCR and
source-page state. Evidence:
`build-vita/devkit-logs/mvc2-pal-native-ab/20260831-231541/`.

These results demonstrate the cache mechanism and stutter cause, but are
**superseded for release acceptance**: the user identified that every config
had `Dynarec.smcChecks=0` (Off). MVC2 requires at least Reduced for code changes
between fights. The interrupted PAL negative-guard run is excluded. The code
now forces `DynarecSmcChecks>=1` for `T1212N`, while preserving an explicit
Full setting, and all PAL configs request 1. This changes cache identity and
requires new training, validation, A/B and a between-fight stability run.

Reduced is already selective: the ARM backend sets `block_check` only when
`RuntimeBlockInfo::read_only` is false. Protected pages use write faults and
block invalidation with no inline SMC check. Reduced compares only the first
four guest bytes for writable blocks; Full checks the whole block. A further
adaptive policy cannot disable both checks and page faults before learning a
write, because then the change is unobservable. A beta trainer should measure
page writes and block-check failures, re-protect cold pages, retain Reduced or
Full only for demonstrated hotspots, and key learned classifications by ROM,
PC, FPSCR and source-page hash.

The current non-MMU path has an important limitation: `rdv_BlockCheckFail`
clears the whole JIT cache, resets the failure count and clears
`smc_hotspots`; the >5-failure hotspot promotion only operates in the MMU
branch. Do not claim an adaptive non-MMU policy until that behavior is changed
and tested across fight transitions.

Reduced rebuilds after the correction:

- trace ELF/SHA256 `E74447BB7FC85FA596B9064568B206F92563DC2E27FA230E467402E1449AB154`,
  SELF `FD39CA7225EB9BE7CBB9AA4CAA48E519E5E8E20E845CCBE92D1E9AB82B0BF56F`,
  GNU build ID `496b5d459cbcb85e7761138f11f805e8accba6e5`;
- normal ELF `FA0EF448E7ADDCF242532732390C7FE45E4C31D754414B3A7ECEA61493C628F1`,
  SELF `D5105D2571BF3CF34EDA0A3764998FE18E917C72D8849D81C06469372363930C`,
  GNU build ID `dee236f3c70e343844405b052bba84c3321fce18`;
- both have adjacent `sce_sys/param.sfo` with ATTRIBUTE2=12 (SFO SHA256
  `0106D568E3DC48228DE1A4AF779D2B4DAD9FE5DA393170ED826037A44A4038F0`).

The first corrected normal baseline is 16.131 s / 16 guest seconds (99.19%),
800 renders. The corrected traced baseline is 16.169 s (98.95%), with p50
19.285, p95 25.291, p99 33.449, max 122.989 and 28 intervals >=30 ms. Thus
Reduced has no measurable regression in this workload and does not itself
remove the compilation bursts. Evidence is under
`mvc2-pal-smc-reduced-normal-baseline/20260831-232923/` and
`mvc2-pal-smc-reduced-trace-baseline/20260831-233037/`.

### Reduced native cache completion

The cache was retrained after enabling Reduced SMC. The corrected trace cache
contains 6,758 records, 722,468 native bytes and 961 FPU variants; SHA256
`0440E003D61D62138DBA561120DF2AAEC1628A3520F4E7C4496F2A76BA61E952`.
Validation loaded all records and executed 6,669 hits with zero source rejects,
zero regeneration failures, 69 used-field rejects and 140 page checks. All five
negative guards (wrong build, truncated file, invalid native offset, changed
source page and changed native bytes) safely fell back to ordinary JIT and
completed the 800-render workload.

Two corrected trace replays reduced max frame interval from 122.989 ms to
41.18/41.07 ms, p99 from 33.449 to 27.32/26.98 ms and intervals >=30 ms from
28 to 7/6. Cache-attributed compile intervals fell from 25 to 4/3. Four RAM
invalidations were observed; neither replay had a cache validation failure.
Evidence is under `mvc2-pal-smc-reduced-native-record/20260831-233220/`,
`mvc2-pal-smc-reduced-native-on/20260831-233622/` and the five
`mvc2-pal-smc-reduced-native-reject-*` directories.

The normal release-style cache was separately recorded and validated: 6,758
records / 722,468 native bytes, SHA256
`0B123030155C5DC6A6ED3EE26F7848B014055C148F6EA9D24D6F61C44D7B39DD`,
GNU build ID `dee236f3c70e343844405b052bba84c3321fce18`. Its alternating A/B wall
times were 16.049/16.068 s off and 15.881/15.875 s on: mean end-to-end gain
about 1.12%. Evidence:
`build-vita-cache-normal/devkit-logs/mvc2-pal-smc-reduced-normal-native-ab3/20260901-101923/`.
This retains the cache primarily for its large peak-stutter reduction, not as
a claim that average rendering cost is solved.

### `make_index` vertex-validation bypass rejected, 2026-09-01

A diagnostic build counted the existing invalid-coordinate predicate without
changing its result. Over the deterministic PAL slot-8 workload it performed
11,619,366 vertex validations and rejected zero vertices. Exact diagnostic
artifacts and logs are preserved under
`build-vita-mvc2-vertex-diag/devkit-logs/mvc2-pal-make-index-diagnostic/20260901-103219/`;
ELF SHA256 `B915A1E5FE11ADE368DCAF7E5BC8167C83A19A567DCF0ECBE13C7D995D41F7D1`.
This establishes only that this savestate did not need the guard, not that all
stages and transitions are free of invalid vertices.

The clean control used ELF SHA256
`C2231B963E35E3D8DB9B45579C1E47CC8712376045A3E803E22EBEA68F2AF50F`
and measured 16.054 / 16.071 / 16.077 s (mean 16.0673 s), with the expected
RTC and exactly 800 renders in every run. A first runtime-gated bypass enlarged
`make_index` from 295 to 325 ARM instructions and regressed to a 16.0960-s
mean. It was superseded by the proper specialized build, which removed the
guard at compile time and reduced `make_index` to 209 instructions, including
14 fewer VFP comparisons. That build measured 16.072 / 16.077 / 16.072 s
(mean 16.0737 s), 0.04% slower than control and therefore indistinguishable
from noise rather than a gain. Screenshots showed coherent geometry, but the
candidate has no performance justification for accepting its correctness risk.

Evidence is under
`mvc2-pal-make-index-control/20260901-104129/`,
`mvc2-pal-make-index-skip-validation/20260901-105021/` and
`mvc2-pal-make-index-compile-time-skip/20260901-105659/`. The diagnostic and
bypass source/options were reverted. Sampling rank or source-level sample
counts must not be converted into the claimed 11.55 ms/frame or 82% instruction
share; the on-device elimination test directly contradicts that estimate.

### Page-fault adaptive SMC rejected, 2026-09-01

The corrected Reduced trace contains four first-write RAM invalidations at
`0x0c196344`, `0x0c1a4eb0`, `0x0c198324` and `0x0c12fd20`, and no inline
block-check failure. This does not mean SMC is unnecessary: after the first
fault the current non-MMU manager deliberately leaves each page writable, so
later writes are guarded by per-block Reduced checks rather than new faults.

A conservative candidate re-protected a written page when its first new
single-page block was compiled, then classified the page as hot after four
observed write faults and returned it to existing Reduced behavior. Cross-page
blocks were excluded. The exact trace ELF SHA256 was
`8C07AC716FC1BCF5FE2E6B980FA399E5D70829AD4247E7835161E06906E5A5F4`
(SELF `C42C8FAD9CC9DEFFCCFE87871E4B16CE981E8BE9B8F54010A70268EFCE022AE4`),
with adjacent ATTRIBUTE2=12 SFO.

All four pages reached the hot threshold: `0x0c196000`, `0x0c1a4000` and
`0x0c198000` did so in the first 0.9 s after trace start; `0x0c12f000` by
5.3 s. Compared with Reduced control, invalidations rose 4 -> 16, their total
wall scope 1.689 -> 263.415 ms and maximum 1.344 -> 106.148 ms. Guest-frame
mean rose 20.354 -> 20.573 ms, p99 33.449 -> 34.217 ms, maximum 122.989 ->
193.500 ms and intervals >=30 ms 28 -> 37. Frames 213 and 214 contained
119.51 and 74.65 ms of invalidation overlap respectively. There were 7,058
compilations versus 6,958 in control.

Evidence:
`build-vita/devkit-logs/mvc2-pal-adaptive-smc-frame-trace-long/20260901-111637/`.
The earlier 40-s attempt did not export its trace before planned termination
and is excluded. The source and build option were reverted. Re-protection is
not a viable replacement for Reduced inline checks on Vita for these pages;
the fault/permission transition itself creates larger hitches. Any future
per-block classification must first collect exact changed-block evidence and
cannot treat ordinary compilation or a page write as proof that every block on
the page modifies itself.

### Writable-SMC native cache v2 accepted, 2026-09-01

The v1 Reduced cache deliberately omitted blocks compiled after their RAM page
became writable. All 40 ordinary compilations left in frame 214 were on page
`0x0c12f000`. Format v2 records those blocks with `NeedsSmcCheck`, retains the
ordinary JIT's inline Reduced/Full check, and keys lookup by exact guest block
bytes. Protected records retain the existing protected-page hash. Multiple
PC/FPSCR/source-state variants can coexist; a writable record is eligible only
while at least one covered page is actually writable, preserving the
`read_only` specialization used by `AnalyseBlock`.

That last condition was established by a failed intermediate validation. The
first v2 selected the writable variant of `8C1A4A00` while its page was still
protected and correctly stopped on a regeneration mismatch. Evidence:
`build-vita/devkit-logs/mvc2-pal-smc-writable-native-v2-validate/20260901-113629/`.
After adding the protection-class gate, the trace cache validated 6,846
executed hits with zero failures. Its properties are:

- 6,939 records / 750,396 native bytes / 988 FPU variants;
- 181 writable-SMC records, including 59 on page `0x0c12f000`;
- cache SHA256
  `9FB3753293EF46E477F819BA14A697A0D24921890CCF68FBA4AED8E23DE53916`;
- trace ELF SHA256
  `872CA6EBCD1AD176E6E887822A190DDEF4407C0475725A2E3EC524AFBF78BC33`,
  SELF `42E613BC4A2C8A688113DAD12C9BED697D85FF863FA10E70376C8FC6A40986A3`,
  GNU build ID `b9bd35a45ff12c794c55c9f8dc3ec1c973160b07`.

Alternating traced A/B evidence is under
`build-vita/devkit-logs/mvc2-pal-smc-writable-native-v2b-ab/20260901-160821/`.
The two off runs had max guest-frame intervals 123.190/124.099 ms, p99
33.985/33.440 ms and 28/26 intervals >=30 ms. The two on runs reduced max to
40.936/40.597 ms, p99 to 26.880/26.948 ms and both had six intervals >=30 ms.
At frame 214, off took 102.443/101.580 ms with 436 ordinary compilations and
76.631/75.954 ms compile overlap. On took 31.286/31.505 ms with 436/436 native
hits, zero decode/emit and 6.646/6.682 ms cache-lookup overlap. This removes the
40 v1 misses and improves that residual hitch by about 9.6 ms relative to v1.
Both cache-miss-only candidate analyses returned zero PCs at the >=30-ms / >=10-ms
compile threshold.

All five v2 negative guards passed on target: wrong build, truncation, invalid
offset, changed source hash and changed native bytes all fell back to ordinary
JIT and completed their deterministic workload. The good cache was restored.
Evidence is in the five `mvc2-native-v2b-reject-*` directories dated
20260901-161724 through 20260901-162134.

The separate normal build has ELF SHA256
`FD3F1725CC1C839B5EE0C5106B600DEB0578D251E8FF64B4486EDCB4DA46012E`,
SELF `D3585FA833B34224837FAD92E1A60CB1C8DCD0E6306E2854EA748C2ABA60861B`
and GNU build ID `ca09a10644046d73e545c4abf0fa9fefe601ab7a`. Its adjacent SFO retains
ATTRIBUTE2=12. The normal cache has identical coverage and SHA256
`1116D9C0D78026563156A0CF0F1D7660D6C5DDFAA0B0691AA0BF63120B08AC97`;
6,846 executed hits regenerated with zero failure. Three normal off runs were
16.092/16.105/16.110 s (mean 16.1023); three on runs were
15.874/15.874/15.867 s (mean 15.8717), a 1.43% wall-time reduction and about
1.45% higher throughput. Evidence:
`build-vita-cache-normal/devkit-logs/mvc2-pal-smc-writable-native-v2b-normal-ab/20260901-163242/`.

A 180-second normal-cache run remained alive until planned termination. Its
current-run log reported 6,935 hits, 44 misses, 73 source/protection-state
rejects and zero failures; no cache-disable or error line was present. Evidence:
`build-vita-cache-normal/devkit-logs/mvc2-pal-smc-writable-native-v2b-normal-long/20260901-163842/`.
This is temporal stability from one savestate, not proof of between-fight or
whole-game coverage. Reduced SMC remains enabled and no check was removed.

## Next evidence gate

1. Extend the beta trainer across played matches, stages, character teams and
   between-fight transitions. Merge only exact build/config/PC/FPSCR/source
   variants and repeat regeneration plus corruption guards on the aggregate.
2. Instrument actual Reduced-SMC failures and writable-page invalidations over
   fight transitions before implementing an adaptive policy. Do not infer hot
   SMC blocks from ordinary JIT compilation events.
3. For sustained execution cost, retain the post-division profile. The vertex cluster
   still needs an algorithmic/vector or data-movement improvement; merely
   translating its small emission block to C++ lost performance, and removing
   `make_index` validation was neutral. Any broader HLE region must retain
   intermediate scheduler/interrupt checks as well as complete register/memory
   residue and dynamic cycle accounting.
4. Capture audio or perform a supervised listening pass before claiming that
   game-speed improvement preserves pacing.
5. Exercise additional heavy matches/stages; this one savestate is not a
   whole-game correctness result.
6. Re-profile every retained material change and perform final LTO-on versus
   LTO-off A/B only after representative gameplay is full speed.
