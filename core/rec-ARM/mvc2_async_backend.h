// Included once by rec_arm.cpp: shares its emitter helpers, not live compiler
// state (assembler, register allocator and canonical arguments are thread-local).
#ifdef __vita__
#include <atomic>
#include <deque>
#include <memory>
#define SHIL_MODE 2
#include "hw/sh4/dyna/shil_canonical.h"

namespace mvc2_async {
static __thread bool workerThread;
static u32 cursor;
static std::atomic<u32> freeBytes{MVC2_ASYNC_CODE_SIZE};
static std::mutex mutex;
static std::condition_variable wake;
static std::thread worker;
static bool stopping;
static bool cancel;
static u32 fallbackEntries, promoted, rejected;
static void* sharedFallback;

// An unpublished clone has not incremented the block-manager page counters.
struct UnpublishedDelete {
	void operator()(DynaRBI* p) const { if (p) { p->sh4_code_size = 0; delete p; } }
};
struct Job {
	RuntimeBlockInfoPtr original; // bound immediately after bm_AddBlock
	std::unique_ptr<DynaRBI, UnpublishedDelete> native;
	std::atomic<bool> ready{false};
	bool published = false;
	u32 guestPc;
	u32 chargedCycles;
	u64 queued, started = 0, finished = 0;
	u64 promotedAt = 0;
	u32 interpreted = 0;
	Job(DynaRBI* b) : native(new DynaRBI(*b)), guestPc(b->vaddr), chargedCycles(b->guest_cycles), queued(sceKernelGetProcessTimeWide()) {
		if (!ImmediateA32::IsImmediateA32(chargedCycles)) chargedCycles &= ~3;
	}
};
static std::deque<Job*> queue;
// Keeps fallback entry arguments alive until the entire code cache is reset.
static std::vector<std::unique_ptr<Job>> jobs;

static u32 value(const shil_param& p)
{
	return p.is_imm() ? p.imm_value() : p.is_null() ? 0 : *p.reg_ptr();
}

// Only integer IR is admitted initially. Reusing the optimized IR preserves
// folded literals, delay-slot branch latching, BRA chaining and cycle charge.
// No interpreter Step(), CPU_RATIO debit, FP rounding change or guest PC walk.
static bool supported(shilop op)
{
	switch (op) {
	case shop_mov32: case shop_jdyn: case shop_jcond:
	case shop_readm: case shop_writem:
	case shop_and: case shop_or: case shop_xor: case shop_not:
	case shop_add: case shop_sub: case shop_neg:
	case shop_shl: case shop_shr: case shop_sar: case shop_ror:
	case shop_shld: case shop_shad: case shop_swaplb: case shop_swap:
	case shop_ext_s8: case shop_ext_s16:
	case shop_mul_u16: case shop_mul_s16: case shop_mul_i32:
	case shop_test: case shop_seteq: case shop_setge: case shop_setgt:
	case shop_setae: case shop_setab: case shop_setpeq: case shop_xtrct:
		return true;
	default: return false;
	}
}

static u32 DYNACALL interpret(Job* job)
{
	++fallbackEntries;
	++job->interpreted;
	const RuntimeBlockInfo& b = *job->original;
	u32 dynamic = 0;
	for (const shil_opcode& op : b.oplist) {
		u32 a = value(op.rs1), c = value(op.rs2), d = value(op.rs3), result = 0;
		switch (op.op) {
		case shop_mov32: result = a; break;
		case shop_jdyn: dynamic = a + c; continue;
		case shop_jcond: dynamic = a; continue;
		case shop_readm:
			if (op.size == 1) result = (s32)(::s8)ReadMem8_nommu(a + d);
			else if (op.size == 2) result = (s32)(::s16)ReadMem16_nommu(a + d);
			else result = ReadMem32_nommu(a + d);
			break;
		case shop_writem:
			if (op.size == 1) WriteMem8_nommu(a + d, c);
			else if (op.size == 2) WriteMem16_nommu(a + d, c);
			else WriteMem32_nommu(a + d, c);
			continue;
		case shop_and: result = a & c; break;
		case shop_or: result = a | c; break;
		case shop_xor: result = a ^ c; break;
		case shop_not: result = ~a; break;
		case shop_add: result = a + c; break;
		case shop_sub: result = a - c; break;
		case shop_neg: result = -a; break;
		// A32 register shifts use the low byte, with saturating shifts >= 32.
		case shop_shl: c &= 255; result = c >= 32 ? 0 : a << c; break;
		case shop_shr: c &= 255; result = c >= 32 ? 0 : a >> c; break;
		case shop_sar: c &= 255; result = (s32)a >> std::min(c, 31u); break;
		case shop_ror: c &= 31; result = c ? (a >> c) | (a << (32 - c)) : a; break;
#define MVC2_BIN(name) case shop_##name: result = shil_opcl_##name::f1::impl(a, c); break
#define MVC2_UN(name) case shop_##name: result = shil_opcl_##name::f1::impl(a); break
		MVC2_BIN(shld); MVC2_BIN(shad); MVC2_UN(swaplb); MVC2_UN(swap);
		MVC2_UN(ext_s8); MVC2_UN(ext_s16);
		MVC2_BIN(mul_u16); MVC2_BIN(mul_s16); MVC2_BIN(mul_i32);
		MVC2_BIN(test); MVC2_BIN(seteq); MVC2_BIN(setge); MVC2_BIN(setgt);
		MVC2_BIN(setae); MVC2_BIN(setab); MVC2_BIN(setpeq); MVC2_BIN(xtrct);
#undef MVC2_BIN
#undef MVC2_UN
		default: die("unsupported MVC2 async IR");
		}
		*op.rd.reg_ptr() = result;
	}
	switch (b.BlockType) {
	case BET_StaticCall: case BET_StaticJump: return b.BranchBlock;
	case BET_Cond_0: case BET_Cond_1:
		return (b.has_jcond ? dynamic : sr.T) == (b.BlockType & 1) ? b.BranchBlock : b.NextBlock;
	case BET_DynamicCall: case BET_DynamicJump: case BET_DynamicRet: return dynamic;
	default: die("unsupported MVC2 async block end"); return 0;
	}
}

static void run()
{
	workerThread = true;
	currentAssembler = &workerAssembler;
	currentRegAlloc = &workerRegAlloc;
	currentCCParams = &workerCCParams;
	for (;;) {
		Job* job;
		{
			std::unique_lock<std::mutex> lock(mutex);
			wake.wait(lock, [] { return stopping || !queue.empty(); });
			if (cancel || (stopping && queue.empty())) break;
			job = queue.front(); queue.pop_front();
		}
		job->started = sceKernelGetProcessTimeWide();
		// No guest memory reads: decoding/SSA and literal folding were done by
		// the emulation thread. No HLE lookup, SMC checks or page registration.
		ngen_Compile(job->native.get(), false, false, false, true);
		job->finished = sceKernelGetProcessTimeWide();
		job->ready.store(true, std::memory_order_release);
	}
	workerThread = false;
}

static void* DYNACALL ready(Job* job)
{
	if (!job->ready.load(std::memory_order_acquire)) return nullptr;
	if (job->published) return (void*)bm_GetCodeByVAddr(job->guestPc);
	// A write to any protected source/literal page discards the original.
	// Never publish a result after that invalidation, even if PC is reused.
	if (bm_GetBlock(job->guestPc).get() != job->original.get()) {
		++rejected;
		return (void*)bm_GetCodeByVAddr(job->guestPc);
	}
	DynaRBI* native = job->native.release();
	bm_DiscardBlock(job->original.get());
	native->SetProtectedFlags();
	// Flush on the consuming CPU too: do not assume the producer's local
	// instruction-cache invalidation made the new code visible everywhere.
	vmem_platform_flush_cache((void*)native->code, (u8*)native->code + native->host_code_size - 1,
		(void*)native->code, (u8*)native->code + native->host_code_size - 1);
	bm_AddBlock(native);
	job->published = true;
	job->original.reset(); // stale block manager list retains it until its safe-point cleanup
	job->promotedAt = sceKernelGetProcessTimeWide();
	++promoted;
	return (void*)CC_RW2RX(native->code);
}

// Differential on-device check of every admitted arithmetic operation against
// the actual A32 emitter, including aliases and boundary/random input bits.
static void validateIR()
{
	static bool tested;
	if (tested || !cfgLoadBool("perf", "Mvc2AsyncValidate", false)) return;
	tested = true;
	NOTICE_LOG(DYNAREC, "mvc2-async: beginning on-device IR validation");
	auto saved = Sh4cntx;
	u32 testedCases = 0, seed = 0x13579bdf;
	const u32 edges[] = {0, 1, 2, 7, 15, 16, 31, 32, 33, 63, 127, 128, 255, 256,
		0xffff, 0x7fffffff, 0x80000000, 0xffffffff};
	for (int opcode = 0; opcode < shop_max; ++opcode) {
		if (!supported((shilop)opcode) || opcode == shop_jdyn || opcode == shop_jcond
			|| opcode == shop_readm || opcode == shop_writem) continue;
		bool unary = opcode == shop_mov32 || opcode == shop_not || opcode == shop_neg
			|| opcode == shop_swap || opcode == shop_swaplb || opcode == shop_ext_s8 || opcode == shop_ext_s16;
		for (int alias = 0; alias < 3; ++alias) {
			RuntimeBlockInfoPtr b(new DynaRBI());
			b->sh4_code_size = 0;
			b->guest_cycles = 1;
			b->vaddr = 0; b->BlockType = BET_StaticJump; b->BranchBlock = 0x1234;
			shil_opcode op{};
			op.op = (shilop)opcode; op.rd = shil_param((Sh4RegType)(reg_r0 + alias));
			op.rs1 = shil_param(reg_r1);
			if (!unary) op.rs2 = shil_param(reg_r2);
			b->oplist.push_back(op);
			ass = Arm32Assembler((u8*)emit_GetCCPtr(), emit_FreeSpace());
			auto entry = (void (*)(void*))emit_GetCCPtr();
			RegisterList savedRegs = RegisterList::Union(RegisterList(r4, r5, r6, r7), RegisterList(r8, r9, r10, r11), RegisterList(r12, lr));
			ass.Push(savedRegs); ass.Mov(r8, r0);
			sh4RegAlloc.DoAlloc(b.get(), alloc_regs, alloc_fpu);
			sh4RegAlloc.OpBegin(&b->oplist[0], 0);
			ngen_compile_opcode(b.get(), &b->oplist[0], true);
			sh4RegAlloc.OpEnd(&b->oplist[0]); sh4RegAlloc.Cleanup();
			ass.Pop(savedRegs); ass.Bx(lr); ass.Finalize();
			emit_Skip(ass.GetCursorOffset());
			Job testJob((DynaRBI*)b.get()); testJob.original = b;
			for (u32 i = 0; i < 1024; ++i) {
				Sh4cntx = saved;
				for (u32 rindex = 0; rindex < 16; ++rindex) {
					seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
					*GetRegPtr(reg_r0 + rindex) = seed;
				}
				if (i < 324) { *GetRegPtr(reg_r1) = edges[i / 18]; *GetRegPtr(reg_r2) = edges[i % 18]; }
				auto input = Sh4cntx;
				entry((u8*)&Sh4cntx + sizeof(Sh4cntx));
				auto expected = Sh4cntx;
				Sh4cntx = input;
				interpret(&testJob);
				if (memcmp(&Sh4cntx, &expected, sizeof(Sh4cntx))) {
					ERROR_LOG(DYNAREC, "mvc2-async: IR mismatch op=%d alias=%d case=%u", opcode, alias, i);
					die("MVC2 async IR validation failed");
				}
				++testedCases;
			}
		}
	}
	Sh4cntx = saved;
	fallbackEntries = 0;
	NOTICE_LOG(DYNAREC, "mvc2-async: IR validation passed %u arithmetic/register-context cases", testedCases);
}

static void generateFallback(Job* layout)
{
	if (sharedFallback) return;
	ass = Arm32Assembler((u8*)emit_GetCCPtr(), emit_FreeSpace());
	sharedFallback = emit_GetCCPtr();
	const int pcOffset = (u8*)&layout->guestPc - (u8*)layout;
	const int cycleOffset = (u8*)&layout->chargedCycles - (u8*)layout;
	ass.Mov(r5, r0); // all SH4 allocated registers were written back at block end
	ass.Ldr(r4, MemOperand(r5, pcOffset));
	call((void*)ready);
	ass.Cmp(r0, 0);
	ass.Bx(ne, r0);
	ass.Ldr(r0, MemOperand(r5, cycleOffset));
	ass.Sub(SetFlags, r9, r9, r0);
	Label execute, interrupt;
	ass.B(gt, &execute);
	// Equivalent to intc_sched, but uses the explicit guest PC. The common
	// helper is not a guest block and must not use bm_GetBlock(host LR).
	ass.Add(r9, r9, SH4_TIMESLICE);
	call((void*)UpdateSystem);
	ass.Cmp(r0, 0);
	ass.B(ne, &interrupt);
	ass.Ldr(r0, MemOperand(r8, rcbOffset(cntx.CpuRunning)));
	ass.Cmp(r0, 0);
	ass.B(ne, &execute);
	ass.Bind(&interrupt);
	ass.Ldr(r0, MemOperand(r5, pcOffset));
	call((void*)rdv_DoInterrupts_pc);
	ass.Mov(r4, r0);
	jump(no_update);
	ass.Bind(&execute);
	ass.Mov(r0, r5);
	call((void*)interpret);
	ass.Mov(r4, r0);
	jump(no_update);
	ass.Finalize();
	emit_Skip(ass.GetCursorOffset());
}
}

bool mvc2AsyncWorker() { return mvc2_async::workerThread; }
void* mvc2AsyncEmitPtr() { return CodeCache + CODE_SIZE + TEMP_CODE_SIZE + mvc2_async::cursor; }
u32 mvc2AsyncEmitFree() { return MVC2_ASYNC_CODE_SIZE - mvc2_async::cursor; }
void mvc2AsyncEmitSkip(u32 bytes)
{
	mvc2_async::cursor += bytes;
	mvc2_async::freeBytes.store(mvc2AsyncEmitFree(), std::memory_order_release);
}

void mvc2AsyncStop(bool reset)
{
	using namespace mvc2_async;
	if (worker.joinable()) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopping = true;
			cancel = reset;
		}
		wake.notify_one();
		worker.join();
	}
	if (!jobs.empty())
		NOTICE_LOG(DYNAREC, "mvc2-async: stop reset=%d jobs=%u promoted=%u rejected=%u fallback=%u bytes=%u",
			reset, (u32)jobs.size(), promoted, rejected, fallbackEntries, cursor);
	if (reset) {
		sharedFallback = nullptr;
		queue.clear(); jobs.clear(); cursor = 0;
		freeBytes.store(MVC2_ASYNC_CODE_SIZE, std::memory_order_release);
		fallbackEntries = promoted = rejected = 0;
	}
}

void mvc2AsyncReport(u64 origin)
{
	using namespace mvc2_async;
	if (jobs.empty()) return;
	const std::string path = get_writable_data_path("mvc2_async_jobs.csv");
	FILE* f = fopen(path.c_str(), "w");
	if (!f) return;
	fprintf(f, "# origin_us=%llu jobs=%u promoted=%u rejected=%u fallback=%u\n",
		(unsigned long long)origin, (u32)jobs.size(), promoted, rejected, fallbackEntries);
	fprintf(f, "pc,queued_us,started_us,finished_us,promoted_us,interpretations,ready\n");
	for (const auto& job : jobs) {
		bool done = job->ready.load(std::memory_order_acquire);
		fprintf(f, "%u,%llu,%llu,%llu,%llu,%u,%d\n", job->guestPc,
			(unsigned long long)job->queued, (unsigned long long)(done ? job->started : 0),
			(unsigned long long)(done ? job->finished : 0), (unsigned long long)job->promotedAt,
			job->interpreted, done);
	}
	int error = ferror(f); int closed = fclose(f);
	NOTICE_LOG(DYNAREC, "mvc2-async: report jobs=%u promoted=%u rejected=%u fallback=%u io_error=%d",
		(u32)jobs.size(), promoted, rejected, fallbackEntries, error || closed);
}

void mvc2AsyncBind(RuntimeBlockInfo* b)
{
	DynaRBI* block = (DynaRBI*)b;
	if (block->asyncFallback)
		((mvc2_async::Job*)block->asyncJob)->original = bm_GetBlock(block->vaddr);
}

bool mvc2AsyncCompile(RuntimeBlockInfo* b)
{
	using namespace mvc2_async;
	if (settings.content.gameId != "T1212N" || !cfgLoadBool("perf", "Mvc2AsyncJit", false)
		|| config::FullMMU || CCN_MMUCR.AT || config::DynarecSmcChecks
		|| !b->read_only || b->temp_block || b->has_fpu_op || b->oplist.size() > 64
		|| b->BlockType == BET_StaticIntr || b->BlockType == BET_DynamicIntr)
		return false;
	for (const shil_opcode& op : b->oplist) {
		if (!supported(op.op)) return false;
		for (const shil_param* p : {&op.rd, &op.rd2, &op.rs1, &op.rs2, &op.rs3})
			if (p->is_reg() && !p->is_r32i()) return false;
		if ((op.op == shop_readm || op.op == shop_writem) && op.size != 1 && op.size != 2 && op.size != 4)
			return false;
	}
	if (hle_LookupNative(b->vaddr)) return false;
	validateIR();
	std::unique_lock<std::mutex> lock(mutex);
	// Bound work and reserve a conservative 8 KiB per queued <=64-op block.
	// Exhaustion falls back to the existing compiler, never permanent IR mode.
	if (queue.size() >= 128 || freeBytes.load(std::memory_order_acquire) < (queue.size() + 2) * 8192)
		return false;
	if (!worker.joinable()) {
		stopping = cancel = false;
		worker = std::thread(run);
		NOTICE_LOG(DYNAREC, "mvc2-async: integer IR backend worker started (experimental)");
	}
	DynaRBI* block = (DynaRBI*)b;
	std::unique_ptr<Job> holder(new Job(block));
	Job* job = holder.get();
	jobs.push_back(std::move(holder));
	block->asyncFallback = true;
	block->asyncJob = job;
	generateFallback(job);
	block->code = (DynarecCodeEntryPtr)emit_GetCCPtr();
	// Three fixed A32 instructions: MOVW r0, job; MOVT r0, job; B helper.
	// All entries/helper are in the same <=10 MiB foreground code arena.
	u32* entry = (u32*)block->code;
	u32 address = (u32)job;
	entry[0] = 0xe3000000 | ((address & 0xf000) << 4) | (address & 0xfff);
	entry[1] = 0xe3400000 | ((address & 0xf0000000) >> 12) | ((address >> 16) & 0xfff);
	ptrdiff_t displacement = (u8*)sharedFallback - ((u8*)entry + 16);
	verify((displacement & 3) == 0 && displacement >= -32 * 1024 * 1024 && displacement < 32 * 1024 * 1024);
	entry[2] = 0xea000000 | ((displacement >> 2) & 0xffffff);
	vmem_platform_flush_cache(entry, (u8*)entry + 11, entry, (u8*)entry + 11);
	block->host_code_size = 12;
	block->host_opcodes = 3;
	block->relink_offset = 0;
	block->relink_data = 0;
	emit_Skip(block->host_code_size);
	queue.push_back(job);
	lock.unlock();
	wake.notify_one();
	return true;
}
#endif
