// T1212N US v1.001: unsigned 32-bit divide, nonzero-divisor basic block.
#include "hw/sh4/dyna/hle_patch.h"

#ifdef __vita__
#include "cfg/cfg.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_opcode_list.h"
#include <cstring>

void mvc2CacheLoopEntry(s32 remainingCycles);
bool mvc2CacheLoopEligible();

namespace {

bool eligible()
{
	return !config::FullMMU;
}

bool fastState()
{
	const u32 stack = Sh4cntx.r[15];
	return Sh4cntx.r[0] != 0 && CCN_MMUCR.AT == 0
		&& IsOnRam(stack) && (stack & 3) == 0
		&& (stack & RAM_MASK) <= RAM_MASK - 3;
}

void divideBlock()
{
	auto& ctx = Sh4cntx;
	const u32 quotient = ctx.r[1] / ctx.r[0];
	ctx.r[0] = quotient;
	ctx.r[1] = quotient;
	// After DIV0U and all 32 DIV1 steps, M=0 and Q=!quotient.bit0.
	// The final ROTCL consumes quotient.bit0 and shifts out an initial zero.
	ctx.sr.status = (ctx.sr.status & ~0x300u) | ((~quotient & 1) << 8);
	ctx.sr.T = 0;
	const u32 returnPc = ctx.pr;
	std::memcpy(&ctx.r[2], &mem_b[ctx.r[15] & RAM_MASK], 4);
	ctx.r[15] += 4;
	ctx.pc = returnPc;
}

void originalBlock()
{
	// Direct instruction handlers do not charge cycles. Execute the RTS delay
	// slot ourselves to avoid the interpreter's separate delay-slot debit.
	u32 pc = 0x8C1291E2;
	auto execute = [&](u16 opcode) {
		pc += 2;
		Sh4cntx.pc = pc;
		OpPtr[opcode](opcode);
	};
	execute(0xE200);
	execute(0x0019);
	for (unsigned i = 0; i < 32; ++i)
	{
		execute(0x4124);
		execute(0x3204);
	}
	execute(0x4124);
	execute(0x6013);
	const u32 returnPc = Sh4cntx.pr;
	pc += 2; // RTS, whose only other effect is the delay slot below.
	execute(0x62F6);
	Sh4cntx.pc = returnPc;
}

void restoreOperands(const Sh4Context& initial)
{
	std::memcpy(Sh4cntx.r, initial.r, sizeof(initial.r));
	Sh4cntx.sr = initial.sr;
	Sh4cntx.pc = initial.pc;
}

__attribute__((noinline)) void validateBlock()
{
	// This block has only one memory read, proved side-effect-free by fastState.
	const Sh4Context initial = Sh4cntx;
	originalBlock();
	const Sh4Context expected = Sh4cntx;
	restoreOperands(initial);
	divideBlock();
	const auto& actual = Sh4cntx;
	const bool match = std::memcmp(actual.r, expected.r, sizeof(actual.r)) == 0
		&& std::memcmp(actual.xffr, expected.xffr, sizeof(actual.xffr)) == 0
		&& actual.sr.status == expected.sr.status && actual.sr.T == expected.sr.T
		&& actual.pc == expected.pc && actual.pr == expected.pr
		&& actual.fpul == expected.fpul && actual.fpscr.full == expected.fpscr.full
		&& actual.hle_cycles == expected.hle_cycles
		&& actual.cycle_counter == expected.cycle_counter;
	if (!match)
		ERROR_LOG(DYNAREC, "MVC2 divide HLE differential mismatch: %08X / %08X",
				initial.r[1], initial.r[0]);
	verify(match);
	restoreOperands(initial);
}

void divideEntry(s32 /*remainingCycles*/)
{
	static int checksLeft = -1;
	static bool fallbackReported = false;
	if (checksLeft < 0)
	{
		checksLeft = cfgLoadBool("perf", "Mvc2HleValidate", false) ? 16384 : 0;
		NOTICE_LOG(DYNAREC, "MVC2 divide HLE entered; shadow checks=%d", checksLeft);
	}
	// Exactly the decoded entry block: reuse its complete existing guest debit,
	// including the decoder's 64 cycles for the collapsed DIV1/ROTCL sequence.
	// No guest scheduler boundary is removed or shifted.
	const int completeDynamicGuestCycles = -Sh4cntx.hle_cycles;
	if (likely(fastState()))
	{
		if (unlikely(checksLeft > 0))
		{
			validateBlock();
			if (--checksLeft == 0)
				NOTICE_LOG(DYNAREC, "MVC2 divide HLE differential 16384 passed");
		}
		divideBlock();
	}
	else
	{
		if (!fallbackReported)
		{
			NOTICE_LOG(DYNAREC, "MVC2 divide HLE fallback: R0=%08X SP=%08X AT=%u",
					Sh4cntx.r[0], Sh4cntx.r[15], CCN_MMUCR.AT);
			fallbackReported = true;
		}
		originalBlock();
	}
	Sh4cntx.hle_cycles += completeDynamicGuestCycles;
}

HlePatch patches[] = {
	{0x8C1291E2, 140, 0x5598C45C, &divideEntry, -1, &eligible},
	{0x8C02DEC6, 8, 0x9139F3E8, &mvc2CacheLoopEntry, -1, &mvc2CacheLoopEligible},
};
HlePatchSet patchSet = {"T1212N", "MVC2 US v1.001", patches, 2};
HLE_REGISTER_PATCH_SET(patchSet);

} // namespace
#endif
