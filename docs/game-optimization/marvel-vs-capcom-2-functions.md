# MVC2 US v1.001: executable analysis

This is a reconstruction from the user's disc and the measured RAM dump, not
recovered original source or an automatic SH4 decompiler result.

## Inputs and tools

- User disc: root worktree `marvel/`, GDI plus all three referenced tracks.
- IP.BIN identifies `T1212N V1.001`, boot file `1ST_READ.BIN`.
- Boot extent: absolute LBA 548116, 1,809,817 bytes, loaded at `0x8C010000`.
- Boot SHA-256: `862E43CDD42FAB395E2D71C8F1D9435F41AF79380FE3B2820CB9BA25FE1F234F`.
- `scripts/extract-mvc2-boot.ps1` reads Mode 1 sectors and the ISO directory;
  no descrambling applies to this GD-ROM (same rule as reios.cpp).
- IDA 7.0, SH4 little-endian processor module. The installed Hex-Rays modules
  cover ARM/x86, not SH4. The IDC script does not require a decompiler.
- Generated private artifacts (not committed game bytes):
  `build-vita/workloads/marvel-vs-capcom-2/disc-analysis/`, including the
  `mvc2-us-v1001.i64` database and `mvc2-hot-functions.txt` disassembly.
- Exact comparison with the previously profiled RAM dump: zero mismatches
  for 8 bytes at 8C02DEC6, 70 at 8C0354D4, 140 at 8C1291E2, and 220 at
  8C12FDB6. This verifies these regions, not every mutable byte in RAM.

IDA's system Python 2.7 installation points to a missing `C:/Python27`.
The IDC run used an isolated standard library from the official Python 2.7.18
source archive (MD5 `fd6cc8ec0a78c44036f825e739f36e5a`), with process-local
PYTHONHOME/IDAUSR. No system Python or IDA files were changed. Optional
MSP430/Python-skin plugins reported errors; the native SH4 module and IDC
analysis completed with exit code 0. The first broken text export and
incomplete temporary database files were removed and regenerated.

## LZ decompressor: 8C0354C0..8C035528

Inputs: R4 compressed source, R5 requested output bytes, R6 destination.
Return: R0 = original R5. R9/R10/R11/R12/R14 are saved on the guest stack
and restored. R4/R6 advance; R2/R3 and SR.T retain intermediate residue.
Two embedded references to this function occur at 8C033CC8 and 8C033F7C.
The latter is loaded at 8C033E94 and called at 8C033ECC after R5 is set to
half the product of two image dimensions. This supports an image/sprite-data
decompression interpretation; the complete asset format is not yet proven.

Algorithm-only pseudocode (not a drop-in emulator handler):

```c
end = dst + requested_bytes;
mask = 0;
do {
    if (mask == 0) { flags = *src++; mask = 0x80; }
    if (flags & mask) {
        token = *src++;
        back = dst - (token >> 4) - 1;  // distance 1..16
        count = (token & 15) + 2;       // length 2..17
        do { *dst++ = *back++; } while (--count != 0);
    } else {
        *dst++ = *src++;
    }
    mask >>= 1;
} while ((uint32_t)dst < (uint32_t)end);
return requested_bytes;
```

The byte copy is forward and may overlap, so memcpy is invalid. The output
limit is checked after a whole token; zero requested bytes still executes
one token, and the final token can overshoot. A correct HLE must retain the
actual signed byte-load residue and all guest effects, not merely this
high-level output. The rejected bounded HLE preserved those effects but was
slower; a new attempt needs a distinct, measured reduction in host overhead.
Fusing the entire function would also have to reproduce its internal
scheduler boundaries, not just its total cycles.

## Vertex pipeline: 8C12FDA0..8C12FEAA

IDA recovered the enclosing function for all three profiled hot entries:

| Entry | Work visible in the code |
| --- | --- |
| 8C12FDB6 | Load vertex fields, FTRV, optional lighting calculation |
| 8C12FE18 | Perspective reciprocal/divide, unpack color channels, prepare output |
| 8C12FE5A/5C | Scale output values, ordered store-queue writes and PREF submission |

The function toggles FPSCR.SZ with FSCHG at entry/exit and around an internal
path. IDA prints generic `fmov.s` mnemonics; these do **not** prove that a
particular executed move is 32-bit. The active FPSCR and actual decoded JIT
operation determine 32/64-bit width and FR/XF selection. This is why the
dynarec's 64-bit register-transfer optimization is relevant here.

Whole-function HLE is not yet implemented. Preserve intermediate scheduler
checks, FPU rounding/flags, PR/FPUL, the full register residue, and the exact
order of both PREF submissions. Existing failed single-block vertex HLE is
not evidence that a broader, carefully bounded optimization cannot work.

## Aligned copy family: 8C17A6E0..8C17A734

IDA did not assign a function boundary to the hot label automatically. The
surrounding disassembly shows an alignment dispatch on R4|R5|R6: eight-byte
copy uses FSCHG and divides the byte count by eight; fallback loops use four,
two or one-byte transfers. The hot five-instruction loop at 8C17A6F0 has ten
bytes and CRC32 `538D50CB`, matching the executable extracted from the disc.

With FPSCR.SZ=1, each iteration reads eight bytes at R5 into raw DR0, advances
R5 by eight, decrements R6 and updates SR.T, writes DR0 at R4, then advances
R4 by eight in the BF/S delay slot. Next PC is 8C17A6FA when R6 becomes zero,
otherwise 8C17A6F0. An initial zero count wraps; it does not skip the loop.
The profiled JIT block costs five guest cycles. Preserve the final raw DR0
bits, all untouched state, read-before-write order and scheduler boundaries.

The first device probe found destination A4445408, source 0CE60008, count 63,
SZ=1: the workload is RAM-to-VRAM, not the initially hypothesized RAM-to-RAM
copy. A RAM-only guard never entered its fast path during that probe, so no
device shadow-validation success is claimed for that initial variant.

The corrected candidate targets only the linear 64-bit VRAM interfaces at
physical 04/06; 05/07's 32-bit interface has different address mapping and is
excluded. It writes through the original lockable `vram` mapping. The Vita
abort handler handles VramLockedWrite before testing whether the faulting PC
is JIT code, so native C++ stores retain texture invalidation and page unlock
callbacks. No manual pre-unlock or writable bypass alias is used. Batches
stay within both physical pages and stop before the next scheduler debit.
MMU, STRICT_MODE and GGPO are excluded. Diagnostic comparison computes native
output privately and performs original VRAM writes exactly once, avoiding
extra texture callbacks from replaying both versions into visible memory.
