# Vita per-game optimization instrumentation

This branch is a neutral starting point for disposable, game-specific Flycast
specializations. It contains no game addresses, product IDs or HLE handlers.
Compatibility with other games may be discarded only on a per-game branch.

## Deterministic workload

Set `Dreamcast.AutoStartContent` in `ux0:data/flycast/emu.cfg`. A savestate,
attract mode or a Target Manager controller recording can establish a repeatable
scene. Record input on the host with:

```powershell
psp2ctrl record controller <devkit-id> > captures\game-input.dat
```

Stop recording with Ctrl+C. Replay and collect console/XML/screenshot artifacts:

```powershell
scripts\devkit-game-loop.bat -Target <devkit-id> -RunName <game> `
  -InputRecording captures\game-input.dat -InputDelaySeconds 20 `
  -ObservationSeconds 90 -Screenshot
```

`-InputStartPattern` can delay playback until an emulator log marker appears.
Alternatively, `-CompletionPattern` terminates after a game-specific log marker.
The XML log is authoritative for distinguishing a normal exit, a kill and a
target-side crash.

## CPU profiling

Build with `-Symbols`, enable `[perf] RazorFrameSync=yes` and
`RazorAutoCapture=yes`, then start the listener before launching the workload:

```powershell
scripts\capture-vita-profile.bat -BuildDir build-vita -Mode pc-sample
```

The script stores an exact copy of the ELF beside the Razor capture and XML.
PC sampling ranks functions only: Razor charges samples to waypoint addresses,
so it cannot identify a hot instruction. Use PFT, JIT run counters and SH4
disassembly before making instruction-level claims.

For guest-block execution counts, configure with `-DVITA_JIT_RUN_PROFILE=ON`.
The default profile resets counters after 15 seconds, measures 15 seconds and
writes `jit_blocks.txt` plus `sh4_main_ram.bin`. The warmup, duration and RAM
dump size are CMake cache variables.

## Game-specific HLE

Add one source under `core/hw/sh4/hle/` on the per-game branch and register a
patch set with `HLE_REGISTER_PATCH_SET`. Every entry names its guest address,
verification length, CRC32 and native handler. The handler reproduces all
observable SH4 state, writes `next_pc`, and adds its complete dynamic guest cost
to `Sh4cntx.hle_cycles`. Keep scheduler observation points equivalent; a large
HLE region that skips interrupts can change game speed even when its arithmetic
is correct.

Enable the set with `[perf] HlePatches=yes`. A product-ID mismatch leaves it
inactive, and a byte mismatch falls back to the normal dynarec block.

## Loop discipline

Keep one measured hypothesis per commit or easily reversible patch. For each
iteration retain build identity, config, exact ELF, console/XML logs, timing and
a screenshot. Compare the same scene and reject changes that improve one metric
while changing game speed, audio continuity, rendering or control behavior.
Standalone/no-UI conversion is the final phase, after representative scenes are
full speed and stable.
