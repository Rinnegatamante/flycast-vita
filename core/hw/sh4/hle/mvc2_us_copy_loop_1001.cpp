// T1212N US v1.001: aligned RAM-to-64-bit-VRAM copy, scheduler-bounded.
#include "hw/sh4/dyna/hle_patch.h"

#ifdef __vita__
#include "mvc2_copy_loop.h"
#include "cfg/cfg.h"
#include "hw/pvr/pvr_mem.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_opcode_list.h"
#include <algorithm>
#include <cstring>

namespace {

bool directRam(u32 address)
{
	const u32 segment = address >> 29;
	return IsOnRam(address) && (segment == 0 || segment == 4 || segment == 5)
		&& (address & 7) == 0;
}

u32 fastBatch(s32 remaining, int blockCycles)
{
	const auto& ctx = Sh4cntx;
	const u32 destinationSegment = ctx.r[4] >> 29;
	const u32 destinationArea = (ctx.r[4] >> 24) & 0x1F;
	if (CCN_MMUCR.AT != 0 || ctx.fpscr.SZ != 1
		|| !directRam(ctx.r[5]) || (ctx.r[4] & 7) != 0
		|| (destinationSegment != 0 && destinationSegment != 4 && destinationSegment != 5)
		|| (destinationArea != 4 && destinationArea != 6))
		return 0;
	// 04/06 map linearly to the 64-bit VRAM interface; 05/07's swizzle is not
	// interchangeable. No batch crosses a physical page or scheduler boundary.
	u32 n = mvc2::copyLoopBatch(ctx.r[6], remaining, blockCycles);
	n = std::min(n, (PAGE_SIZE - (ctx.r[4] & PAGE_MASK)) / 8);
	return std::min(n, (PAGE_SIZE - (ctx.r[5] & PAGE_MASK)) / 8);
}

void nativeBatch(u32 iterations, u8* destination)
{
	auto& ctx = Sh4cntx;
	// Use the original lockable VRAM mapping, not its writable bypass alias.
	// Each store can fault through VramLockedWrite exactly as _vmem_WriteMem64
	// does. The existing fault handler supports native C++ PCs, not just JIT PCs.
	const u64 last = mvc2::copyLoopBytes(destination,
		&mem_b[ctx.r[5] & RAM_MASK], iterations);
	// The interpreter's raw dr_hex view starts at the active fr bank, xffr[16].
	// GetDR/SetDR swap floating-point words and are not appropriate for FMOV.
	std::memcpy(&ctx.xffr[16], &last, 8);
	ctx.r[4] += iterations * 8;
	ctx.r[5] += iterations * 8;
	ctx.r[6] -= iterations;
	ctx.sr.T = ctx.r[6] == 0;
	ctx.pc = ctx.sr.T ? 0x8C17A6FA : 0x8C17A6F0;
}

void originalBatch(u32 iterations)
{
	for (u32 i = 0; i < iterations; ++i)
	{
		Sh4cntx.pc = 0x8C17A6F2;
		OpPtr[0xF059](0xF059);
		Sh4cntx.pc = 0x8C17A6F4;
		OpPtr[0x4610](0x4610);
		Sh4cntx.pc = 0x8C17A6F6;
		OpPtr[0xF40A](0xF40A);
		// Execute BF/S's delay slot explicitly: the interpreter branch helper
		// would separately debit it, whereas the JIT entry has paid for it.
		const u32 destination = Sh4cntx.sr.T ? 0x8C17A6FA : 0x8C17A6F0;
		Sh4cntx.pc = 0x8C17A6FA;
		OpPtr[0x7408](0x7408);
		Sh4cntx.pc = destination;
	}
}

void restoreOperands(const Sh4Context& initial)
{
	Sh4cntx.r[4] = initial.r[4];
	Sh4cntx.r[5] = initial.r[5];
	Sh4cntx.r[6] = initial.r[6];
	std::memcpy(&Sh4cntx.xffr[16], &initial.xffr[16], 8);
	Sh4cntx.sr = initial.sr;
	Sh4cntx.pc = initial.pc;
}

__attribute__((noinline)) void validateBatch(u32 iterations)
{
	const Sh4Context initial = Sh4cntx;
	const u32 bytes = iterations * 8;
	alignas(8) u8 expectedMemory[720];
	verify(bytes <= sizeof(expectedMemory));
	// RAM source and VRAM destination cannot overlap. Calculate candidate
	// output privately, then execute original stores exactly once to preserve
	// texture callbacks and avoid exposing repeated diagnostic writes.
	nativeBatch(iterations, expectedMemory);
	const Sh4Context expected = Sh4cntx;
	restoreOperands(initial);
	originalBatch(iterations);
	const auto& actual = Sh4cntx;
	const bool match = std::memcmp(&vram[initial.r[4] & VRAM_MASK], expectedMemory, bytes) == 0
		&& std::memcmp(actual.r, expected.r, sizeof(actual.r)) == 0
		&& std::memcmp(actual.xffr, expected.xffr, sizeof(actual.xffr)) == 0
		&& actual.sr.status == expected.sr.status && actual.sr.T == expected.sr.T
		&& actual.pc == expected.pc && actual.pr == expected.pr
		&& actual.fpul == expected.fpul && actual.fpscr.full == expected.fpscr.full
		&& actual.hle_cycles == expected.hle_cycles
		&& actual.cycle_counter == expected.cycle_counter;
	if (!match)
		ERROR_LOG(DYNAREC, "MVC2 copy HLE differential mismatch: dst=%08X src=%08X count=%u batch=%u",
			initial.r[4], initial.r[5], initial.r[6], iterations);
	verify(match);
}

} // namespace

bool mvc2CopyLoopEligible()
{
#ifdef STRICT_MODE
	return false;
#else
	return !config::FullMMU && !config::GGPOEnable
		&& cfgLoadBool("perf", "Mvc2CopyLoopHle", false);
#endif
}

void mvc2CopyLoopEntry(s32 remainingCycles)
{
	static int checksLeft = -1;
	static bool firstFast = true, firstFallback = true;
	if (checksLeft < 0)
	{
		checksLeft = cfgLoadBool("perf", "Mvc2HleValidate", false) ? 16384 : 0;
		NOTICE_LOG(DYNAREC, "MVC2 copy HLE entered; shadow checks=%d", checksLeft);
	}
	const int blockCycles = -Sh4cntx.hle_cycles;
	const u32 iterations = fastBatch(remainingCycles, blockCycles);
	if (likely(iterations != 0))
	{
		if (unlikely(firstFast))
		{
			firstFast = false;
			NOTICE_LOG(DYNAREC, "MVC2 copy HLE fast: dst=%08X src=%08X count=%u batch=%u cycles=%d",
				Sh4cntx.r[4], Sh4cntx.r[5], Sh4cntx.r[6], iterations, blockCycles);
		}
		if (unlikely(checksLeft > 0))
		{
			validateBatch(iterations);
			if (--checksLeft == 0)
				NOTICE_LOG(DYNAREC, "MVC2 copy HLE differential 16384 passed");
		}
		else
			nativeBatch(iterations, &vram[Sh4cntx.r[4] & VRAM_MASK]);
		Sh4cntx.hle_cycles += blockCycles * iterations;
	}
	else
	{
		if (unlikely(firstFallback))
		{
			firstFallback = false;
			NOTICE_LOG(DYNAREC, "MVC2 copy HLE fallback: dst=%08X src=%08X count=%u SZ=%u AT=%u",
				Sh4cntx.r[4], Sh4cntx.r[5], Sh4cntx.r[6], Sh4cntx.fpscr.SZ, CCN_MMUCR.AT);
		}
		originalBatch(1);
		Sh4cntx.hle_cycles += blockCycles;
	}
}
#endif
