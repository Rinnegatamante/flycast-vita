#include "types.h"
#include "oslib/frame_trace.h"
#include <unordered_set>

#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_core.h"
#include "hw/sh4/sh4_interrupts.h"

#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/modules/mmu.h"
#include "cfg/option.h"

#include <ctime>
#include <cfloat>

#include "blockmanager.h"
#include "ngen.h"
#include "mvc2_native_cache.h"
#include "decoder.h"

#include <xxhash.h>

#if FEAT_SHREC != DYNAREC_NONE
#if defined(_WIN32) || FEAT_SHREC != DYNAREC_JIT || defined(TARGET_IPHONE) || defined(TARGET_ARM_MAC)
static u8 *SH4_TCB;
#else
static u8 SH4_TCB[CODE_SIZE + TEMP_CODE_SIZE + 4096]
#if defined(__unix__) || defined(__SWITCH__) || defined(__vita__)
	__attribute__((section(".text")));
#elif defined(__APPLE__)
	__attribute__((section("__TEXT,.text")));
#else
	#error SH4_TCB ALLOC
#endif
#endif

u8* CodeCache;
static u8* TempCodeCache;
ptrdiff_t cc_rx_offset;

static u32 LastAddr;
static u32 TempLastAddr;
static u32 *emit_ptr;
static u32 *emit_ptr_limit;

static std::unordered_set<u32> smc_hotspots;

static sh4_if sh4Interp;

void* emit_GetCCPtr() { return emit_ptr==0?(void*)&CodeCache[LastAddr]:(void*)emit_ptr; }

static void clear_temp_cache(bool full)
{
	FT_SCOPE(Emu, TempClear, full);
	//printf("recSh4:Temp Code Cache clear at %08X\n", curr_pc);
	TempLastAddr = 0;
	bm_ResetTempCache(full);
}

static void recSh4_ClearCache()
{
	mvc2NativeCacheReset();
	FT_SCOPE(Emu, CacheClear, next_pc);
	INFO_LOG(DYNAREC, "recSh4:Dynarec Cache clear at %08X free space %d", next_pc, emit_FreeSpace());
	LastAddr = 0;
	bm_ResetCache();
	smc_hotspots.clear();
	clear_temp_cache(true);
}

static void recSh4_Run()
{
	sh4_int_bCpuRun = true;
	RestoreHostRoundingMode();

	u8 *sh4_dyna_rcb = (u8 *)&Sh4cntx + sizeof(Sh4cntx);
	INFO_LOG(DYNAREC, "cntx // fpcb offset: %td // pc offset: %td // pc %08X", (u8*)&sh4rcb.fpcb - sh4_dyna_rcb, (u8*)&sh4rcb.cntx.pc - sh4_dyna_rcb, sh4rcb.cntx.pc);
	
	ngen_mainloop(sh4_dyna_rcb);

	sh4_int_bCpuRun = false;
}

void emit_Skip(u32 sz)
{
	if (emit_ptr)
		emit_ptr = (u32*)((u8*)emit_ptr + sz);
	else
		LastAddr += sz;

}
u32 emit_FreeSpace()
{
	if (emit_ptr)
		return (emit_ptr_limit - emit_ptr) * sizeof(u32);
	else
		return CODE_SIZE - LastAddr;
}

void AnalyseBlock(RuntimeBlockInfo* blk);

const char* RuntimeBlockInfo::hash()
{
	XXH32_hash_t hash = 0;

	u8* ptr = GetMemPtr(this->addr, this->sh4_code_size);

	if (ptr)
	{
		XXH32_state_t *state = XXH32_createState();
		XXH32_reset(state, 7);
		for (u32 i = 0; i < this->guest_opcodes; i++)
		{
			u16 data = ptr[i];
			//Do not count PC relative loads (relocated code)
			if ((data >> 12) == 0xD)
				data = 0xD000;

			XXH32_update(state, &data, 2);
		}
		hash = XXH32_digest(state);
		XXH32_freeState(state);
	}
	static char block_hash[20];
	sprintf(block_hash, ">:1:%02X:%08X", this->guest_opcodes, hash);

	return block_hash;
}

bool RuntimeBlockInfo::Setup(u32 rpc,fpscr_t rfpu_cfg)
{
	staging_runs=addr=lookups=runs=host_code_size=0;
	guest_cycles=guest_opcodes=host_opcodes=0;
	sh4_code_size = 0;
	pBranchBlock=pNextBlock=0;
	code=0;
	has_jcond=false;
	BranchBlock = NullAddress;
	NextBlock = NullAddress;
	BlockType = BET_SCL_Intr;
	has_fpu_op = false;
	temp_block = false;
	
	vaddr = rpc;
	if (mmu_enabled())
	{
		u32 rv = mmu_instruction_translation(vaddr, addr);
		if (rv != MMU_ERROR_NONE)
		{
			DoMMUException(vaddr, rv, MMU_TT_IREAD);
			return false;
		}
	}
	else
		addr = vaddr;
	fpu_cfg=rfpu_cfg;
	
	oplist.clear();

	try {
		FT_SCOPE(Emu, Decode, rpc);
		if (!dec_DecodeBlock(this, SH4_TIMESLICE / 2))
			return false;
	}
	catch (const SH4ThrownException& ex) {
		Do_Exception(rpc, ex.expEvn);
		return false;
	}
	SetProtectedFlags();

	{
		FT_SCOPE(Emu, Analyse, rpc);
		AnalyseBlock(this);
	}

	return true;
}

DynarecCodeEntryPtr rdv_CompilePC(u32 blockcheck_failures)
{
	FT_SCOPE(Emu, Compile, next_pc);
	u32 pc=next_pc;

	if (emit_FreeSpace()<16*1024 || pc==0x8c0000e0 || pc==0xac010000 || pc==0xac008300)
		recSh4_ClearCache();

	if (blockcheck_failures == 0 && smc_hotspots.find(pc) == smc_hotspots.end())
		if (RuntimeBlockInfo* cached = mvc2NativeCacheLookup(pc, fpscr.full))
		{
			bm_AddBlock(cached);
			return cached->code;
		}

	RuntimeBlockInfo* rbi = ngen_AllocateBlock();

	if (!rbi->Setup(pc,fpscr))
	{
		delete rbi;
		return NULL;
	}
	rbi->blockcheck_failures = blockcheck_failures;
	if (smc_hotspots.find(rbi->addr) != smc_hotspots.end())
	{
		if (TEMP_CODE_SIZE - TempLastAddr < 16 * 1024)
			clear_temp_cache(false);
		emit_ptr = (u32 *)(TempCodeCache + TempLastAddr);
		emit_ptr_limit = (u32 *)(TempCodeCache + TEMP_CODE_SIZE);
		rbi->temp_block = true;
		if (rbi->read_only)
			INFO_LOG(DYNAREC, "WARNING: temp block %x (%x) is protected!", rbi->vaddr, rbi->addr);
	}
	bool do_opts = !rbi->temp_block;
	rbi->staging_runs=do_opts?100:-100;
	bool block_check = !rbi->read_only;
	ngen_Compile(rbi, block_check, (pc & 0xFFFFFF) == 0x08300 || (pc & 0xFFFFFF) == 0x10000, false, do_opts);
	verify(rbi->code!=0);

	bm_AddBlock(rbi);

	if (emit_ptr != NULL)
	{
		TempLastAddr = (u8*)emit_ptr - TempCodeCache;
		emit_ptr = NULL;
		emit_ptr_limit = NULL;
	}

	return rbi->code;
}

DynarecCodeEntryPtr DYNACALL rdv_FailedToFindBlock_pc()
{
	return rdv_FailedToFindBlock(next_pc);
}

DynarecCodeEntryPtr DYNACALL rdv_FailedToFindBlock(u32 pc)
{
	//DEBUG_LOG(DYNAREC, "rdv_FailedToFindBlock %08x", pc);
	next_pc=pc;
	DynarecCodeEntryPtr code = rdv_CompilePC(0);
	if (code == NULL)
		code = bm_GetCodeByVAddr(next_pc);
	else
		code = (DynarecCodeEntryPtr)CC_RW2RX(code);
	return code;
}

static void ngen_FailedToFindBlock_internal() {
	rdv_FailedToFindBlock(Sh4cntx.pc);
}

void (*ngen_FailedToFindBlock)() = &ngen_FailedToFindBlock_internal;

u32 DYNACALL rdv_DoInterrupts_pc(u32 pc) {
	next_pc = pc;
	UpdateINTC();

	return next_pc;
}

u32 DYNACALL rdv_DoInterrupts(void* block_cpde)
{
	RuntimeBlockInfoPtr rbi = bm_GetBlock(block_cpde);
	return rdv_DoInterrupts_pc(rbi->vaddr);
}

// addr must be the physical address of the start of the block
DynarecCodeEntryPtr DYNACALL rdv_BlockCheckFail(u32 addr)
{
	FT_MARK(Emu, BlockCheck, addr, 0);
	DEBUG_LOG(DYNAREC, "rdv_BlockCheckFail @ %08x", addr);
	u32 blockcheck_failures = 0;
	if (mmu_enabled())
	{
		RuntimeBlockInfoPtr block = bm_GetBlock(addr);
		blockcheck_failures = block->blockcheck_failures + 1;
		if (blockcheck_failures > 5)
		{
			bool inserted = smc_hotspots.insert(addr).second;
			if (inserted)
				DEBUG_LOG(DYNAREC, "rdv_BlockCheckFail SMC hotspot @ %08x fails %d", addr, blockcheck_failures);
		}
		bm_DiscardBlock(block.get());
	}
	else
	{
		next_pc = addr;
		recSh4_ClearCache();
	}
	return (DynarecCodeEntryPtr)CC_RW2RX(rdv_CompilePC(blockcheck_failures));
}

DynarecCodeEntryPtr rdv_FindOrCompile()
{
	DynarecCodeEntryPtr rv = bm_GetCodeByVAddr(next_pc);  // Returns exec addr
	if (rv == ngen_FailedToFindBlock)
		rv = (DynarecCodeEntryPtr)CC_RW2RX(rdv_CompilePC(0));  // Returns rw addr
	
	return rv;
}

void* DYNACALL rdv_LinkBlock(u8* code,u32 dpc)
{
	// code is the RX addr to return after, however bm_GetBlock returns RW
	//DEBUG_LOG(DYNAREC, "rdv_LinkBlock %p pc %08x", code, dpc);
	RuntimeBlockInfoPtr rbi = bm_GetBlock(code);
	bool stale_block = false;
	if (!rbi)
	{
		stale_block = true;
		rbi = bm_GetStaleBlock(code);
	}
	
	verify(rbi != NULL);

	u32 bcls = BET_GET_CLS(rbi->BlockType);

	if (bcls == BET_CLS_Static)
	{
		if (rbi->BlockType == BET_StaticIntr)
			next_pc = rbi->NextBlock;
		else
			next_pc = rbi->BranchBlock;
	}
	else if (bcls == BET_CLS_Dynamic)
	{
		next_pc = dpc;
	}
	else if (bcls == BET_CLS_COND)
	{
		if (dpc)
			next_pc = rbi->BranchBlock;
		else
			next_pc = rbi->NextBlock;
	}

	DynarecCodeEntryPtr rv = rdv_FindOrCompile();  // Returns rx ptr

	if (!mmu_enabled() && !stale_block)
	{
		if (bcls == BET_CLS_Dynamic)
		{
			verify(rbi->relink_data == 0 || rbi->pBranchBlock == NULL);

			if (rbi->pBranchBlock != NULL)
			{
				rbi->pBranchBlock->RemRef(rbi);
				rbi->pBranchBlock = NULL;
				rbi->relink_data = 1;
			}
			else if (rbi->relink_data == 0)
			{
				rbi->pBranchBlock = bm_GetBlock(next_pc).get();
				rbi->pBranchBlock->AddRef(rbi);
			}
		}
		else
		{
			RuntimeBlockInfo* nxt = bm_GetBlock(next_pc).get();

			if (rbi->BranchBlock == next_pc)
				rbi->pBranchBlock = nxt;
			if (rbi->NextBlock == next_pc)
				rbi->pNextBlock = nxt;

			nxt->AddRef(rbi);
		}
		u32 ncs = rbi->relink_offset + rbi->Relink();
		verify(rbi->host_code_size >= ncs);
		rbi->host_code_size = ncs;
	}
	else
	{
		INFO_LOG(DYNAREC, "null RBI: from %08X to %08X -- unlinked stale block -- code %p next %p", rbi->vaddr, next_pc, code, rv);
	}
	
	return (void*)rv;
}
static void recSh4_Stop()
{
	sh4Interp.Stop();
}

static void recSh4_Step()
{
	sh4Interp.Step();
}

static void recSh4_Reset(bool hard)
{
	sh4Interp.Reset(hard);
	recSh4_ClearCache();
	if (hard)
		bm_Reset();
}

static void recSh4_Init()
{
	INFO_LOG(DYNAREC, "recSh4 Init");
	Get_Sh4Interpreter(&sh4Interp);
	sh4Interp.Init();
	bm_Init();

	
	if (_nvmem_enabled())
	{
		if (!_nvmem_4gb_space())
		{
			verify(mem_b.data==((u8*)p_sh4rcb->sq_buffer+512+0x0C000000));
		}
		else
		{
			verify(mem_b.data==((u8*)p_sh4rcb->sq_buffer+512+0x8C000000));
		}
	}

	// Prepare some pointer to the pre-allocated code cache:
	void *candidate_ptr = (void*)(((unat)SH4_TCB + 4095) & ~4095);

	// Call the platform-specific magic to make the pages RWX
	CodeCache = NULL;
	#if defined(FEAT_NO_RWX_PAGES)
	verify(vmem_platform_prepare_jit_block(candidate_ptr, CODE_SIZE + TEMP_CODE_SIZE, (void**)&CodeCache, &cc_rx_offset));
	#else
	verify(vmem_platform_prepare_jit_block(candidate_ptr, CODE_SIZE + TEMP_CODE_SIZE, (void**)&CodeCache));
	#endif
	// Ensure the pointer returned is non-null
	verify(CodeCache != NULL);

	TempCodeCache = CodeCache + CODE_SIZE;
	ngen_init();
	bm_ResetCache();
}

static void recSh4_Term()
{
	mvc2NativeCacheReset();
	INFO_LOG(DYNAREC, "recSh4 Term");
	bm_Term();
	sh4Interp.Term();
}

#if defined(__vita__) && FEAT_SHREC == DYNAREC_JIT && HOST_CPU == CPU_ARM
// Compile a previously observed guest entry with the current backend. The
// portable cache has already verified the guest bytes. No cached host code or
// executable address is reused here.
Mvc2PortableWarmResult rdv_PrewarmPortableBlock(
    const mvc2_native::PortableRecord& source, bool stageFpuBlock)
{
	const u32 guestPc = source.guestPc;
	const u32 fpuMode = source.fpuMode;
	if (bm_GetBlock(guestPc) != nullptr)
		return Mvc2PortableWarmResult::Existing;
	if (emit_FreeSpace() < mvc2_native::MaxBlockBytes
			|| smc_hotspots.find(guestPc) != smc_hotspots.end())
		return Mvc2PortableWarmResult::NoSpace;

	Sh4Context savedContext;
	memcpy(&savedContext, &Sh4cntx, sizeof(savedContext));
	fpscr_t mode;
	mode.full = fpuMode;
	RuntimeBlockInfo* fresh = ngen_AllocateBlock();
	bool setup = false;
	bool compiled = false;
	bool staged = false;
	try
	{
		setup = fresh->Setup(guestPc, mode);
		if (setup)
		{
			fresh->blockcheck_failures = 0;
			fresh->staging_runs = 100;
			ngen_Compile(fresh, !fresh->read_only,
				(guestPc & 0xFFFFFF) == 0x08300 || (guestPc & 0xFFFFFF) == 0x10000,
				false, true);
			if (stageFpuBlock && fresh->has_fpu_op)
			{
				// Keep the emitted variant out of the single-entry-per-PC map
				// until the matching FPSCR mode actually requests it.
				fresh->Discard();
				mvc2NativeCacheStagePortableBlock(fresh, source);
				staged = true;
			}
			else
			{
				bm_AddBlock(fresh);
				compiled = true;
			}
			fresh = nullptr; // ownership transferred to block manager or staged cache
		}
	}
	catch (...)
	{
		WARN_LOG(DYNAREC, "mvc2-portable: prewarm threw pc=%08X", guestPc);
	}
	if (fresh != nullptr)
	{
		if (setup)
			fresh->Discard();
		delete fresh;
	}
	memcpy(&Sh4cntx, &savedContext, sizeof(savedContext));
	if (staged)
		return Mvc2PortableWarmResult::Staged;
	return compiled
		? Mvc2PortableWarmResult::Compiled
		: Mvc2PortableWarmResult::DecodeFailed;
}

// Diagnostic-only execution path: regenerate at the original address, compare,
// then restore every byte before any guest code can run. Never advance the SH4.
bool rdv_ValidateNativeBlock(const mvc2_native::Record& entry)
{
	u8* destination = CodeCache + entry.offset;
	std::vector<u8> backup(destination, destination + mvc2_native::MaxBlockBytes);
	Sh4Context savedContext;
	memcpy(&savedContext, &Sh4cntx, sizeof(savedContext));
	u32* savedEmit = emit_ptr;
	u32* savedLimit = emit_ptr_limit;
	RuntimeBlockInfo* fresh = nullptr;
	bool setup = false;
	bool same = false;
	try
	{
		fresh = ngen_AllocateBlock();
		fpscr_t mode;
		mode.full = entry.fpuMode;
		setup = fresh->Setup(entry.guestPc, mode);
		if (setup)
		{
			emit_ptr = (u32*)destination;
			emit_ptr_limit = (u32*)(destination + mvc2_native::MaxBlockBytes);
			ngen_Compile(fresh, entry.flags & mvc2_native::NeedsSmcCheck,
				false, false, true);
			same = fresh->host_code_size == entry.size && fresh->sh4_code_size == entry.guestBytes
				&& fresh->guest_cycles == entry.cycles && fresh->guest_opcodes == entry.guestOps
				&& fresh->host_opcodes == entry.hostOps
				&& (u32)fresh->BlockType == entry.endType && fresh->BranchBlock == entry.branchPc
				&& fresh->NextBlock == entry.nextPc && fresh->relink_offset == entry.relinkOffset
				&& (((fresh->has_fpu_op ? mvc2_native::HasFpuOp : 0u)
						| (fresh->has_jcond ? mvc2_native::HasJcond : 0u)
						| (entry.flags & mvc2_native::NeedsSmcCheck))
					== (entry.flags & mvc2_native::CodegenFlags))
				&& memcmp(destination, backup.data(), entry.size) == 0;
		}
	}
	catch (...) { WARN_LOG(DYNAREC, "mvc2-native: regeneration threw; rejecting cache"); }
	if (setup) fresh->Discard();
	delete fresh;
	emit_ptr = savedEmit;
	emit_ptr_limit = savedLimit;
	same = same && memcmp(&savedContext, &Sh4cntx, sizeof(savedContext)) == 0;
	memcpy(&Sh4cntx, &savedContext, sizeof(savedContext));
	memcpy(destination, backup.data(), backup.size());
	vmem_platform_flush_cache(destination, destination + backup.size() - 1,
		destination, destination + backup.size() - 1);
	return same;
}
#endif

static bool recSh4_IsCpuRunning()
{
	return sh4Interp.IsCpuRunning();
}

void Get_Sh4Recompiler(sh4_if* cpu)
{
	cpu->Run = recSh4_Run;
	cpu->Stop = recSh4_Stop;
	cpu->Step = recSh4_Step;
	cpu->Reset = recSh4_Reset;
	cpu->Init = recSh4_Init;
	cpu->Term = recSh4_Term;
	cpu->IsCpuRunning = recSh4_IsCpuRunning;
	cpu->ResetCache = recSh4_ClearCache;
}

#endif  // FEAT_SHREC != DYNAREC_NONE
