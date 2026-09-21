// Included inside the ARM32 backend: native blocks need its relinking metadata.
#ifdef __vita__
extern "C" const unsigned char mvc2_native_build_id[];
namespace mvc2_native {
static_assert(MaxNativeBytes <= CODE_SIZE,
    "persistent native cache cannot exceed the JIT code cache");
static Header identity;
static std::vector<Record> loaded, learned;
static std::vector<Relocation> loadedRelocations, learnedRelocations;
static std::vector<u8> loadedCode, learnedCode, consumed;
static std::vector<PortableRecord> portableRecords;
static std::vector<u8> portableSources;
static std::vector<u8> portableDone;
struct KeyHash {
    size_t operator()(u64 key) const { return hashKey32(key); }
};
struct StagedPortableBlock {
    RuntimeBlockInfo* block;
    PortableRecord source;
};
static std::unordered_multimap<u64, size_t, KeyHash> blockIndex;
static std::unordered_multimap<u64, StagedPortableBlock, KeyHash> stagedPortableBlocks;
static std::vector<PortableRecord> portableTrainingRecords;
static std::vector<u8> portableTrainingSources;
static std::unordered_multimap<u64, size_t, KeyHash> portableTrainingIndex;
static bool initialized, enabled, recording, merging, validating, validateHits, reported;
static bool portableRecording, portableTrainingDirty, portablePrewarming;
static bool portableTrainingDiskExists, portableExitOnSaturation;
static std::atomic<bool> portableExitRequested{false};
static PortableHeader portableTrainingDiskHeader;
static u32 portableTrainingSaturated, portableTrainingInvalidSkipped;
static u32 mapGeneration, hits, misses, sourceRejects, usedRejects, duplicateSkips;
static u32 validated, validationFailures, saturated;
static int validationTRegister, validationFlagsCondition;
static u64 protectedPageHash[4096];
static bool protectedPageVerified[4096];
static u32 pageChecks;
static bool portableEnabled;
static size_t portableCursor;
static u32 portableBudgetUs, portableInitialBudgetUs, portableMaxRenders, portablePolls;
static u32 portableCompiled, portableExisting, portableLastRenderCount;
static bool portableRenderBaseline, portableInitialWarmDone;
static u32 portableRejected, portableStaged, portableStagedHits, portableStagedRejects;
static u32 portableStagedProtectionRejects, portableStagedSourceRejects;
static u32 portableStagedFpuRejects;
static u64 portablePageHash[4096];
static u32 portablePageEpoch[4096], portableEpoch;
#ifdef VITA_FRAME_TRACE
static int testResetAfter;
static bool testResetDone, testResetPending;
#endif
static u64 started, deadline;
static u32 checkpointSeconds, portableTrainingSourceLimit;
static std::string path, portableTrainingPath;
static constexpr size_t RecordReserveChunk = 12000;
static constexpr size_t RelocationReserveChunk = 12000;
static constexpr size_t NativeReserveChunk = 2 * 1024 * 1024;

static bool reserveLearnedCapacity(size_t nextNativeBytes, size_t nextRelocations, bool initial)
{
    const size_t recordLimit = MaxRecords - loaded.size();
    const size_t nativeLimit = MaxNativeBytes - loadedCode.size();
    const size_t nativeNeeded = learnedCode.size() + nextNativeBytes;
    const size_t relocationLimit = MaxTotalRelocations - loadedRelocations.size();
    const size_t relocationNeeded = learnedRelocations.size() + nextRelocations;
    try {
        if (initial || learned.size() == learned.capacity()) {
            const size_t target = initial
                ? std::min(recordLimit, RecordReserveChunk)
                : std::min(recordLimit, learned.capacity() + RecordReserveChunk);
            if (target > learned.capacity())
                learned.reserve(target);
        }
        if (initial || nativeNeeded > learnedCode.capacity()) {
            const size_t target = initial
                ? std::min(nativeLimit, NativeReserveChunk)
                : std::min(nativeLimit, std::max(nativeNeeded,
                    learnedCode.capacity() + NativeReserveChunk));
            if (target > learnedCode.capacity())
                learnedCode.reserve(target);
        }
        if (initial || relocationNeeded > learnedRelocations.capacity()) {
            const size_t target = initial
                ? std::min(relocationLimit, RelocationReserveChunk)
                : std::min(relocationLimit, std::max(relocationNeeded,
                    learnedRelocations.capacity() + RelocationReserveChunk));
            if (target > learnedRelocations.capacity())
                learnedRelocations.reserve(target);
        }
    } catch (...) {
        saturated = 1;
        WARN_LOG(DYNAREC, "mvc2-native: collector memory reservation failed records=%u/%u bytes=%u/%u",
            (u32)(loaded.size() + learned.size()), MaxRecords,
            (u32)(loadedCode.size() + learnedCode.size()), MaxNativeBytes);
        return false;
    }
    return learned.size() < recordLimit && nativeNeeded <= nativeLimit
        && relocationNeeded <= relocationLimit;
}

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
static u64 portableHeaderHash(PortableHeader header)
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
static bool portableSourceMatches(const PortableRecord& entry)
{
    const u32 guestOffset = entry.guestPc & RAM_MASK;
    if (entry.flags & PortableExactSourceHash)
        return XXH64(mem_b.data + guestOffset, entry.guestBytes, 0) == entry.sourceHash;
    u32 pageOffset = guestOffset & ~4095u;
    for (u32 page = 0; page < entry.pageCount; ++page, pageOffset += 4096) {
        const u32 physicalPage = pageOffset / 4096;
        if (portablePageEpoch[physicalPage] != portableEpoch) {
            portablePageHash[physicalPage] = XXH64(mem_b.data + pageOffset, 4096, 0);
            portablePageEpoch[physicalPage] = portableEpoch;
        }
        if (portablePageHash[physicalPage] != entry.pageHash[page])
            return false;
    }
    return true;
}
static bool portableSourceMatchesNow(const PortableRecord& entry)
{
    const u32 guestOffset = entry.guestPc & RAM_MASK;
    if (entry.flags & PortableExactSourceHash)
        return XXH64(mem_b.data + guestOffset, entry.guestBytes, 0) == entry.sourceHash;
    u32 pageOffset = guestOffset & ~4095u;
    for (u32 page = 0; page < entry.pageCount; ++page, pageOffset += 4096) {
        if (!bm_IsRamPageProtected(pageOffset)
                || XXH64(mem_b.data + pageOffset, 4096, 0) != entry.pageHash[page])
            return false;
    }
    return true;
}
static bool portableProtectionMatches(const RuntimeBlockInfo* block)
{
    bool readOnly = (block->addr & 0x1fff0000) != 0x0c000000;
    for (u32 address = block->addr & ~4095u;
            readOnly && address < block->addr + block->sh4_code_size;
            address += 4096)
        readOnly = bm_IsRamPageProtected(address);
    return block->read_only == readOnly;
}
static bool portableRecordProtectionMatches(const PortableRecord& record)
{
    bool readOnly = (record.guestPc & 0x1fff0000) != 0x0c000000;
    for (u32 address = record.guestPc & ~4095u;
            readOnly && address < record.guestPc + record.guestBytes;
            address += 4096)
        readOnly = bm_IsRamPageProtected(address);
    const bool expectedReadOnly = !(record.flags & PortableWritableSource);
    return readOnly == expectedReadOnly;
}
static bool readPortableFile(const std::string& portablePath,
                             std::vector<PortableRecord>& records,
                             std::vector<u8>& sources)
{
    FILE* file = fopen(portablePath.c_str(), "rb");
    if (!file)
        return false;
    setvbuf(file, nullptr, _IOFBF, 65536);
    PortableHeader header{};
    bool valid = fread(&header, sizeof(header), 1, file) == 1;
    const bool legacy = valid && header.version == LegacyPortableVersion;
    const u32 sourceBytes = legacy ? 0 : header.reserved[0];
    valid = valid
        && !memcmp(header.magic, "MVC2SH41", 8)
        && (legacy || header.version == PortableVersion)
        && header.headerSize == sizeof(header)
        && header.recordSize == (legacy ? sizeof(LegacyPortableRecord)
                                        : sizeof(PortableRecord))
        && header.count <= MaxPortableRecords
        && sourceBytes <= MaxPortableSourceBytes
        && header.gameIdentity == PortableGameIdentity
        && (legacy ? !header.reserved[0] : true)
        && !header.reserved[1] && !header.reserved[2] && !header.reserved[3]
        && header.headerHash == portableHeaderHash(header);
    u64 digest = 0;
    if (valid) {
        try { records.reserve(std::min<u32>(header.count, 16000)); }
        catch (...) { valid = false; }
    }
    for (u32 index = 0; index < header.count && valid; ++index) {
        PortableRecord record{};
        if (legacy) {
            LegacyPortableRecord oldRecord{};
            valid = fread(&oldRecord, sizeof(oldRecord), 1, file) == 1
                && validLegacyPortableRecord(oldRecord, RAM_MASK);
            if (valid) {
                digest = XXH64(&oldRecord, sizeof(oldRecord), digest);
                record = upgradeLegacyPortableRecord(oldRecord);
            }
        } else {
            valid = fread(&record, sizeof(record), 1, file) == 1
                && validPortableRecord(record, RAM_MASK);
            if (valid)
                digest = XXH64(&record, sizeof(record), digest);
        }
        if (!valid) break;
        if (hasPortableSource(record)
                && (record.sourceBytes > sourceBytes
                    || record.sourceOffset > sourceBytes - record.sourceBytes)) {
            valid = false;
            break;
        }
        try { records.push_back(record); }
        catch (...) { valid = false; }
    }
    if (valid && sourceBytes) {
        try { sources.resize(sourceBytes); }
        catch (...) { valid = false; }
        if (valid) {
            valid = fread(sources.data(), sourceBytes, 1, file) == 1;
            if (valid)
                digest = XXH64(sources.data(), sources.size(), digest);
        }
    }
    valid = valid && digest == header.payloadHash && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!valid) {
        records.clear();
        sources.clear();
    }
    return valid;
}
static bool readPortableCoverage()
{
    const std::string portablePath = get_writable_data_path("mvc2_portable_cache.bin");
    std::vector<PortableRecord> records;
    std::vector<u8> sources;
    if (!readPortableFile(portablePath, records, sources)) {
        WARN_LOG(DYNAREC, "mvc2-portable: coverage rejected; ordinary JIT");
        return false;
    }
    portableRecords.swap(records);
    portableSources.swap(sources);
    try { portableDone.assign(portableRecords.size(), 0); }
    catch (...) {
        std::vector<PortableRecord>().swap(portableRecords);
        std::vector<u8>().swap(portableSources);
        WARN_LOG(DYNAREC, "mvc2-portable: state allocation failed; ordinary JIT");
        return false;
    }
    NOTICE_LOG(DYNAREC, "mvc2-portable: loaded=%u source_bytes=%u build_independent=1 budget_us=%u initial_budget_us=%u max_renders=%u",
        (u32)portableRecords.size(),
        (u32)portableSources.size(),
        portableBudgetUs, portableInitialBudgetUs, portableMaxRenders);
    return !portableRecords.empty();
}
static bool readPortableHeader(const std::string& portablePath,
                               PortableHeader& header)
{
    FILE* file = fopen(portablePath.c_str(), "rb");
    if (!file)
        return false;
    const bool read = fread(&header, sizeof(header), 1, file) == 1;
    fclose(file);
    if (!read)
        return false;
    const bool legacy = header.version == LegacyPortableVersion;
    return !memcmp(header.magic, "MVC2SH41", 8)
        && (legacy || header.version == PortableVersion)
        && header.headerSize == sizeof(header)
        && header.recordSize == (legacy ? sizeof(LegacyPortableRecord)
                                        : sizeof(PortableRecord))
        && header.count <= MaxPortableRecords
        && (legacy ? !header.reserved[0]
                   : header.reserved[0] <= MaxPortableSourceBytes)
        && header.gameIdentity == PortableGameIdentity
        && !header.reserved[1] && !header.reserved[2] && !header.reserved[3]
        && header.headerHash == portableHeaderHash(header);
}
static bool portableTrainingDiskUnchanged()
{
    FILE* probe = fopen(portableTrainingPath.c_str(), "rb");
    if (!probe)
        return !portableTrainingDiskExists;
    fclose(probe);
    if (!portableTrainingDiskExists)
        return false;
    PortableHeader current{};
    if (!readPortableHeader(portableTrainingPath, current))
        return false;
    return !memcmp(&current, &portableTrainingDiskHeader, sizeof(current));
}
static bool savePortableTraining()
{
    if (!portableTrainingDirty && !portableTrainingSaturated)
        return true;
    if (!portableTrainingDiskUnchanged()) {
        WARN_LOG(DYNAREC, "mvc2-portable-training: checkpoint refused; on-disk shard changed");
        return false;
    }
    const u32 diskSourceBytes = portableTrainingDiskExists
        && portableTrainingDiskHeader.version == PortableVersion
        ? portableTrainingDiskHeader.reserved[0] : 0;
    if (portableTrainingDiskExists
            && (portableTrainingRecords.size() < portableTrainingDiskHeader.count
                || portableTrainingSources.size() < diskSourceBytes)) {
        WARN_LOG(DYNAREC, "mvc2-portable-training: checkpoint refused; would shrink records=%u/%u source_bytes=%u/%u",
            (u32)portableTrainingRecords.size(), portableTrainingDiskHeader.count,
            (u32)portableTrainingSources.size(), diskSourceBytes);
        return false;
    }
    PortableHeader header{};
    memcpy(header.magic, "MVC2SH41", 8);
    header.version = PortableVersion;
    header.headerSize = sizeof(header);
    header.recordSize = sizeof(PortableRecord);
    header.count = portableTrainingRecords.size();
    header.gameIdentity = PortableGameIdentity;
    header.reserved[0] = portableTrainingSources.size();
    for (const PortableRecord& record : portableTrainingRecords) {
        if (!validPortableRecord(record, RAM_MASK)
                || (hasPortableSource(record)
                    && (record.sourceBytes > portableTrainingSources.size()
                        || record.sourceOffset
                            > portableTrainingSources.size() - record.sourceBytes))) {
            WARN_LOG(DYNAREC, "mvc2-portable-training: invalid in-memory record; checkpoint rejected");
            return false;
        }
        header.payloadHash = XXH64(&record, sizeof(record), header.payloadHash);
    }
    if (!portableTrainingSources.empty())
        header.payloadHash = XXH64(portableTrainingSources.data(),
            portableTrainingSources.size(), header.payloadHash);
    header.headerHash = portableHeaderHash(header);
    const std::string temporary = portableTrainingPath + ".tmp";
    FILE* file = fopen(temporary.c_str(), "wb");
    bool valid = file != nullptr;
    if (file) {
        setvbuf(file, nullptr, _IOFBF, 65536);
        valid = fwrite(&header, sizeof(header), 1, file) == 1;
        if (!portableTrainingRecords.empty())
            valid = fwrite(portableTrainingRecords.data(), sizeof(PortableRecord),
                portableTrainingRecords.size(), file) == portableTrainingRecords.size()
                && valid;
        if (!portableTrainingSources.empty())
            valid = fwrite(portableTrainingSources.data(), 1,
                portableTrainingSources.size(), file) == portableTrainingSources.size()
                && valid;
        valid = fclose(file) == 0 && valid;
    }
    valid = valid && rename(temporary.c_str(), portableTrainingPath.c_str()) == 0;
    NOTICE_LOG(DYNAREC, "mvc2-portable-training: saved=%u source_bytes=%u saturated=%u io_error=%u path=%s invalid_skipped=%u",
        (u32)portableTrainingRecords.size(), (u32)portableTrainingSources.size(),
        portableTrainingSaturated, !valid, portableTrainingPath.c_str(),
        portableTrainingInvalidSkipped);
    if (valid)
    {
        portableTrainingDirty = false;
        portableTrainingDiskExists = true;
        portableTrainingDiskHeader = header;
    }
    return valid;
}
static bool beginPortableTraining()
{
    portableTrainingPath = get_writable_data_path("mvc2_portable_training.bin");
    FILE* probe = fopen(portableTrainingPath.c_str(), "rb");
    const bool exists = probe != nullptr;
    if (probe)
        fclose(probe);
    if (exists && !readPortableFile(portableTrainingPath,
                                    portableTrainingRecords,
                                    portableTrainingSources)) {
        WARN_LOG(DYNAREC, "mvc2-portable-training: existing file rejected; collection disabled");
        return false;
    }
    portableTrainingDiskExists = exists;
    portableTrainingDiskHeader = PortableHeader{};
    if (exists && !readPortableHeader(portableTrainingPath,
                                      portableTrainingDiskHeader)) {
        WARN_LOG(DYNAREC, "mvc2-portable-training: existing header rejected; collection disabled");
        return false;
    }
    if (portableTrainingRecords.size() >= MaxPortableRecords
            || portableTrainingSources.size() >= portableTrainingSourceLimit)
        portableTrainingSaturated = 1;
    try {
        portableTrainingRecords.reserve(std::min<size_t>(MaxPortableRecords,
            portableTrainingRecords.size() + RecordReserveChunk));
        portableTrainingSources.reserve(std::min<size_t>(portableTrainingSourceLimit,
            portableTrainingSources.size() + 256 * 1024));
        portableTrainingIndex.reserve(portableTrainingRecords.size()
            + RecordReserveChunk);
        for (size_t index = 0; index < portableTrainingRecords.size(); ++index) {
            const PortableRecord& record = portableTrainingRecords[index];
            portableTrainingIndex.emplace(key(record.guestPc, record.fpuMode), index);
        }
    } catch (...) {
        WARN_LOG(DYNAREC, "mvc2-portable-training: initial allocation failed; collection disabled");
        return false;
    }
    NOTICE_LOG(DYNAREC, "mvc2-portable-training: recording base=%u source_bytes=%u source_limit=%u saturated=%u",
        (u32)portableTrainingRecords.size(), (u32)portableTrainingSources.size(),
        portableTrainingSourceLimit, portableTrainingSaturated);
    return true;
}
static void recordPortableSource(RuntimeBlockInfo* block)
{
    if (!portableRecording || portablePrewarming || validating
            || portableTrainingSaturated || block->temp_block
            || block->blockcheck_failures || excludedPc(block->vaddr))
        return;
    PortableRecord record{};
    record.guestPc = block->vaddr;
    record.fpuMode = block->fpu_cfg.full;
    record.guestBytes = block->sh4_code_size;
    record.flags = block->read_only ? 0u
        : PortableExactSourceHash | PortableWritableSource;
    record.pageCount = ((record.guestPc & 4095) + record.guestBytes + 4095) / 4096;
    record.sourceOffset = block->read_only
        ? NoPortableSource : portableTrainingSources.size();
    record.sourceBytes = block->read_only ? 0 : record.guestBytes;
    const u32 guestOffset = record.guestPc & RAM_MASK;
    if (portableTrainingRecords.size() >= MaxPortableRecords
            || portableTrainingSources.size() > portableTrainingSourceLimit
            || (record.sourceBytes && record.sourceBytes
                > portableTrainingSourceLimit - portableTrainingSources.size())) {
        portableTrainingSaturated = 1;
        WARN_LOG(DYNAREC, "mvc2-portable-training: capacity reached records=%u source_bytes=%u",
            (u32)portableTrainingRecords.size(), (u32)portableTrainingSources.size());
        return;
    }
    if (!validPortableRecord(record, RAM_MASK)) {
        ++portableTrainingInvalidSkipped;
        if (portableTrainingInvalidSkipped == 1)
            WARN_LOG(DYNAREC, "mvc2-portable-training: skipping invalid block pc=%08x bytes=%u pages=%u",
                record.guestPc, record.guestBytes, record.pageCount);
        return;
    }
    if (record.flags & PortableExactSourceHash) {
        record.sourceHash = XXH64(mem_b.data + guestOffset, record.guestBytes, 0);
    } else {
        u32 pageOffset = guestOffset & ~4095u;
        for (u32 page = 0; page < record.pageCount; ++page, pageOffset += 4096)
            record.pageHash[page] = XXH64(mem_b.data + pageOffset, 4096, 0);
    }
    const auto range = portableTrainingIndex.equal_range(
        key(record.guestPc, record.fpuMode));
    for (auto it = range.first; it != range.second; ++it) {
        const PortableRecord& previous = portableTrainingRecords[it->second];
        if (samePortableSource(previous, record)) {
            if (!hasPortableSource(record)
                    || (previous.sourceBytes == record.sourceBytes
                        && !memcmp(portableTrainingSources.data() + previous.sourceOffset,
                            mem_b.data + guestOffset, record.sourceBytes)))
                return;
        }
    }
    const size_t previousRecordCount = portableTrainingRecords.size();
    const size_t previousSourceBytes = portableTrainingSources.size();
    try {
        if (portableTrainingRecords.size() == portableTrainingRecords.capacity())
            portableTrainingRecords.reserve(std::min<size_t>(MaxPortableRecords,
                portableTrainingRecords.capacity() + RecordReserveChunk));
        const size_t neededSourceBytes = portableTrainingSources.size()
            + record.sourceBytes;
        if (neededSourceBytes > portableTrainingSources.capacity())
            portableTrainingSources.reserve(std::min<size_t>(portableTrainingSourceLimit,
                std::max(neededSourceBytes,
                    portableTrainingSources.capacity() + 256 * 1024)));
        if (record.sourceBytes)
            portableTrainingSources.insert(portableTrainingSources.end(),
                mem_b.data + guestOffset,
                mem_b.data + guestOffset + record.sourceBytes);
        portableTrainingRecords.push_back(record);
        portableTrainingIndex.emplace(key(record.guestPc, record.fpuMode),
                                      previousRecordCount);
        portableTrainingDirty = true;
    } catch (...) {
        portableTrainingRecords.resize(previousRecordCount);
        portableTrainingSources.resize(previousSourceBytes);
        portableTrainingSaturated = 1;
        WARN_LOG(DYNAREC, "mvc2-portable-training: allocation failed; collection stopped");
    }
}
struct PortableWarmStats {
    u32 scanned, unavailable, matched, stored, compiled, existing, rejected, staged;
    u64 elapsedUs;
};
static PortableWarmStats prewarmPortableCoverageBudget(u32 budgetUs,
                                                        bool modeIndependent,
                                                        bool allowStoredSource = false)
{
    PortableWarmStats stats{};
    if (!portableEnabled || portableRecords.empty() || !budgetUs)
        return stats;
    const u64 start = sceKernelGetProcessTimeWide();
    const size_t count = portableRecords.size();
    ++portableEpoch;
    if (!portableEpoch) {
        memset(portablePageEpoch, 0, sizeof(portablePageEpoch));
        portableEpoch = 1;
    }
    while (stats.scanned < count) {
        if (sceKernelGetProcessTimeWide() - start >= budgetUs)
            break;
        const size_t index = portableCursor;
        portableCursor = (portableCursor + 1) % count;
        ++stats.scanned;
        if (portableDone[index])
            continue;
        const PortableRecord& record = portableRecords[index];
        const bool currentMode = record.fpuMode == fpscr.full;
        const u8* storedSource = nullptr;
        if (allowStoredSource && hasPortableSource(record)
                && (record.flags & (PortableExactSourceHash | PortableWritableSource))
                    == (PortableExactSourceHash | PortableWritableSource)) {
            const u8* candidate = portableSources.data() + record.sourceOffset;
            if (XXH64(candidate, record.sourceBytes, 0) == record.sourceHash)
                storedSource = candidate;
        }
        // A stored writable variant must always be staged, even when the same
        // bytes happen to be resident now. Publishing it directly lets a later
        // overlay write invalidate it before the variant is actually needed.
        const bool residentSource = storedSource == nullptr
            && portableSourceMatches(record)
            && portableRecordProtectionMatches(record);
        if ((!currentMode && !modeIndependent)
                || (!residentSource && storedSource == nullptr)) {
            ++stats.unavailable;
        } else {
            ++stats.matched;
            stats.stored += storedSource != nullptr;
            portablePrewarming = true;
            const Mvc2PortableWarmResult result = rdv_PrewarmPortableBlock(
                record, modeIndependent && !currentMode, storedSource);
            portablePrewarming = false;
            if (result == Mvc2PortableWarmResult::NoSpace) {
                portableEnabled = false;
                WARN_LOG(DYNAREC, "mvc2-portable: prewarm stopped; JIT cache has no safe reserve");
                break;
            }
            portableDone[index] = 1;
            if (result == Mvc2PortableWarmResult::Compiled) {
                ++stats.compiled;
                ++portableCompiled;
            } else if (result == Mvc2PortableWarmResult::Existing) {
                ++stats.existing;
                ++portableExisting;
            } else if (result == Mvc2PortableWarmResult::Staged) {
                ++stats.staged;
                ++portableStaged;
            } else {
                ++stats.rejected;
                ++portableRejected;
            }
        }
    }
    stats.elapsedUs = sceKernelGetProcessTimeWide() - start;
    return stats;
}
static void prewarmPortableCoverage()
{
    if (!portableEnabled || portableRecords.empty() || !portableBudgetUs)
        return;
    const u32 renderCount = ta_getGuestRenderCount();
    if (!portableRenderBaseline || renderCount < portableLastRenderCount) {
        portableLastRenderCount = renderCount;
        portableRenderBaseline = true;
        return;
    }
    const u32 renderDelta = renderCount - portableLastRenderCount;
    portableLastRenderCount = renderCount;
    ++portablePolls;
    if (portableMaxRenders && renderDelta > portableMaxRenders) {
        if (portablePolls % 30 == 0)
            NOTICE_LOG(DYNAREC, "mvc2-portable: active interval renders=%u; prewarm deferred prepared=%u/%u staged_live=%u staged_hits=%u staged_skips=%u protection_skips=%u source_skips=%u fpu_skips=%u",
                renderDelta, portableCompiled + portableExisting + portableStaged,
                (u32)portableRecords.size(), (u32)stagedPortableBlocks.size(),
                portableStagedHits, portableStagedRejects,
                portableStagedProtectionRejects, portableStagedSourceRejects,
                portableStagedFpuRejects);
        return;
    }
    const PortableWarmStats stats = prewarmPortableCoverageBudget(portableBudgetUs, false);
    if (stats.compiled || stats.rejected || portablePolls % 30 == 0) {
        NOTICE_LOG(DYNAREC, "mvc2-portable: prewarm renders=%u scanned=%u unavailable=%u matched=%u stored=%u compiled=%u existing=%u staged=%u rejected=%u total=%u/%u elapsed_us=%llu",
            renderDelta, stats.scanned, stats.unavailable, stats.matched,
            stats.stored, stats.compiled, stats.existing, stats.staged, stats.rejected,
            portableCompiled + portableExisting + portableStaged,
            (u32)portableRecords.size(),
            (unsigned long long)stats.elapsedUs);
    }
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
    const bool legacy = valid && header.version == LegacyVersion;
    const char* reason = "header";
    valid = valid && !memcmp(header.magic, identity.magic, sizeof(header.magic))
        && (legacy || header.version == Version) && header.headerSize == sizeof(Header)
        && header.recordSize == (legacy ? sizeof(LegacyRecord) : sizeof(Record))
        && header.count <= MaxRecords && header.relocationCount <= MaxTotalRelocations
        && (!legacy || header.relocationCount == 0)
        && header.extent >= identity.prefixSize && header.extent <= CODE_SIZE
        && header.headerHash == headerHash(header);
    if (valid) {
        reason = "build/layout/config";
        valid = !memcmp(header.buildId, identity.buildId, sizeof(header.buildId))
            && header.codeBase == identity.codeBase && header.contextBase == identity.contextBase
            && header.prefixSize == identity.prefixSize && header.prefixHash == identity.prefixHash
            && header.layoutHash == identity.layoutHash && header.configHash == identity.configHash;
    }
    u32 end = identity.prefixSize;
    u32 nativeBytes = 0;
    u32 relocationCount = 0;
    u64 digest = 0;
    if (valid) {
        reason = "records/checksum";
        loaded.reserve(header.count);
        if (merging) {
            loadedCode.reserve(std::min<u32>(header.extent - header.prefixSize, MaxNativeBytes));
            loadedRelocations.reserve(header.relocationCount);
        }
        std::array<Relocation, MaxRelocations> entryRelocations;
        for (u32 n = 0; n < header.count && valid; ++n) {
            Record entry{};
            if (legacy) {
                LegacyRecord oldEntry{};
                valid = fread(&oldEntry, sizeof(oldEntry), 1, file) == 1;
                if (valid) {
                    digest = XXH64(&oldEntry, sizeof(oldEntry), digest);
                    entry = upgradeLegacyRecord(oldEntry);
                }
            } else {
                valid = fread(&entry, sizeof(entry), 1, file) == 1;
                if (valid)
                    digest = XXH64(&entry, sizeof(entry), digest);
            }
            valid = valid && validRecord(entry, identity.prefixSize, end, CODE_SIZE, RAM_MASK)
                && entry.size <= MaxNativeBytes - nativeBytes
                && entry.relocationCount <= MaxTotalRelocations - relocationCount
                && !excludedPc(entry.guestPc);
            if (!valid) break;
            if (entry.relocationCount) {
                valid = fread(entryRelocations.data(), sizeof(Relocation),
                              entry.relocationCount, file) == entry.relocationCount;
                if (!valid) break;
                digest = XXH64(entryRelocations.data(),
                               entry.relocationCount * sizeof(Relocation), digest);
            }
            u8* destination = CodeCache + entry.offset;
            valid = fread(destination, entry.size, 1, file) == 1;
            if (!valid) break;
            digest = XXH64(destination, entry.size, digest);
            for (u32 relocation = 0; relocation < entry.relocationCount && valid; ++relocation)
                valid = validRelocation(entry, entryRelocations[relocation],
                                        destination, identity.codeBase)
                    && !targetsMovableCode(entryRelocations[relocation], identity.codeBase,
                                           identity.prefixSize, CODE_SIZE);
            if (!valid) break;
            loaded.push_back(entry);
            if (merging) {
                loadedCode.insert(loadedCode.end(), destination, destination + entry.size);
                loadedRelocations.insert(loadedRelocations.end(), entryRelocations.begin(),
                                         entryRelocations.begin() + entry.relocationCount);
            }
            nativeBytes += entry.size;
            relocationCount += entry.relocationCount;
            end = entry.offset + entry.size;
        }
        valid = valid && digest == header.payloadHash && header.extent == end
            && relocationCount == header.relocationCount
            && fgetc(file) == EOF && !ferror(file);
    }
    fclose(file);
    if (!valid) {
        loaded.clear();
        loadedCode.clear();
        loadedRelocations.clear();
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
    NOTICE_LOG(DYNAREC, "mvc2-native: loaded=%u relocations=%u extent=%u version=%u load_us=%llu (no decode/emission)",
        (unsigned)loaded.size(), relocationCount, end, header.version,
        (unsigned long long)(sceKernelGetProcessTimeWide() - started));
    return true;
}
static bool saveCache(bool keepLearned)
{
    if (learned.empty()) return true;
    Header header = identity;
    header.count = loaded.size() + learned.size();
    header.extent = learned.back().offset + learned.back().size;
    header.relocationCount = loadedRelocations.size() + learnedRelocations.size();
    size_t offset = 0, relocationOffset = 0;
    for (const Record& entry : loaded) {
        header.payloadHash = XXH64(&entry, sizeof(entry), header.payloadHash);
        if (entry.relocationCount)
            header.payloadHash = XXH64(loadedRelocations.data() + relocationOffset,
                entry.relocationCount * sizeof(Relocation), header.payloadHash);
        header.payloadHash = XXH64(loadedCode.data() + offset, entry.size, header.payloadHash);
        relocationOffset += entry.relocationCount;
        offset += entry.size;
    }
    offset = relocationOffset = 0;
    for (const Record& entry : learned) {
        header.payloadHash = XXH64(&entry, sizeof(entry), header.payloadHash);
        if (entry.relocationCount)
            header.payloadHash = XXH64(learnedRelocations.data() + relocationOffset,
                entry.relocationCount * sizeof(Relocation), header.payloadHash);
        header.payloadHash = XXH64(learnedCode.data() + offset, entry.size, header.payloadHash);
        relocationOffset += entry.relocationCount;
        offset += entry.size;
    }
    header.headerHash = headerHash(header);
    const std::string temporary = path + ".tmp";
    FILE* file = fopen(temporary.c_str(), "wb");
    bool valid = file != nullptr;
    if (file) {
        setvbuf(file, nullptr, _IOFBF, 65536);
        valid = fwrite(&header, sizeof(header), 1, file) == 1;
        offset = relocationOffset = 0;
        for (const Record& entry : loaded) {
            valid = valid && fwrite(&entry, sizeof(entry), 1, file) == 1;
            if (entry.relocationCount)
                valid = valid && fwrite(loadedRelocations.data() + relocationOffset,
                    sizeof(Relocation), entry.relocationCount, file) == entry.relocationCount;
            valid = valid && fwrite(loadedCode.data() + offset, entry.size, 1, file) == 1;
            relocationOffset += entry.relocationCount;
            offset += entry.size;
        }
        offset = relocationOffset = 0;
        for (const Record& entry : learned) {
            valid = valid && fwrite(&entry, sizeof(entry), 1, file) == 1;
            if (entry.relocationCount)
                valid = valid && fwrite(learnedRelocations.data() + relocationOffset,
                    sizeof(Relocation), entry.relocationCount, file) == entry.relocationCount;
            valid = valid && fwrite(learnedCode.data() + offset, entry.size, 1, file) == 1;
            relocationOffset += entry.relocationCount;
            offset += entry.size;
        }
        valid = fclose(file) == 0 && valid;
    }
    valid = valid && rename(temporary.c_str(), path.c_str()) == 0;
    NOTICE_LOG(DYNAREC, "mvc2-native: saved=%u base=%u added=%u relocations=%u bytes=%u saturated=%u io_error=%u header_hash=%016llx path=%s",
        header.count, (unsigned)loaded.size(), (unsigned)learned.size(),
        header.relocationCount, (unsigned)(loadedCode.size() + learnedCode.size()), saturated, !valid,
        (unsigned long long)header.headerHash, path.c_str());
    if (valid && !keepLearned) {
        std::vector<Record>().swap(learned);
        std::vector<Relocation>().swap(learnedRelocations);
        std::vector<u8>().swap(learnedCode);
    }
    return valid;
}
}

void mvc2NativeCacheReset()
{
    using namespace mvc2_native;
    if (initialized && portableRecording && portableTrainingDirty) {
        const bool saved = savePortableTraining();
        NOTICE_LOG(DYNAREC, "mvc2-portable-training: reset checkpoint saved=%u",
            saved);
    }
    for (auto& entry : stagedPortableBlocks) {
        entry.second.block->Discard();
        delete entry.second.block;
    }
    stagedPortableBlocks.clear();
    initialized = enabled = recording = merging = validating = reported = false;
    portableRecording = portableTrainingDirty = portablePrewarming = false;
    portableTrainingDiskExists = portableExitOnSaturation = false;
    portableTrainingDiskHeader = PortableHeader{};
    portableTrainingSaturated = 0;
    portableTrainingInvalidSkipped = 0;
    portableEnabled = false;
    portableCursor = 0;
    portableTrainingSourceLimit = 0;
    portableBudgetUs = portableInitialBudgetUs = portableMaxRenders = portablePolls = 0;
    portableCompiled = portableExisting = portableLastRenderCount = 0;
    portableRenderBaseline = portableInitialWarmDone = false;
    portableRejected = portableStaged = portableStagedHits = portableStagedRejects = portableEpoch = 0;
    portableStagedProtectionRejects = portableStagedSourceRejects = 0;
    portableStagedFpuRejects = 0;
    memset(protectedPageVerified, 0, sizeof(protectedPageVerified));
    memset(portablePageEpoch, 0, sizeof(portablePageEpoch));
    std::vector<Record>().swap(loaded);
    std::vector<Record>().swap(learned);
    std::vector<Relocation>().swap(loadedRelocations);
    std::vector<Relocation>().swap(learnedRelocations);
    std::vector<u8>().swap(loadedCode);
    std::vector<u8>().swap(consumed);
    std::vector<u8>().swap(learnedCode);
    std::vector<PortableRecord>().swap(portableRecords);
    std::vector<u8>().swap(portableSources);
    std::vector<u8>().swap(portableDone);
    std::vector<PortableRecord>().swap(portableTrainingRecords);
    std::vector<u8>().swap(portableTrainingSources);
    blockIndex.clear();
    portableTrainingIndex.clear();
}
void mvc2NativeCacheInvalidatePage(u32 guestAddress)
{
    mvc2_native::protectedPageVerified[(guestAddress & RAM_MASK) / 4096] = false;
}
bool mvc2NativeCacheWantsRelocations()
{
    return mvc2_native::recording && !mvc2_native::validating;
}
void mvc2NativeCacheBegin()
{
    using namespace mvc2_native;
    if (initialized) {
        if ((enabled || recording || portableEnabled)
                && (configHash() != identity.configHash
                    || _vmem_native_mapping_generation() != mapGeneration))
            enabled = recording = portableEnabled = false;
        return;
    }
    initialized = true;
#ifdef VITA_JIT_RUN_PROFILE
    return; // That executable embeds per-RBI heap addresses in generated code.
#endif
    if (settings.content.gameId != "T1212N" || config::FullMMU || mmu_enabled()
            || !_nvmem_enabled() || RAM_SIZE != 16 * 1024 * 1024)
        return;
    const bool aotPack = cfgLoadBool("perf", "Mvc2AotPack", false);
    const bool portableLoad = cfgLoadBool("perf", "Mvc2PortableCache", false);
    portableRecording = cfgLoadBool("perf", "Mvc2PortableCacheRecord", false);
    recording = cfgLoadBool("perf", "Mvc2NativeCacheRecord", false);
    const bool nativeLoad = cfgLoadBool("perf", "Mvc2NativeCache", false);
    bool load = aotPack || nativeLoad;
    if (aotPack && recording) {
        WARN_LOG(DYNAREC, "mvc2-native: AOT pack is read-only; runtime recording disabled");
        recording = false;
    }
    merging = recording && load && cfgLoadBool("perf", "Mvc2NativeCacheMerge", false);
    if (!recording && !load && !portableLoad && !portableRecording) return;
    if ((recording || load)
            && memcmp(mvc2_native_build_id,
                "\x04\0\0\0\x14\0\0\0\x03\0\0\0GNU\0", 16)) {
        WARN_LOG(DYNAREC, "mvc2-native: missing linked build identity");
        recording = false;
        load = false;
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
    hits = misses = sourceRejects = usedRejects = duplicateSkips = 0;
    validated = validationFailures = saturated = 0;
    pageChecks = 0;
#ifdef VITA_FRAME_TRACE
    testResetAfter = cfgLoadInt("perf", "Mvc2NativeCacheResetAfter", 0);
#endif
    validateHits = cfgLoadBool("perf", "Mvc2NativeCacheValidate", false);
    started = sceKernelGetProcessTimeWide();
    portableBudgetUs = std::max(0, std::min(50000,
        cfgLoadInt("perf", "Mvc2PortableWarmBudgetUs", 0)));
    portableInitialBudgetUs = std::max(0, std::min(5000000,
        cfgLoadInt("perf", "Mvc2PortableInitialWarmBudgetUs", 0)));
    portableMaxRenders = std::max(0, std::min(120,
        cfgLoadInt("perf", "Mvc2PortableWarmMaxRenders", 0)));
    portableTrainingSourceLimit = std::max(1024,
        std::min((int)MaxPortableSourceBytes,
            cfgLoadInt("perf", "Mvc2PortableTrainingSourceLimitBytes",
                       MaxPortableSourceBytes)));
    portableExitOnSaturation = cfgLoadBool(
        "perf", "Mvc2PortableExitOnSaturation", false);
    checkpointSeconds = std::max(5, std::min(120, cfgLoadInt("perf", "Mvc2NativeCacheSeconds", 30)));
    deadline = started + checkpointSeconds * 1000000ULL;
    path = get_writable_data_path(aotPack ? "mvc2_aot_pack.bin" : "mvc2_native_cache.bin");
    if (aotPack)
        NOTICE_LOG(DYNAREC, "mvc2-native: offline AOT pack selected path=%s", path.c_str());
    // Ordinary experiments keep cold training and replay separate. Explicit
    // beta merge mode reserves the existing layout, reuses its valid records,
    // and appends only runtime misses at later native offsets.
    if (merging) {
        enabled = readCache();
        if (!reserveLearnedCapacity(0, 0, true))
            recording = merging = false;
        if (recording)
            NOTICE_LOG(DYNAREC, "mvc2-native: incremental recording base=%u bytes=%u origin=%llu",
                (unsigned)loaded.size(), (unsigned)loadedCode.size(), (unsigned long long)started);
    } else if (recording) {
        if (!reserveLearnedCapacity(0, 0, true))
            recording = false;
        if (recording)
            NOTICE_LOG(DYNAREC, "mvc2-native: recording cold blocks, origin=%llu", (unsigned long long)started);
    } else if (load) {
        enabled = readCache();
    }
    if (portableLoad)
        portableEnabled = readPortableCoverage();
    if (portableRecording)
        portableRecording = beginPortableTraining();
}
void mvc2NativeCachePrewarmInitial()
{
    using namespace mvc2_native;
    if (portableInitialWarmDone)
        return;
    portableInitialWarmDone = true;
    if (!portableEnabled || !portableInitialBudgetUs)
        return;
    const u64 initialStart = sceKernelGetProcessTimeWide();
    const PortableWarmStats current =
        prewarmPortableCoverageBudget(portableInitialBudgetUs, false, true);
    const u64 currentElapsed = sceKernelGetProcessTimeWide() - initialStart;
    const u32 remainingUs = currentElapsed < portableInitialBudgetUs
        ? portableInitialBudgetUs - (u32)currentElapsed : 0;
    // A block without FPU opcodes is identical across FPSCR modes. Such blocks
    // can be emitted from coverage recorded in another mode without changing
    // the single-entry-per-PC block manager contract.
    const PortableWarmStats independent =
        prewarmPortableCoverageBudget(remainingUs, true, true);
    portableLastRenderCount = ta_getGuestRenderCount();
    portableRenderBaseline = true;
    NOTICE_LOG(DYNAREC, "mvc2-portable: initial-prewarm fpu=%08X current_scanned=%u current_matched=%u current_stored=%u current_compiled=%u independent_scanned=%u independent_matched=%u independent_stored=%u independent_compiled=%u staged=%u rejected=%u total=%u/%u elapsed_us=%llu",
        fpscr.full, current.scanned, current.matched, current.stored, current.compiled,
        independent.scanned, independent.matched, independent.stored, independent.compiled,
        independent.staged, current.rejected + independent.rejected,
        portableCompiled + portableExisting + portableStaged,
        (u32)portableRecords.size(),
        (unsigned long long)(sceKernelGetProcessTimeWide() - initialStart));
}
void mvc2NativeCacheRecord(RuntimeBlockInfo* block, int tRegister, int flagsCondition,
                           const mvc2_native::Relocation* relocations, u32 relocationCount)
{
    using namespace mvc2_native;
    if (validating) {
        validationTRegister = tRegister;
        validationFlagsCondition = flagsCondition;
        return;
    }
    recordPortableSource(block);
    if (!recording || saturated || block->temp_block
            || block->blockcheck_failures || excludedPc(block->vaddr))
        return;
    if (_vmem_native_mapping_generation() != mapGeneration) {
        recording = enabled = false;
        WARN_LOG(DYNAREC, "mvc2-native: memory mapping changed; recording rejected");
        return;
    }
    const size_t recordCount = loaded.size() + learned.size();
    const size_t nativeBytes = loadedCode.size() + learnedCode.size();
    const size_t totalRelocations = loadedRelocations.size() + learnedRelocations.size();
    if (recordCount >= MaxRecords || nativeBytes >= MaxNativeBytes
            || block->host_code_size > MaxNativeBytes - nativeBytes
            || relocationCount > MaxRelocations
            || totalRelocations > MaxTotalRelocations - std::min<u32>(relocationCount, MaxRelocations)) {
        if (!saturated) {
            WARN_LOG(DYNAREC, "mvc2-native: collector saturated records=%u/%u relocations=%u/%u bytes=%u/%u next=%u",
                (u32)recordCount, MaxRecords, (u32)totalRelocations, MaxTotalRelocations,
                (u32)nativeBytes, MaxNativeBytes, block->host_code_size);
        }
        saturated = 1;
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
    entry.relocationCount = relocationCount;
    u32 previousEnd = learned.empty()
        ? (loaded.empty() ? identity.prefixSize : loaded.back().offset + loaded.back().size)
        : learned.back().offset + learned.back().size;
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
    // Do not append a source/protection variant that this run has not consumed.
    // If an identical record was consumed and the block is compiled again after
    // invalidation, retain another instance because native addresses cannot be
    // safely reused within the same process.
    const auto range = blockIndex.equal_range(key(entry.guestPc, entry.fpuMode));
    for (auto it = range.first; it != range.second; ++it) {
        const size_t n = it->second;
        if (!consumed[n] && sameSourceVariant(loaded[n], entry)) {
            ++duplicateSkips;
            return;
        }
    }
    const u8* code = (const u8*)block->code;
    for (u32 relocation = 0; relocation < entry.relocationCount; ++relocation)
        if (!validRelocation(entry, relocations[relocation], code, identity.codeBase)
                || targetsMovableCode(relocations[relocation], identity.codeBase,
                                      identity.prefixSize, CODE_SIZE)) {
            WARN_LOG(DYNAREC, "mvc2-native: invalid relocation pc=%08X offset=%u target=%08X; block skipped",
                entry.guestPc, relocations[relocation].offset, relocations[relocation].target);
            return;
        }
    if (!reserveLearnedCapacity(entry.size, entry.relocationCount, false))
        return;
    learned.push_back(entry);
    learnedRelocations.insert(learnedRelocations.end(), relocations,
                              relocations + entry.relocationCount);
    learnedCode.insert(learnedCode.end(), code, code + entry.size); // Before linking/fastmem rewrites.
}
void mvc2NativeCacheStagePortableBlock(RuntimeBlockInfo* block,
                                       const mvc2_native::PortableRecord& source)
{
    using namespace mvc2_native;
    stagedPortableBlocks.emplace(key(source.guestPc, source.fpuMode),
                                 StagedPortableBlock{block, source});
}
RuntimeBlockInfo* mvc2NativeCacheLookup(u32 guestPc, u32 fpuMode)
{
    using namespace mvc2_native;
    const auto stagedRange = stagedPortableBlocks.equal_range(key(guestPc, fpuMode));
    for (auto it = stagedRange.first; it != stagedRange.second; ) {
        auto candidate = it++;
        RuntimeBlockInfo* block = candidate->second.block;
        const bool protectionMatches = portableProtectionMatches(block);
        const bool sourceMatches = portableSourceMatchesNow(candidate->second.source);
        const bool fpuRejected = block->has_fpu_op && (sr.status & (1 << 15));
        if (!protectionMatches || !sourceMatches || fpuRejected) {
            portableStagedProtectionRejects += !protectionMatches;
            portableStagedSourceRejects += !sourceMatches;
            portableStagedFpuRejects += fpuRejected;
            // Code overlays and their page-protection class are transient.
            // Keep nonmatching variants for a later exact-source lookup rather
            // than destroying them on the first state observed at this PC.
            ++portableStagedRejects;
            continue;
        }
        bm_AttachBlockProtection(block);
        stagedPortableBlocks.erase(candidate);
        ++portableStagedHits;
        FT_MARK(Emu, NativeHit, guestPc, 1);
        return block;
    }
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
    if (!initialized)
        return;
    prewarmPortableCoverage();
    const bool portableFinalCheckpoint = portableRecording
        && portableTrainingSaturated;
    if ((reported && !recording && !portableRecording)
            || (!enabled && !recording && !portableRecording && !validationFailures)
            || (!portableFinalCheckpoint
                && sceKernelGetProcessTimeWide() < deadline))
        return;
    if (enabled || recording || validationFailures)
        NOTICE_LOG(DYNAREC, "mvc2-native: report hits=%u misses=%u source_reject=%u used_reject=%u duplicate_skip=%u saturated=%u validated=%u failures=%u page_checks=%u",
            hits, misses, sourceRejects, usedRejects, duplicateSkips,
            saturated, validated, validationFailures, pageChecks);
    bool continueCheckpointing = false;
    if (recording) {
        const bool identityValid = _vmem_native_mapping_generation() == mapGeneration
            && configHash() == identity.configHash;
        const bool saved = identityValid && saveCache(merging);
        continueCheckpointing = merging && saved && !saturated;
        if (!continueCheckpointing)
            recording = false;
    }
    if (portableRecording) {
        const bool saved = savePortableTraining();
        if (saved && !portableTrainingSaturated)
            continueCheckpointing = true;
        else {
            portableRecording = false;
            if (saved && portableTrainingSaturated
                    && portableExitOnSaturation) {
                NOTICE_LOG(DYNAREC, "mvc2-portable-training: collector complete; requesting Vita process exit records=%u source_bytes=%u",
                    (u32)portableTrainingRecords.size(),
                    (u32)portableTrainingSources.size());
                portableExitRequested.store(true, std::memory_order_release);
            }
        }
    }
    if (continueCheckpointing) {
        // Periodic atomic checkpoints bound data loss for long tester
        // sessions without publishing appended entries in this process.
        deadline = sceKernelGetProcessTimeWide() + checkpointSeconds * 1000000ULL;
        return;
    }
    reported = true;
}
bool mvc2NativeCacheTakeExitRequest()
{
    return mvc2_native::portableExitRequested.exchange(
        false, std::memory_order_acq_rel);
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
