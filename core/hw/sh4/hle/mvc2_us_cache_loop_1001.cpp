// T1212N US v1.001: scheduler-bounded OCBI / ADD / DT / BF loop.
#include "hw/sh4/dyna/hle_patch.h"

#ifdef __vita__
#include "mvc2_cache_loop.h"
#include "cfg/cfg.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_opcode_list.h"
#include <cstring>

namespace {

void nativeBatch(u32 iterations)
{
	auto& ctx = Sh4cntx;
	ctx.r[0] += iterations * 32u;
	ctx.r[5] -= iterations;
	ctx.sr.T = ctx.r[5] == 0;
	ctx.pc = ctx.sr.T ? 0x8C02DECE : 0x8C02DEC6;
}

void originalBatch(u32 iterations)
{
	constexpr u16 opcodes[] = {0x0093, 0x7020, 0x4510, 0x8BFB};
	for (u32 i = 0; i < iterations; ++i)
	{
		Sh4cntx.pc = 0x8C02DEC6;
		for (u16 opcode : opcodes)
		{
			Sh4cntx.pc += 2;
			OpPtr[opcode](opcode);
		}
	}
}

void restoreOperands(const Sh4Context& initial)
{
	Sh4cntx.r[0] = initial.r[0];
	Sh4cntx.r[5] = initial.r[5];
	Sh4cntx.sr = initial.sr;
	Sh4cntx.pc = initial.pc;
}

__attribute__((noinline)) void validateBatch(u32 iterations)
{
	const Sh4Context initial = Sh4cntx;
	originalBatch(iterations);
	const Sh4Context expected = Sh4cntx;
	restoreOperands(initial);
	nativeBatch(iterations);
	const auto& actual = Sh4cntx;
	const bool match = std::memcmp(actual.r, expected.r, sizeof(actual.r)) == 0
		&& std::memcmp(actual.xffr, expected.xffr, sizeof(actual.xffr)) == 0
		&& actual.sr.status == expected.sr.status && actual.sr.T == expected.sr.T
		&& actual.pc == expected.pc && actual.pr == expected.pr
		&& actual.fpul == expected.fpul && actual.fpscr.full == expected.fpscr.full
		&& actual.hle_cycles == expected.hle_cycles
		&& actual.cycle_counter == expected.cycle_counter;
	if (!match)
		ERROR_LOG(DYNAREC, "MVC2 cache HLE differential mismatch: count=%u batch=%u", initial.r[5], iterations);
	verify(match);
	restoreOperands(initial);
}

} // namespace

bool mvc2CacheLoopEligible()
{
#ifdef STRICT_MODE
	// STRICT_MODE models cache writes; this specialization must not skip them.
	return false;
#else
	return !config::FullMMU && cfgLoadBool("perf", "Mvc2CacheLoopHle", false);
#endif
}

void mvc2CacheLoopEntry(s32 remainingCycles)
{
	static int checksLeft = -1;
	if (checksLeft < 0)
	{
		checksLeft = cfgLoadBool("perf", "Mvc2HleValidate", false) ? 16384 : 0;
		NOTICE_LOG(DYNAREC, "MVC2 cache HLE entered; shadow checks=%d", checksLeft);
	}
	const int blockCycles = -Sh4cntx.hle_cycles;
	// OCBI is a NOP in the non-STRICT interpreter and JIT. No memory, SQ, or
	// callback is elided. Unexpected timing/MMU state executes just one block.
	const u32 iterations = CCN_MMUCR.AT == 0
		? mvc2::cacheLoopBatch(Sh4cntx.r[5], remainingCycles, blockCycles) : 1;
	if (unlikely(checksLeft > 0))
	{
		validateBatch(iterations);
		if (--checksLeft == 0)
			NOTICE_LOG(DYNAREC, "MVC2 cache HLE differential 16384 passed");
	}
	nativeBatch(iterations);
	// Full dynamic cost includes the entry already pre-debited by the wrapper.
	Sh4cntx.hle_cycles += blockCycles * iterations;
}
#endif
