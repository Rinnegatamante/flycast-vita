// Included inside the ARM32 backend: native blocks need its relinking metadata.
#ifdef __vita__
extern "C" const unsigned char mvc2_native_build_id[];
namespace mvc2_native {
static Header identity;
static std::vector<Record> loaded, learned;
static std::vector<u8> learnedCode, consumed;
struct KeyHash {
    size_t operator()(u64 key) const { return hashKey32(key); }
};
static std::unordered_multimap<u64, size_t, KeyHash> blockIndex;
static bool initialized, enabled, recording, validating, validateHits, reported;
static u32 mapGeneration, hits, misses, sourceRejects, usedRejects, validated, validationFailures, saturated;
static int validationTRegister, validationFlagsCondition;
static u64 protectedPageHash[4096];
static bool protectedPageVerified[4096];
static u32 pageChecks;
#ifdef VITA_FRAME_TRACE
static int testResetAfter;
static bool testResetDone, testResetPending;
#endif
static u64 started, deadline;
static std::string path;

static u64 configHash()
{
    float downclock = config::DynarecDownclock;
    u32 clockBits;
    memcpy(&clockBits, &downclock, sizeof(clockBits));
    const u32 values[] = { clockBits, (u32)(bool)config::DynarecIdleSkip,
        (u32)(bool)config::DynarecFloatHack, (u32)config::DynarecSmcChecks,
        (u32)(bool)config::DynarecUseNeon, (u32)config::DynarecSafeMode,
        (u32)cfgLoadBool("perf", "Mvc2DirectFpuPairs", false),
        (u32)cfgLoadBool("perf", "HlePatches", false),
        (u32)cfgLoadBool("perf", "Mvc2CopyLoopHle", false),
        (u32)cfgLoadBool("perf", "Mvc2CacheLoopHle", false),
        RAM_SIZE, VRAM_SIZE, ARAM_SIZE, SH4_TIMESLICE, (u32)_nvmem_4gb_space() };
    return XXH64(values, sizeof(values), 0);
}
static u64 headerHash(Header header)
{
    header.headerHash = 0;
    return XXH64(&header, sizeof(header), 0);
}
static u64 key(u32 guestPc, u32 fpuMode) { return blockKey(guestPc, fpuMode); }
static bool excludedPc(u32 guestPc)
{
    // Never bypass revision verification or configuration of accepted HLE entries.
    return guestPc == 0x8c02dec6 || guestPc == 0x8c1291e2 || guestPc == 0x8c17a6f0;
}
static bool sourceMatches(const Record& entry)
{
    FT_SCOPE(Emu, NativeCheck, entry.guestPc);
    if (entry.flags & NeedsSmcCheck) {
        // AnalyseBlock also specializes on RuntimeBlockInfo::read_only. A
        // writable-page record is therefore not interchangeable with the
        // protected-page version even when this block's own SH4 bytes did not
        // change. Require the page-protection class used during training.
        bool writable = false;
        u32 pageOffset = (entry.guestPc & RAM_MASK) & ~4095u;
        for (u32 page = 0; page < entry.pageCount; ++page, pageOffset += 4096)
            writable |= !bm_IsRamPageProtected(pageOffset);
        if (!writable)
            return false;
        const u32 offset = entry.guestPc & RAM_MASK;
        return XXH64(mem_b.data + offset, entry.guestBytes, 0) == entry.pageHash[0];
    }
    u32 offset = (entry.guestPc & RAM_MASK) & ~4095u;
    for (u32 page = 0; page < entry.pageCount; ++page, offset += 4096) {
        if (!bm_IsRamPageProtected(offset))
            return false;
        const u32 physicalPage = offset / 4096;
        if (!protectedPageVerified[physicalPage]) {
            protectedPageHash[physicalPage] = XXH64(mem_b.data + offset, 4096, 0);
            ++pageChecks;
        }
        if (entry.pageHash[page] != protectedPageHash[physicalPage]) return false;
    }
    return true;
}
static bool readCache()
{
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        NOTICE_LOG(DYNAREC, "mvc2-native: no cache; ordinary JIT");
        return false;
    }
    setvbuf(file, nullptr, _IOFBF, 65536);
    Header header{};
    bool valid = fread(&header, sizeof(header), 1, file) == 1;
    const char* reason = "header";
    valid = valid && !memcmp(header.magic, identity.magic, sizeof(header.magic))
        && header.version == Version && header.headerSize == sizeof(Header)
        && header.recordSize == sizeof(Record) && header.count <= MaxRecords
        && header.headerHash == headerHash(header) && header.reserved == 0;
    if (valid) {
        reason = "build/layout/config";
        valid = !memcmp(header.buildId, identity.buildId, sizeof(header.buildId))
            && header.codeBase == identity.codeBase && header.contextBase == identity.contextBase
            && header.prefixSize == identity.prefixSize && header.prefixHash == identity.prefixHash
            && header.layoutHash == identity.layoutHash && header.configHash == identity.configHash;
    }
    u32 end = identity.prefixSize;
    u64 digest = 0;
    if (valid) {
        reason = "records/checksum";
        loaded.reserve(header.count);
        for (u32 n = 0; n < header.count && valid; ++n) {
            Record entry{};
            valid = fread(&entry, sizeof(entry), 1, file) == 1
                && validRecord(entry, identity.prefixSize, end, CODE_SIZE, RAM_MASK)
                && !excludedPc(entry.guestPc);
            if (!valid) break;
            u8* destination = CodeCache + entry.offset;
            valid = fread(destination, entry.size, 1, file) == 1;
            if (!valid) break;
            digest = XXH64(&entry, sizeof(entry), digest);
            digest = XXH64(destination, entry.size, digest);
            loaded.push_back(entry);
            end = entry.offset + entry.size;
        }
        valid = valid && digest == header.payloadHash && header.extent == end
            && fgetc(file) == EOF && !ferror(file);
    }
    fclose(file);
    if (!valid) {
        loaded.clear();
        WARN_LOG(DYNAREC, "mvc2-native: cache rejected (%s); ordinary JIT", reason);
        return false; // No code is published and the normal allocator can overwrite it.
    }
    consumed.assign(loaded.size(), 0);
    blockIndex.reserve(loaded.size());
    for (size_t n = 0; n < loaded.size(); ++n)
        blockIndex.emplace(key(loaded[n].guestPc, loaded[n].fpuMode), n);
    emit_Skip(end - identity.prefixSize); // Reserve original positions, including holes.
    if (end > identity.prefixSize)
        vmem_platform_flush_cache(CodeCache + identity.prefixSize, CodeCache + end - 1,
                                  CodeCache + identity.prefixSize, CodeCache + end - 1);
    NOTICE_LOG(DYNAREC, "mvc2-native: loaded=%u extent=%u load_us=%llu (no decode/emission)",
        (unsigned)loaded.size(), end, (unsigned long long)(sceKernelGetProcessTimeWide() - started));
    return true;
}
static void saveCache()
{
    if (learned.empty()) return;
    Header header = identity;
    header.count = learned.size();
    header.extent = learned.back().offset + learned.back().size;
    size_t offset = 0;
    for (const Record& entry : learned) {
        header.payloadHash = XXH64(&entry, sizeof(entry), header.payloadHash);
        header.payloadHash = XXH64(learnedCode.data() + offset, entry.size, header.payloadHash);
        offset += entry.size;
    }
    header.headerHash = headerHash(header);
    const std::string temporary = path + ".tmp";
    FILE* file = fopen(temporary.c_str(), "wb");
    bool valid = file != nullptr;
    if (file) {
        setvbuf(file, nullptr, _IOFBF, 65536);
        valid = fwrite(&header, sizeof(header), 1, file) == 1;
        offset = 0;
        for (const Record& entry : learned) {
            valid = valid && fwrite(&entry, sizeof(entry), 1, file) == 1;
            valid = valid && fwrite(learnedCode.data() + offset, entry.size, 1, file) == 1;
            offset += entry.size;
        }
        valid = fclose(file) == 0 && valid;
    }
    valid = valid && rename(temporary.c_str(), path.c_str()) == 0;
    NOTICE_LOG(DYNAREC, "mvc2-native: saved=%u bytes=%u saturated=%u io_error=%u header_hash=%016llx path=%s",
        header.count, (unsigned)learnedCode.size(), saturated, !valid,
        (unsigned long long)header.headerHash, path.c_str());
    std::vector<Record>().swap(learned);
    std::vector<u8>().swap(learnedCode);
}
}

void mvc2NativeCacheReset()
{
    using namespace mvc2_native;
    initialized = enabled = recording = validating = reported = false;
    memset(protectedPageVerified, 0, sizeof(protectedPageVerified));
    std::vector<Record>().swap(loaded);
    std::vector<Record>().swap(learned);
    std::vector<u8>().swap(consumed);
    std::vector<u8>().swap(learnedCode);
    blockIndex.clear();
}
void mvc2NativeCacheInvalidatePage(u32 guestAddress)
{
    mvc2_native::protectedPageVerified[(guestAddress & RAM_MASK) / 4096] = false;
}
void mvc2NativeCacheBegin()
{
    using namespace mvc2_native;
    if (initialized) {
        if ((enabled || recording) && (configHash() != identity.configHash
                || _vmem_native_mapping_generation() != mapGeneration))
            enabled = recording = false;
        return;
    }
    initialized = true;
#ifdef VITA_JIT_RUN_PROFILE
    return; // That executable embeds per-RBI heap addresses in generated code.
#endif
    if (settings.content.gameId != "T1212N" || config::FullMMU || mmu_enabled()
            || !_nvmem_enabled() || RAM_SIZE != 16 * 1024 * 1024)
        return;
    recording = cfgLoadBool("perf", "Mvc2NativeCacheRecord", false);
    bool load = cfgLoadBool("perf", "Mvc2NativeCache", false);
    if (!recording && !load) return;
    if (memcmp(mvc2_native_build_id, "\x04\0\0\0\x14\0\0\0\x03\0\0\0GNU\0", 16)) {
        WARN_LOG(DYNAREC, "mvc2-native: missing linked build identity");
        recording = false;
        return;
    }
    identity = Header{};
    memcpy(identity.magic, "MVC2ARM1", 8);
    identity.version = Version;
    identity.headerSize = sizeof(Header);
    identity.recordSize = sizeof(Record);
    identity.codeBase = (uintptr_t)CodeCache;
    identity.contextBase = (uintptr_t)p_sh4rcb;
    identity.prefixSize = (u8*)emit_GetCCPtr() - CodeCache;
    identity.prefixHash = XXH64(CodeCache, identity.prefixSize, 0);
    identity.layoutHash = _vmem_native_layout_hash();
    identity.configHash = configHash();
    memcpy(identity.buildId, mvc2_native_build_id + 16, sizeof(identity.buildId));
    mapGeneration = _vmem_native_mapping_generation();
    hits = misses = sourceRejects = usedRejects = validated = validationFailures = saturated = 0;
    pageChecks = 0;
#ifdef VITA_FRAME_TRACE
    testResetAfter = cfgLoadInt("perf", "Mvc2NativeCacheResetAfter", 0);
#endif
    validateHits = cfgLoadBool("perf", "Mvc2NativeCacheValidate", false);
    started = sceKernelGetProcessTimeWide();
    deadline = started + std::max(5, std::min(120, cfgLoadInt("perf", "Mvc2NativeCacheSeconds", 30))) * 1000000ULL;
    path = get_writable_data_path("mvc2_native_cache.bin");
    // Deliberately separate cold training from replay for attributable measurements.
    if (recording) {
        learned.reserve(MaxRecords);
        learnedCode.reserve(MaxNativeBytes);
        NOTICE_LOG(DYNAREC, "mvc2-native: recording cold blocks, origin=%llu", (unsigned long long)started);
    } else if (load) {
        enabled = readCache();
    }
}
void mvc2NativeCacheRecord(RuntimeBlockInfo* block, int tRegister, int flagsCondition)
{
    using namespace mvc2_native;
    if (validating) {
        validationTRegister = tRegister;
        validationFlagsCondition = flagsCondition;
        return;
    }
    if (!recording || block->temp_block
            || block->blockcheck_failures || excludedPc(block->vaddr))
        return;
    if (_vmem_native_mapping_generation() != mapGeneration) {
        recording = enabled = false;
        WARN_LOG(DYNAREC, "mvc2-native: memory mapping changed; recording rejected");
        return;
    }
    if (learned.size() == MaxRecords || block->host_code_size > MaxNativeBytes - learnedCode.size()) {
        ++saturated;
        return;
    }
    Record entry{};
    entry.offset = (u8*)block->code - CodeCache;
    entry.size = block->host_code_size;
    entry.guestPc = block->vaddr;
    entry.fpuMode = block->fpu_cfg.full;
    entry.guestBytes = block->sh4_code_size;
    entry.cycles = block->guest_cycles;
    entry.guestOps = block->guest_opcodes;
    entry.hostOps = block->host_opcodes;
    entry.endType = block->BlockType;
    entry.branchPc = block->BranchBlock;
    entry.nextPc = block->NextBlock;
    entry.relinkOffset = block->relink_offset;
    entry.flags = (block->has_fpu_op ? HasFpuOp : 0u)
        | (block->has_jcond ? HasJcond : 0u)
        | (block->read_only ? 0u : NeedsSmcCheck);
    entry.tRegister = tRegister;
    entry.flagsCondition = flagsCondition;
    entry.pageCount = ((entry.guestPc & 4095) + entry.guestBytes + 4095) / 4096;
    u32 previousEnd = learned.empty() ? identity.prefixSize : learned.back().offset + learned.back().size;
    if (!validRecord(entry, identity.prefixSize, previousEnd, CODE_SIZE, RAM_MASK)) return;
    u32 offset = entry.guestPc & RAM_MASK;
    if (entry.flags & NeedsSmcCheck) {
        // Writable-page records retain the inline Reduced/Full check emitted
        // by the ordinary JIT. Key them by the exact SH4 bytes so multiple
        // code states at the same PC can coexist in one trained cache.
        entry.pageHash[0] = XXH64(mem_b.data + offset, entry.guestBytes, 0);
    } else {
        offset &= ~4095u;
        for (u32 page = 0; page < entry.pageCount; ++page, offset += 4096)
            entry.pageHash[page] = XXH64(mem_b.data + offset, 4096, 0);
    }
    learned.push_back(entry);
    const u8* code = (const u8*)block->code;
    learnedCode.insert(learnedCode.end(), code, code + entry.size); // Before linking/fastmem rewrites.
}
RuntimeBlockInfo* mvc2NativeCacheLookup(u32 guestPc, u32 fpuMode)
{
    using namespace mvc2_native;
    if (!enabled) return nullptr;
    if (mmu_enabled() || _vmem_native_mapping_generation() != mapGeneration) {
        enabled = false;
        return nullptr;
    }
    const auto range = blockIndex.equal_range(key(guestPc, fpuMode));
    for (auto it = range.first; it != range.second; ++it) {
        const size_t n = it->second;
        const Record& entry = loaded[n];
        if (consumed[n]) { ++usedRejects; continue; } // Never reuse live/stale native addresses.
        if (!sourceMatches(entry) || ((entry.flags & HasFpuOp) && (sr.status & (1 << 15)))) {
            ++sourceRejects;
            continue;
        }
        if (validateHits) {
            validationTRegister = validationFlagsCondition = -10;
            validating = true;
            bool same = rdv_ValidateNativeBlock(entry);
            validating = false;
            same = same && validationTRegister == entry.tRegister
                && validationFlagsCondition == entry.flagsCondition;
            if (!same) {
                ++validationFailures;
                enabled = false;
                ERROR_LOG(DYNAREC, "mvc2-native: regeneration mismatch pc=%08X; cache disabled", guestPc);
                return nullptr;
            }
            ++validated;
        }
        DynaRBI* block = new DynaRBI();
        block->vaddr = block->addr = entry.guestPc;
        block->code = (DynarecCodeEntryPtr)(CodeCache + entry.offset);
        block->lookups = block->runs = block->blockcheck_failures = 0;
        block->staging_runs = 100;
        block->host_code_size = entry.size;
        block->sh4_code_size = entry.guestBytes;
        block->guest_cycles = entry.cycles;
        block->guest_opcodes = entry.guestOps;
        block->host_opcodes = entry.hostOps;
        block->fpu_cfg.full = entry.fpuMode;
        block->BlockType = (BlockEndType)entry.endType;
        block->BranchBlock = entry.branchPc;
        block->NextBlock = entry.nextPc;
        block->relink_offset = entry.relinkOffset;
        block->relink_data = 0;
        block->pNextBlock = block->pBranchBlock = nullptr;
        block->has_fpu_op = entry.flags & HasFpuOp;
        block->has_jcond = entry.flags & HasJcond;
        block->temp_block = false;
        block->T_reg = entry.tRegister < 0 ? Register() : Register(entry.tRegister);
        block->nativeFlagsCondition = entry.flagsCondition;
        block->SetProtectedFlags();
        if (entry.flags & NeedsSmcCheck) {
            verify(!block->read_only);
        } else {
            verify(block->read_only);
            // Only now are these pages physically write-protected. The RAM
            // write callback and every cache/state reset revoke verification.
            u32 physicalPage = (entry.guestPc & RAM_MASK) / 4096;
            for (u32 page = 0; page < entry.pageCount; ++page)
                protectedPageVerified[physicalPage + page] = true;
        }
        consumed[n] = 1;
        ++hits;
        FT_MARK(Emu, NativeHit, guestPc, 0);
        return block;
    }
    ++misses;
    return nullptr;
}
void mvc2NativeCachePoll()
{
    using namespace mvc2_native;
#ifdef VITA_FRAME_TRACE
    if (enabled && !testResetDone && testResetAfter > 0 && testResetAfter <= 60
            && sceKernelGetProcessTimeWide() - started >= (u64)testResetAfter * 1000000) {
        testResetDone = true;
        testResetPending = true;
        // intc_sched still needs the current RBI to recover the guest PC when
        // UpdateSystem returns. Destroy blocks only after leaving native code.
        sh4_cpu.Stop();
        return;
    }
#endif
    if (!initialized || reported || (!enabled && !recording && !validationFailures)
            || sceKernelGetProcessTimeWide() < deadline)
        return;
    reported = true;
    NOTICE_LOG(DYNAREC, "mvc2-native: report hits=%u misses=%u source_reject=%u used_reject=%u validated=%u failures=%u page_checks=%u",
        hits, misses, sourceRejects, usedRejects, validated, validationFailures, pageChecks);
    if (recording) {
        recording = false;
        if (_vmem_native_mapping_generation() == mapGeneration && configHash() == identity.configHash)
            saveCache();
    }
}
#ifdef VITA_FRAME_TRACE
static bool mvc2NativeCacheApplyTestReset()
{
    using namespace mvc2_native;
    if (!testResetPending) return false;
    testResetPending = false;
    NOTICE_LOG(DYNAREC, "mvc2-native: diagnostic whole-cache reset hits=%u validated=%u failures=%u",
        hits, validated, validationFailures);
    sh4_cpu.ResetCache();
    return true;
}
#endif
#endif
