// Bit-preserving transfers between the d0 memory scratch and allocated FPRs.
// Included after Arm32Assembler; shared by the emitter and its device test.
static void mvc2ReadFpuPair(Arm32Assembler& a, SRegister low, SRegister high, bool direct)
{
	verify(low.GetCode() >= 16 && high.GetCode() >= 16 && !low.Is(high));
	if (direct)
	{
		a.Vmov(low, s0);
		a.Vmov(high, s1);
	}
	else
	{
		a.Vmov(r0, r1, d0);
		a.Vmov(low, r0);
		a.Vmov(high, r1);
	}
}

static void mvc2WriteFpuPair(Arm32Assembler& a, SRegister low, SRegister high, bool direct)
{
	verify(low.GetCode() >= 16 && high.GetCode() >= 16 && !low.Is(high));
	if (direct)
	{
		a.Vmov(s0, low);
		a.Vmov(s1, high);
	}
	else
	{
		a.Vmov(r2, low);
		a.Vmov(r3, high);
		a.Vmov(d0, r2, r3);
	}
}

static void mvc2TestFpuPairs()
{
	// Execute the same emitter helpers on the Vita, not a host float model.
	// Cover all ordered distinct allocator pairs, including reversed pairs.
	const u32 special[] = {0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
		0x3f800000, 0x7f7fffff, 0x7f800000, 0xff800000, 0x7fc00001, 0x7f800001,
		0xffffffff, 0xdeadbeef};
	u32 random = 0x74905a13;
	u32 cases = 0;
	for (int lo = 16; lo < 32; ++lo)
		for (int hi = 16; hi < 32; ++hi)
		{
			if (lo == hi) continue;
			Arm32Assembler a((u8 *)emit_GetCCPtr(), emit_FreeSpace());
			a.Push(RegisterList(r4, r5));
			a.Vpush(DRegisterList(d8, 8));
			a.Mov(r4, r0);
			a.Mov(r5, r1);
			a.Vmrs(RegisterOrAPSR_nzcv(r2.GetCode()), FPSCR);
			a.Push(RegisterList(r2, r3));
			a.Ldr(r2, MemOperand(r4, 8));
			a.Vmsr(FPSCR, r2);
			a.Vmrs(RegisterOrAPSR_nzcv(r2.GetCode()), FPSCR);
			a.Str(r2, MemOperand(r5, 36));
			a.Vldr(d0, MemOperand(r4));
			mvc2ReadFpuPair(a, SRegister(lo), SRegister(hi), false);
			a.Vstr(SRegister(lo), MemOperand(r5));
			a.Vstr(SRegister(hi), MemOperand(r5, 4));
			// Poison destinations so a missing candidate move cannot pass.
			a.Mov(r2, 0xdeadbeef);
			a.Vmov(SRegister(lo), r2);
			a.Vmov(SRegister(hi), r2);
			a.Vldr(d0, MemOperand(r4));
			mvc2ReadFpuPair(a, SRegister(lo), SRegister(hi), true);
			a.Vstr(SRegister(lo), MemOperand(r5, 8));
			a.Vstr(SRegister(hi), MemOperand(r5, 12));
			a.Vldr(SRegister(lo), MemOperand(r4));
			a.Vldr(SRegister(hi), MemOperand(r4, 4));
			mvc2WriteFpuPair(a, SRegister(lo), SRegister(hi), false);
			a.Vstr(d0, MemOperand(r5, 16));
			a.Mov(r2, 0xdeadbeef);
			a.Vmov(d0, r2, r2);
			mvc2WriteFpuPair(a, SRegister(lo), SRegister(hi), true);
			a.Vstr(d0, MemOperand(r5, 24));
			a.Vmrs(RegisterOrAPSR_nzcv(r2.GetCode()), FPSCR);
			a.Str(r2, MemOperand(r5, 32));
			a.Pop(RegisterList(r2, r3));
			a.Vmsr(FPSCR, r2);
			a.Vpop(DRegisterList(d8, 8));
			a.Pop(RegisterList(r4, r5));
			a.Bx(lr);
			a.Finalize();
			auto test = (void (*)(const u32 *, u32 *))emit_GetCCPtr();
			emit_Skip(a.GetCursorOffset());
			for (unsigned i = 0; i < 4096; ++i)
			{
				alignas(8) u32 input[4], output[10] = {};
				for (int word = 0; word < 2; ++word)
				{
					random ^= random << 13; random ^= random >> 17; random ^= random << 5;
					input[word] = i < 196 ? special[word ? i % 14 : i / 14] : random;
				}
				// Rounding mode, DN/FZ and cumulative flags; no trap enables.
				input[2] = ((i & 15) << 22) | ((i & 16) ? 0xf800009f : 0);
				test(input, output);
				for (int pair = 0; pair < 4; ++pair)
					verify(output[pair * 2] == input[0] && output[pair * 2 + 1] == input[1]);
				verify(output[8] == output[9]);
				++cases;
			}
		}
	NOTICE_LOG(DYNAREC, "MVC2 FPU pair emitter: %u bit-pattern/FPSCR cases passed", cases);
}
