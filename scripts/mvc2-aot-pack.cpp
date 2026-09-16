// Build either a same-executable native AOT pack or executable-independent SH4
// coverage from MVC2 Vita native-cache shards.
// Build with scripts/build-mvc2-aot-pack.ps1.
#include "hw/sh4/dyna/mvc2_native_format.h"
#include <xxhash.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace mvc2_native;

namespace {

struct Candidate {
    Record record{};
    std::vector<Relocation> relocations;
    std::vector<uint8_t> code;
};

struct Cache {
    Header header{};
    std::vector<Candidate> blocks;
};

struct PortableCandidate {
    PortableRecord record{};
    std::vector<uint8_t> source;
};

uint64_t hashHeader(Header header) {
    header.headerHash = 0;
    return XXH64(&header, sizeof(header), 0);
}

uint64_t hashPortableHeader(PortableHeader header) {
    header.headerHash = 0;
    return XXH64(&header, sizeof(header), 0);
}

template<typename T>
T readObject(const std::vector<uint8_t>& data, size_t& offset, const char* what) {
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        throw std::runtime_error(std::string("truncated ") + what);
    T value;
    memcpy(&value, data.data() + offset, sizeof(value));
    offset += sizeof(value);
    return value;
}

Cache readCache(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open input: " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
    const uint64_t maxFileBytes = sizeof(Header) + uint64_t(MaxRecords) * sizeof(Record)
        + uint64_t(MaxTotalRelocations) * sizeof(Relocation) + MaxNativeBytes;
    if (data.size() < sizeof(Header) || data.size() > maxFileBytes)
        throw std::runtime_error("invalid file size: " + path);

    Cache cache;
    size_t offset = 0;
    cache.header = readObject<Header>(data, offset, "header");
    const bool legacy = cache.header.version == LegacyVersion;
    if (memcmp(cache.header.magic, "MVC2ARM1", 8)
            || (!legacy && cache.header.version != Version)
            || cache.header.headerSize != sizeof(Header)
            || cache.header.recordSize != (legacy ? sizeof(LegacyRecord) : sizeof(Record))
            || cache.header.count > MaxRecords
            || cache.header.relocationCount > MaxTotalRelocations
            || (legacy && cache.header.relocationCount)
            || cache.header.extent < cache.header.prefixSize
            || cache.header.extent > MaxNativeBytes
            || cache.header.headerHash != hashHeader(cache.header))
        throw std::runtime_error("invalid header: " + path);

    uint32_t end = cache.header.prefixSize;
    uint32_t nativeBytes = 0;
    uint32_t relocationCount = 0;
    uint64_t digest = 0;
    cache.blocks.reserve(cache.header.count);
    for (uint32_t index = 0; index < cache.header.count; ++index) {
        Candidate candidate;
        if (legacy) {
            const LegacyRecord oldRecord = readObject<LegacyRecord>(data, offset, "legacy record");
            digest = XXH64(&oldRecord, sizeof(oldRecord), digest);
            candidate.record = upgradeLegacyRecord(oldRecord);
        } else {
            candidate.record = readObject<Record>(data, offset, "record");
            digest = XXH64(&candidate.record, sizeof(candidate.record), digest);
        }
        const Record& record = candidate.record;
        if (!validRecord(record, cache.header.prefixSize, end, MaxNativeBytes, 0xffffff)
                || record.size > MaxNativeBytes - nativeBytes
                || record.relocationCount > MaxTotalRelocations - relocationCount)
            throw std::runtime_error("invalid record metadata: " + path);

        candidate.relocations.resize(record.relocationCount);
        const size_t relocationBytes = size_t(record.relocationCount) * sizeof(Relocation);
        if (relocationBytes > data.size() - offset)
            throw std::runtime_error("truncated relocations: " + path);
        if (relocationBytes) {
            memcpy(candidate.relocations.data(), data.data() + offset, relocationBytes);
            digest = XXH64(data.data() + offset, relocationBytes, digest);
            offset += relocationBytes;
        }

        if (record.size > data.size() - offset)
            throw std::runtime_error("truncated native code: " + path);
        candidate.code.assign(data.begin() + offset, data.begin() + offset + record.size);
        digest = XXH64(data.data() + offset, record.size, digest);
        offset += record.size;
        for (const Relocation& relocation : candidate.relocations)
            if (!validRelocation(record, relocation, candidate.code.data(), cache.header.codeBase)
                    || targetsMovableCode(relocation, cache.header.codeBase,
                                          cache.header.prefixSize, MaxNativeBytes))
                throw std::runtime_error("invalid native relocation: " + path);

        nativeBytes += record.size;
        relocationCount += record.relocationCount;
        end = record.offset + record.size;
        cache.blocks.push_back(candidate);
    }
    if (offset != data.size() || end != cache.header.extent
            || relocationCount != cache.header.relocationCount
            || digest != cache.header.payloadHash)
        throw std::runtime_error("payload checksum/extent mismatch: " + path);
    return cache;
}

std::vector<PortableCandidate> readPortableFile(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open input: " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
    const uint64_t maxFileBytes = sizeof(PortableHeader)
        + uint64_t(MaxPortableRecords) * sizeof(PortableRecord)
        + MaxPortableSourceBytes;
    if (data.size() < sizeof(PortableHeader) || data.size() > maxFileBytes)
        throw std::runtime_error("invalid portable file size: " + path);
    size_t offset = 0;
    const PortableHeader header = readObject<PortableHeader>(data, offset,
                                                              "portable header");
    const bool legacy = header.version == LegacyPortableVersion;
    const uint32_t sourceBytes = legacy ? 0 : header.reserved[0];
    if (memcmp(header.magic, "MVC2SH41", 8)
            || (!legacy && header.version != PortableVersion)
            || header.headerSize != sizeof(header)
            || header.recordSize != (legacy ? sizeof(LegacyPortableRecord)
                                            : sizeof(PortableRecord))
            || header.count > MaxPortableRecords
            || sourceBytes > MaxPortableSourceBytes
            || header.gameIdentity != PortableGameIdentity
            || (legacy && header.reserved[0])
            || header.reserved[1] || header.reserved[2] || header.reserved[3]
            || header.headerHash != hashPortableHeader(header))
        throw std::runtime_error("invalid portable header: " + path);
    std::vector<PortableRecord> records;
    records.reserve(header.count);
    uint64_t digest = 0;
    for (uint32_t index = 0; index < header.count; ++index) {
        PortableRecord record{};
        if (legacy) {
            const LegacyPortableRecord oldRecord =
                readObject<LegacyPortableRecord>(data, offset, "portable v1 record");
            if (!validLegacyPortableRecord(oldRecord, 0xffffff))
                throw std::runtime_error("invalid portable v1 record: " + path);
            digest = XXH64(&oldRecord, sizeof(oldRecord), digest);
            record = upgradeLegacyPortableRecord(oldRecord);
        } else {
            record = readObject<PortableRecord>(data, offset, "portable v2 record");
            if (!validPortableRecord(record, 0xffffff))
                throw std::runtime_error("invalid portable v2 record: " + path);
            digest = XXH64(&record, sizeof(record), digest);
        }
        if (hasPortableSource(record)
                && (record.sourceBytes > sourceBytes
                    || record.sourceOffset > sourceBytes - record.sourceBytes))
            throw std::runtime_error("portable source range outside payload: " + path);
        records.push_back(record);
    }
    if (sourceBytes > data.size() - offset || offset + sourceBytes != data.size())
        throw std::runtime_error("portable source payload size mismatch: " + path);
    if (sourceBytes)
        digest = XXH64(data.data() + offset, sourceBytes, digest);
    if (digest != header.payloadHash)
        throw std::runtime_error("portable payload checksum mismatch: " + path);
    std::vector<PortableCandidate> candidates;
    candidates.reserve(records.size());
    for (const PortableRecord& record : records) {
        PortableCandidate candidate;
        candidate.record = record;
        // Stored bytes are useful only for exact writable overlays. Page-hash
        // records are prewarmed from resident RAM and must not consume the
        // bounded source payload merely because an older collector included
        // redundant bytes.
        if (hasPortableSource(record)
                && (record.flags & (PortableExactSourceHash | PortableWritableSource))
                    == (PortableExactSourceHash | PortableWritableSource))
            candidate.source.assign(data.begin() + offset + record.sourceOffset,
                data.begin() + offset + record.sourceOffset + record.sourceBytes);
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

bool isPortableFile(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    char magic[8] = {};
    return input.read(magic, sizeof(magic))
        && !memcmp(magic, "MVC2SH41", sizeof(magic));
}

bool sameIdentity(const Header& left, const Header& right) {
    return left.codeBase == right.codeBase && left.contextBase == right.contextBase
        && left.prefixSize == right.prefixSize && left.prefixHash == right.prefixHash
        && left.layoutHash == right.layoutHash && left.configHash == right.configHash
        && memcmp(left.buildId, right.buildId, sizeof(left.buildId)) == 0;
}

bool sourceLess(const Candidate& left, const Candidate& right) {
    const Record& a = left.record;
    const Record& b = right.record;
#define CMP_FIELD(field) do { if (a.field != b.field) return a.field < b.field; } while (0)
    CMP_FIELD(guestPc);
    CMP_FIELD(fpuMode);
    CMP_FIELD(guestBytes);
    const uint32_t aFlags = a.flags & CodegenFlags;
    const uint32_t bFlags = b.flags & CodegenFlags;
    if (aFlags != bFlags) return aFlags < bFlags;
    CMP_FIELD(pageCount);
    for (uint32_t page = 0; page < MaxPages; ++page)
        CMP_FIELD(pageHash[page]);
#undef CMP_FIELD
    return false;
}

PortableRecord makePortableRecord(const Record& source) {
    PortableRecord output{};
    output.sourceOffset = NoPortableSource;
    output.guestPc = source.guestPc;
    output.fpuMode = source.fpuMode;
    output.guestBytes = source.guestBytes;
    output.flags = (source.flags & NeedsSmcCheck) ? PortableWritableSource : 0;
    output.pageCount = source.pageCount;
    memcpy(output.pageHash, source.pageHash, sizeof(output.pageHash));
    // Writable records already hash the exact block bytes. Read-only legacy
    // records hash complete backing pages, which is equally safe but coarser.
    if (source.flags & NeedsSmcCheck) {
        output.flags |= PortableExactSourceHash;
        output.sourceHash = source.pageHash[0];
    }
    return output;
}

bool portableLess(const PortableCandidate& left, const PortableCandidate& right) {
    const PortableRecord& a = left.record;
    const PortableRecord& b = right.record;
#define CMP_PORTABLE(field) do { if (a.field != b.field) return a.field < b.field; } while (0)
    CMP_PORTABLE(guestPc);
    CMP_PORTABLE(fpuMode);
    CMP_PORTABLE(guestBytes);
    CMP_PORTABLE(flags);
    CMP_PORTABLE(pageCount);
    CMP_PORTABLE(sourceHash);
    if (!(a.flags & PortableExactSourceHash))
        for (uint32_t page = 0; page < MaxPages; ++page)
            CMP_PORTABLE(pageHash[page]);
#undef CMP_PORTABLE
    return false;
}

std::vector<PortableCandidate> deduplicatePortable(
        std::vector<PortableCandidate> records, uint64_t& duplicates) {
    std::sort(records.begin(), records.end(), portableLess);
    std::vector<PortableCandidate> unique;
    unique.reserve(records.size());
    for (size_t begin = 0; begin < records.size();) {
        size_t end = begin + 1;
        while (end < records.size()
                && samePortableSource(records[begin].record, records[end].record))
            ++end;
        size_t selected = begin;
        for (size_t index = begin + 1; index < end; ++index) {
            if (!records[selected].source.empty() && !records[index].source.empty()
                    && records[selected].source != records[index].source)
                throw std::runtime_error("same portable source identity has different SH4 bytes");
            if (records[selected].source.empty() && !records[index].source.empty())
                selected = index;
        }
        unique.push_back(std::move(records[selected]));
        duplicates += end - begin - 1;
        begin = end;
    }
    return unique;
}

bool candidateLess(const Candidate& left, const Candidate& right) {
    if (sourceLess(left, right)) return true;
    if (sourceLess(right, left)) return false;
    const bool leftFixed = (left.record.flags & FixedOffset) != 0;
    const bool rightFixed = (right.record.flags & FixedOffset) != 0;
    if (leftFixed != rightFixed) return !leftFixed;
    if (left.record.offset != right.record.offset)
        return left.record.offset < right.record.offset;
    const bool sameRelocations = left.relocations.size() == right.relocations.size()
        && (left.relocations.empty() || memcmp(left.relocations.data(),
            right.relocations.data(), left.relocations.size() * sizeof(Relocation)) == 0);
    if (!sameRelocations)
        return std::lexicographical_compare(left.relocations.begin(), left.relocations.end(),
                                            right.relocations.begin(), right.relocations.end(),
            [](const Relocation& a, const Relocation& b) {
                return a.offset != b.offset ? a.offset < b.offset : a.target < b.target;
            });
    return std::lexicographical_compare(left.code.begin(), left.code.end(),
                                        right.code.begin(), right.code.end());
}

bool sameMetadata(const Record& left, const Record& right) {
    LegacyRecord a = left;
    LegacyRecord b = right;
    a.offset = b.offset = 0;
    a.flags &= CodegenFlags;
    b.flags &= CodegenFlags;
    return memcmp(&a, &b, sizeof(a)) == 0;
}

const char* rebaseError(RebaseResult result) {
    switch (result) {
    case RebaseResult::Ok: return "ok";
    case RebaseResult::FixedOffsetRecord: return "legacy fixed-offset record";
    case RebaseResult::InvalidRelocation: return "invalid relocation metadata";
    case RebaseResult::BranchOutOfRange: return "rebased branch out of A32 range";
    }
    return "unknown relocation error";
}

void rebase(Candidate& candidate, uint32_t newOffset, uint32_t codeBase) {
    const RebaseResult result = rebaseA32Block(candidate.code.data(), candidate.record,
        newOffset, candidate.relocations.data(), codeBase);
    if (result != RebaseResult::Ok)
        throw std::runtime_error(std::string("cannot rebase block: ") + rebaseError(result));
    candidate.record.offset = newOffset;
    for (const Relocation& relocation : candidate.relocations)
        if (!validRelocation(candidate.record, relocation, candidate.code.data(), codeBase))
            throw std::runtime_error("post-rebase relocation validation failed");
}

void verifyDuplicate(const Candidate& selected, const Candidate& other, uint32_t codeBase) {
    if (!sameMetadata(selected.record, other.record))
        throw std::runtime_error("same guest source produced different block metadata");
    const bool selectedFixed = (selected.record.flags & FixedOffset) != 0;
    const bool otherFixed = (other.record.flags & FixedOffset) != 0;
    if (selectedFixed) {
        if (!otherFixed || selected.record.offset != other.record.offset
                || selected.code != other.code)
            throw std::runtime_error("incompatible legacy fixed-offset cache lineages");
        return;
    }
    if (otherFixed) {
        Candidate translated = selected;
        rebase(translated, other.record.offset, codeBase);
        if (translated.code != other.code)
            throw std::runtime_error("legacy/native duplicate mismatch after relocation");
        return;
    }
    if (selected.relocations.size() != other.relocations.size())
        throw std::runtime_error("same guest source produced different relocation counts");
    for (size_t index = 0; index < selected.relocations.size(); ++index)
        if (selected.relocations[index].offset != other.relocations[index].offset
                || selected.relocations[index].target != other.relocations[index].target)
            throw std::runtime_error("same guest source produced different relocations");
    Candidate translated = other;
    rebase(translated, selected.record.offset, codeBase);
    if (selected.code != translated.code)
        throw std::runtime_error("same guest source produced different native code");
}

std::vector<Candidate> deduplicate(std::vector<Candidate> candidates, uint32_t codeBase,
                                   uint64_t& duplicates) {
    std::sort(candidates.begin(), candidates.end(), candidateLess);
    std::vector<Candidate> unique;
    unique.reserve(candidates.size());
    for (size_t begin = 0; begin < candidates.size();) {
        size_t end = begin + 1;
        while (end < candidates.size()
                && sameSourceVariant(candidates[begin].record, candidates[end].record))
            ++end;
        for (size_t index = begin + 1; index < end; ++index)
            verifyDuplicate(candidates[begin], candidates[index], codeBase);
        unique.push_back(std::move(candidates[begin]));
        duplicates += end - begin - 1;
        begin = end;
    }
    return unique;
}

void appendBytes(std::vector<uint8_t>& output, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    output.insert(output.end(), bytes, bytes + size);
}

void writePack(const std::string& outputPath, Header header,
               std::vector<Candidate> candidates) {
    std::vector<Candidate> fixed;
    std::vector<Candidate> movable;
    for (Candidate& candidate : candidates) {
        if (candidate.record.flags & FixedOffset)
            fixed.push_back(std::move(candidate));
        else
            movable.push_back(std::move(candidate));
    }
    std::sort(fixed.begin(), fixed.end(), [](const Candidate& a, const Candidate& b) {
        return a.record.offset < b.record.offset;
    });
    std::sort(movable.begin(), movable.end(), sourceLess);

    uint32_t next = header.prefixSize;
    for (const Candidate& candidate : fixed) {
        if (candidate.record.offset < next)
            throw std::runtime_error("overlapping legacy fixed-offset records");
        next = candidate.record.offset + candidate.record.size;
    }
    for (Candidate& candidate : movable) {
        rebase(candidate, next, header.codeBase);
        next += candidate.record.size;
        if (next > MaxNativeBytes)
            throw std::runtime_error("AOT pack exceeds the 10 MiB code cache");
    }
    fixed.insert(fixed.end(), movable.begin(), movable.end());
    if (fixed.size() > MaxRecords)
        throw std::runtime_error("AOT pack exceeds the record limit");

    header.version = Version;
    header.headerSize = sizeof(Header);
    header.recordSize = sizeof(Record);
    header.count = fixed.size();
    header.extent = fixed.empty() ? header.prefixSize
                                  : fixed.back().record.offset + fixed.back().record.size;
    header.relocationCount = 0;
    header.payloadHash = 0;
    header.headerHash = 0;
    std::vector<uint8_t> payload;
    uint32_t previousEnd = header.prefixSize;
    uint32_t nativeBytes = 0;
    for (const Candidate& candidate : fixed) {
        const Record& record = candidate.record;
        if (!validRecord(record, header.prefixSize, previousEnd, MaxNativeBytes, 0xffffff))
            throw std::runtime_error("invalid output record");
        for (const Relocation& relocation : candidate.relocations)
            if (!validRelocation(record, relocation, candidate.code.data(), header.codeBase))
                throw std::runtime_error("invalid output relocation");
        appendBytes(payload, &record, sizeof(record));
        if (!candidate.relocations.empty())
            appendBytes(payload, candidate.relocations.data(),
                        candidate.relocations.size() * sizeof(Relocation));
        appendBytes(payload, candidate.code.data(), candidate.code.size());
        header.payloadHash = XXH64(&record, sizeof(record), header.payloadHash);
        if (!candidate.relocations.empty())
            header.payloadHash = XXH64(candidate.relocations.data(),
                candidate.relocations.size() * sizeof(Relocation), header.payloadHash);
        header.payloadHash = XXH64(candidate.code.data(), candidate.code.size(),
                                   header.payloadHash);
        header.relocationCount += record.relocationCount;
        nativeBytes += record.size;
        previousEnd = record.offset + record.size;
    }
    if (header.relocationCount > MaxTotalRelocations)
        throw std::runtime_error("AOT pack exceeds the relocation limit");
    header.headerHash = hashHeader(header);

    std::ifstream existing(outputPath.c_str(), std::ios::binary);
    if (existing.good())
        throw std::runtime_error("output already exists: " + outputPath);
    const std::string temporary = outputPath + ".tmp";
    std::ofstream output(temporary.c_str(), std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot create temporary output: " + temporary);
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    output.close();
    if (!output)
        throw std::runtime_error("failed writing AOT pack");
    if (std::rename(temporary.c_str(), outputPath.c_str()) != 0)
        throw std::runtime_error("cannot publish AOT pack");
    printf("aot_pack=%s records=%u relocations=%u native_bytes=%u extent=%u fixed_legacy=%u\n",
        outputPath.c_str(), header.count, header.relocationCount, nativeBytes,
        header.extent, uint32_t(fixed.size() - movable.size()));
}

void writeCoverage(const std::string& outputPath,
                   std::vector<PortableCandidate> records) {
    if (records.size() > MaxPortableRecords)
        throw std::runtime_error("portable coverage exceeds the record limit");
    PortableHeader header{};
    memcpy(header.magic, "MVC2SH41", 8);
    header.version = PortableVersion;
    header.headerSize = sizeof(header);
    header.recordSize = sizeof(PortableRecord);
    header.count = static_cast<uint32_t>(records.size());
    header.gameIdentity = PortableGameIdentity;
    std::vector<uint8_t> sources;
    for (PortableCandidate& candidate : records) {
        PortableRecord& record = candidate.record;
        if (candidate.source.empty()) {
            record.sourceOffset = NoPortableSource;
            record.sourceBytes = 0;
        } else {
            if (candidate.source.size() != record.guestBytes
                    || candidate.source.size()
                        > MaxPortableSourceBytes - sources.size())
                throw std::runtime_error("portable SH4 source exceeds bounds");
            record.sourceOffset = sources.size();
            record.sourceBytes = candidate.source.size();
            sources.insert(sources.end(), candidate.source.begin(), candidate.source.end());
        }
        if (!validPortableRecord(record, 0xffffff))
            throw std::runtime_error("invalid portable output record");
        header.payloadHash = XXH64(&record, sizeof(record), header.payloadHash);
    }
    if (!sources.empty())
        header.payloadHash = XXH64(sources.data(), sources.size(), header.payloadHash);
    header.reserved[0] = sources.size();
    header.headerHash = hashPortableHeader(header);

    std::ifstream existing(outputPath.c_str(), std::ios::binary);
    if (existing.good())
        throw std::runtime_error("output already exists: " + outputPath);
    const std::string temporary = outputPath + ".tmp";
    std::ofstream output(temporary.c_str(), std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot create temporary output: " + temporary);
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (const PortableCandidate& candidate : records)
        output.write(reinterpret_cast<const char*>(&candidate.record),
                     sizeof(candidate.record));
    if (!sources.empty())
        output.write(reinterpret_cast<const char*>(sources.data()), sources.size());
    output.close();
    if (!output)
        throw std::runtime_error("failed writing portable coverage");
    if (std::rename(temporary.c_str(), outputPath.c_str()) != 0)
        throw std::runtime_error("cannot publish portable coverage");
    printf("portable_coverage=%s records=%u bytes=%llu game_identity=%016llx\n",
        outputPath.c_str(), header.count,
        (unsigned long long)(sizeof(header) + records.size() * sizeof(PortableRecord)
            + sources.size()),
        (unsigned long long)header.gameIdentity);
}

uint32_t encodeBranch(uint32_t instructionAddress, uint32_t target, uint32_t opcode) {
    const int64_t displacement = int64_t(target) - (int64_t(instructionAddress) + 8);
    assert((displacement & 3) == 0);
    assert(displacement >= -33554432ll && displacement <= 33554428ll);
    return opcode | (uint32_t(displacement / 4) & 0x00ffffffu);
}

void selfTest() {
    const uint32_t base = 0x81000000;
    Record record{};
    record.offset = 0x100;
    record.size = 12;
    record.relocationCount = 1;
    Relocation relocation{0, base + 0x40};
    std::vector<uint8_t> code(record.size);
    uint32_t external = encodeBranch(base + record.offset, relocation.target, 0xeb000000);
    uint32_t internal = encodeBranch(base + record.offset + 4,
                                     base + record.offset + 8, 0x1a000000);
    const uint32_t literalThatLooksLikeBranch = 0xea123456;
    memcpy(code.data(), &external, 4);
    memcpy(code.data() + 4, &internal, 4);
    memcpy(code.data() + 8, &literalThatLooksLikeBranch, 4);
    assert(validRelocation(record, relocation, code.data(), base));
    assert(!targetsMovableCode(relocation, base, 0x80, MaxNativeBytes));
    assert(targetsMovableCode(Relocation{0, base + 0x80}, base, 0x80, MaxNativeBytes));
    assert(rebaseA32Block(code.data(), record, 0x400, &relocation, base)
        == RebaseResult::Ok);
    uint32_t instruction, target;
    memcpy(&instruction, code.data(), 4);
    assert(decodeA32BranchTarget(instruction, base + 0x400, target)
        && target == relocation.target);
    memcpy(&instruction, code.data() + 4, 4);
    assert(decodeA32BranchTarget(instruction, base + 0x404, target)
        && target == base + 0x408);
    memcpy(&instruction, code.data() + 8, 4);
    assert(instruction == literalThatLooksLikeBranch);
    record.flags = FixedOffset;
    record.relocationCount = 0;
    assert(rebaseA32Block(code.data(), record, 0x500, nullptr, base)
        == RebaseResult::FixedOffsetRecord);
    Record native{};
    native.guestPc = 0x8c010000;
    native.fpuMode = 0x00040001;
    native.guestBytes = 16;
    native.pageCount = 1;
    native.flags = NeedsSmcCheck;
    native.pageHash[0] = UINT64_C(0x123456789abcdef0);
    const PortableRecord portable = makePortableRecord(native);
    assert(validPortableRecord(portable, 0xffffff));
    assert((portable.flags & PortableExactSourceHash) != 0);
    assert(portable.sourceHash == native.pageHash[0]);
    puts("mvc2-aot-pack self-test: explicit external branch relocation passed");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            selfTest();
            return 0;
        }
        const bool nativeOutput = argc >= 2 && std::string(argv[1]) == "--output";
        const bool portableOutput = argc >= 2
            && std::string(argv[1]) == "--coverage-output";
        if (argc < 4 || (!nativeOutput && !portableOutput)) {
            fprintf(stderr, "usage: %s --output <mvc2_aot_pack.bin> <cache.bin> [cache2.bin ...]\n",
                    argv[0]);
            fprintf(stderr, "       %s --coverage-output <mvc2_portable_cache.bin> <native-or-portable.bin> [...]\n",
                    argv[0]);
            fprintf(stderr, "       %s --self-test\n", argv[0]);
            return 2;
        }
        const std::string output = argv[2];
        std::vector<Candidate> candidates;
        std::vector<PortableCandidate> portable;
        Header identity{};
        uint64_t inputRecords = 0;
        for (int argument = 3; argument < argc; ++argument) {
            if (portableOutput && isPortableFile(argv[argument])) {
                std::vector<PortableCandidate> input = readPortableFile(argv[argument]);
                inputRecords += input.size();
                portable.insert(portable.end(),
                    std::make_move_iterator(input.begin()),
                    std::make_move_iterator(input.end()));
                continue;
            }
            Cache cache = readCache(argv[argument]);
            inputRecords += cache.blocks.size();
            if (nativeOutput) {
                if (argument == 3)
                    identity = cache.header;
                else if (!sameIdentity(identity, cache.header))
                    throw std::runtime_error(std::string("incompatible cache identity: ") + argv[argument]);
                candidates.insert(candidates.end(),
                    std::make_move_iterator(cache.blocks.begin()),
                    std::make_move_iterator(cache.blocks.end()));
            } else {
                portable.reserve(portable.size() + cache.blocks.size());
                for (const Candidate& candidate : cache.blocks) {
                    PortableCandidate converted;
                    converted.record = makePortableRecord(candidate.record);
                    portable.push_back(std::move(converted));
                }
            }
        }
        uint64_t duplicates = 0;
        if (nativeOutput) {
            candidates = deduplicate(std::move(candidates), identity.codeBase, duplicates);
            printf("inputs=%u input_records=%llu unique_records=%u duplicates=%llu\n",
                uint32_t(argc - 3), (unsigned long long)inputRecords,
                uint32_t(candidates.size()), (unsigned long long)duplicates);
            writePack(output, identity, std::move(candidates));
        } else {
            portable = deduplicatePortable(std::move(portable), duplicates);
            printf("inputs=%u input_records=%llu portable_records=%u duplicates=%llu\n",
                uint32_t(argc - 3), (unsigned long long)inputRecords,
                uint32_t(portable.size()), (unsigned long long)duplicates);
            writeCoverage(output, portable);
        }
    } catch (const std::exception& error) {
        fprintf(stderr, "mvc2-aot-pack: %s\n", error.what());
        return 1;
    }
    return 0;
}
