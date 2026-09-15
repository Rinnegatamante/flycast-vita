// Host-side format checks and deterministic negative-test artifacts.
// g++ -std=c++11 -Icore -Icore/deps/xxHash scripts/test-mvc2-native-cache.cpp
//     core/deps/xxHash/xxhash.c -o build-vita-tools/test-mvc2-native-cache.exe
#include "hw/sh4/dyna/mvc2_native_format.h"
#include <xxhash.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <set>

using namespace mvc2_native;
static constexpr uint32_t Capacity = 10 * 1024 * 1024, RamMask = 0xffffff;

struct ParsedRecord {
    Record record{};
    size_t recordOffset = 0;
    size_t relocationOffset = 0;
    size_t codeOffset = 0;
};

static uint64_t hashHeader(Header header) {
    header.headerHash = 0;
    return XXH64(&header, sizeof(header), 0);
}

static uint64_t hashPortableHeader(PortableHeader header) {
    header.headerHash = 0;
    return XXH64(&header, sizeof(header), 0);
}

static void inspectPortable(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(PortableHeader))
        throw std::runtime_error("portable header truncated");
    PortableHeader header;
    memcpy(&header, data.data(), sizeof(header));
    const uint64_t expectedSize = sizeof(header)
        + uint64_t(header.count) * sizeof(PortableRecord);
    if (memcmp(header.magic, "MVC2SH41", 8)
            || header.version != PortableVersion
            || header.headerSize != sizeof(header)
            || header.recordSize != sizeof(PortableRecord)
            || header.count > MaxPortableRecords
            || header.gameIdentity != PortableGameIdentity
            || header.reserved[0] || header.reserved[1]
            || header.reserved[2] || header.reserved[3]
            || expectedSize != data.size()
            || header.headerHash != hashPortableHeader(header))
        throw std::runtime_error("portable header checksum/format");
    uint64_t digest = 0;
    uint32_t exact = 0, writable = 0, duplicateVariants = 0;
    std::vector<PortableRecord> records;
    records.reserve(header.count);
    size_t offset = sizeof(header);
    for (uint32_t index = 0; index < header.count; ++index) {
        PortableRecord record;
        memcpy(&record, data.data() + offset, sizeof(record));
        offset += sizeof(record);
        if (!validPortableRecord(record, RamMask))
            throw std::runtime_error("portable record bounds/metadata");
        digest = XXH64(&record, sizeof(record), digest);
        exact += (record.flags & PortableExactSourceHash) != 0;
        writable += (record.flags & PortableWritableSource) != 0;
        for (const PortableRecord& previous : records)
            if (samePortableSource(record, previous)) {
                ++duplicateVariants;
                break;
            }
        records.push_back(record);
    }
    if (digest != header.payloadHash)
        throw std::runtime_error("portable payload checksum");
    printf("portable_version=%u records=%u exact_hash=%u page_hash=%u writable=%u duplicate_variants=%u game_identity=%016llx\n",
        header.version, header.count, exact, header.count - exact, writable,
        duplicateVariants, (unsigned long long)header.gameIdentity);
}

static void selfTest() {
    std::set<uint32_t> hashes;
    for (uint32_t n = 0; n < MaxRecords; ++n) {
        uint64_t key = blockKey(0x8c010000 + 2 * n, 0x00040001);
        assert(uint32_t(key) == 0x00040001);
        hashes.insert(hashKey32(key));
    }
    assert(hashes.size() == MaxRecords);
    Record base{};
    base.offset = 4096; base.size = 128; base.guestPc = 0x8c010000;
    base.guestBytes = 16; base.cycles = 8; base.guestOps = 8;
    base.relinkOffset = 112; base.pageCount = 1;
    base.tRegister = base.flagsCondition = -1;
    assert(validRecord(base, 4096, 4096, Capacity, RamMask));
    for (unsigned fault = 0; fault < 14; ++fault) {
        Record bad = base;
        switch (fault) {
        case 0: bad.offset = 0; break;
        case 1: bad.offset++; break;
        case 2: bad.offset = Capacity; break;
        case 3: bad.size = 0; break;
        case 4: bad.size = MaxBlockBytes + 4; break;
        case 5: bad.relinkOffset = bad.size; break;
        case 6: bad.guestPc++; break;
        case 7: bad.guestPc = 0x8d010000; break;
        case 8: bad.guestPc = 0x8cfffffe; break;
        case 9: bad.cycles = 0; break;
        case 10: bad.pageCount = 2; break;
        case 11: bad.flags = 16; break;
        case 12: bad.flagsCondition = 15; break;
        case 13: bad.endType = 3; break;
        }
        assert(!validRecord(bad, 4096, 4096, Capacity, RamMask));
    }
    base.guestPc = 0x8c010ffe; base.pageCount = 2;
    assert(validRecord(base, 4096, 4096, Capacity, RamMask));
    base.flags = NeedsSmcCheck;
    assert(validRecord(base, 4096, 4096, Capacity, RamMask));
    assert(!validRecord(base, 4096, 4100, Capacity, RamMask));
    base.flags = FixedOffset;
    assert(validRecord(base, 4096, 4096, Capacity, RamMask));
    base.relocationCount = 1;
    assert(!validRecord(base, 4096, 4096, Capacity, RamMask));
    const uint32_t codeBase = 0x81000000;
    assert(!targetsMovableCode(Relocation{0, codeBase + 4092}, codeBase, 4096, Capacity));
    assert(targetsMovableCode(Relocation{0, codeBase + 4096}, codeBase, 4096, Capacity));
    assert(!targetsMovableCode(Relocation{0, codeBase + Capacity}, codeBase, 4096, Capacity));
    PortableRecord portable{};
    portable.guestPc = 0x8c010000;
    portable.fpuMode = 0x00040001;
    portable.guestBytes = 16;
    portable.pageCount = 1;
    portable.flags = PortableExactSourceHash;
    portable.sourceHash = 1;
    assert(validPortableRecord(portable, RamMask));
    portable.flags = 4;
    assert(!validPortableRecord(portable, RamMask));
    puts("native-cache format: v2/v3 boundary, overlap, RAM/page and enum tests passed");
}

int main(int argc, char** argv) {
    try {
        selfTest();
        if (argc == 1) return 0;
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("cannot open input");
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
        if (data.size() >= 8 && !memcmp(data.data(), "MVC2SH41", 8)) {
            if (argc != 2)
                throw std::runtime_error("portable mutation is not supported");
            inspectPortable(data);
            return 0;
        }
        const uint64_t maxFileBytes = sizeof(Header) + uint64_t(MaxRecords) * sizeof(Record)
            + uint64_t(MaxTotalRelocations) * sizeof(Relocation) + MaxNativeBytes;
        if (data.size() < sizeof(Header) || data.size() > maxFileBytes)
            throw std::runtime_error("file size");
        Header header;
        memcpy(&header, data.data(), sizeof(header));
        const bool legacy = header.version == LegacyVersion;
        if (memcmp(header.magic, "MVC2ARM1", 8)
                || (!legacy && header.version != Version)
                || header.headerSize != sizeof(Header)
                || header.recordSize != (legacy ? sizeof(LegacyRecord) : sizeof(Record))
                || header.count > MaxRecords || header.relocationCount > MaxTotalRelocations
                || (legacy && header.relocationCount)
                || header.extent < header.prefixSize || header.extent > Capacity
                || header.headerHash != hashHeader(header))
            throw std::runtime_error("header checksum/format");

        size_t offset = sizeof(Header);
        uint32_t end = header.prefixSize, nativeBytes = 0, fpuBlocks = 0;
        uint32_t smcBlocks = 0, smcPage12fBlocks = 0, fixedBlocks = 0;
        uint32_t relocationCount = 0;
        std::vector<Record> records;
        std::vector<ParsedRecord> parsed;
        records.reserve(header.count);
        parsed.reserve(header.count);
        uint64_t digest = 0;
        uint32_t dumpPc = 0;
        if (argc == 3 && std::string(argv[2]).compare(0, 5, "--pc=") == 0)
            dumpPc = std::stoul(argv[2] + 5, nullptr, 16);
        for (uint32_t n = 0; n < header.count; ++n) {
            ParsedRecord item;
            item.recordOffset = offset;
            Record entry{};
            if (legacy) {
                if (offset + sizeof(LegacyRecord) > data.size())
                    throw std::runtime_error("truncated legacy record");
                LegacyRecord oldEntry;
                memcpy(&oldEntry, data.data() + offset, sizeof(oldEntry));
                digest = XXH64(&oldEntry, sizeof(oldEntry), digest);
                offset += sizeof(oldEntry);
                entry = upgradeLegacyRecord(oldEntry);
            } else {
                if (offset + sizeof(Record) > data.size())
                    throw std::runtime_error("truncated record");
                memcpy(&entry, data.data() + offset, sizeof(entry));
                digest = XXH64(&entry, sizeof(entry), digest);
                offset += sizeof(entry);
            }
            if (!validRecord(entry, header.prefixSize, end, Capacity, RamMask))
                throw std::runtime_error("record bounds/metadata");
            item.relocationOffset = offset;
            const size_t relocationBytes = size_t(entry.relocationCount) * sizeof(Relocation);
            if (relocationBytes > data.size() - offset)
                throw std::runtime_error("truncated relocations");
            if (relocationBytes)
                digest = XXH64(data.data() + offset, relocationBytes, digest);
            offset += relocationBytes;
            item.codeOffset = offset;
            if (entry.size > data.size() - offset)
                throw std::runtime_error("truncated native bytes");
            digest = XXH64(data.data() + offset, entry.size, digest);
            for (uint32_t relocation = 0; relocation < entry.relocationCount; ++relocation) {
                Relocation value;
                memcpy(&value, data.data() + item.relocationOffset
                    + relocation * sizeof(Relocation), sizeof(value));
                if (!validRelocation(entry, value, data.data() + offset, header.codeBase)
                        || targetsMovableCode(value, header.codeBase,
                                              header.prefixSize, Capacity))
                    throw std::runtime_error("invalid relocation");
            }
            offset += entry.size;
            end = entry.offset + entry.size;
            if (entry.size > MaxNativeBytes - nativeBytes)
                throw std::runtime_error("native byte limit");
            nativeBytes += entry.size;
            relocationCount += entry.relocationCount;
            fpuBlocks += (entry.flags & HasFpuOp) != 0;
            fixedBlocks += (entry.flags & FixedOffset) != 0;
            if (entry.flags & NeedsSmcCheck) {
                ++smcBlocks;
                if (((entry.guestPc & RamMask) & ~0xfffu) == 0x12f000u)
                    ++smcPage12fBlocks;
            }
            item.record = entry;
            parsed.push_back(item);
            records.push_back(entry);
            if (dumpPc && entry.guestPc == dumpPc)
                printf("record[%u] offset=%u size=%u guest_bytes=%u fpu=%08x flags=%u cycles=%u guest_ops=%u host_ops=%u end=%u branch=%08x next=%08x relink=%u hash=%016llx relocations=%u\n",
                    n, entry.offset, entry.size, entry.guestBytes, entry.fpuMode,
                    entry.flags, entry.cycles, entry.guestOps, entry.hostOps,
                    entry.endType, entry.branchPc, entry.nextPc, entry.relinkOffset,
                    (unsigned long long)entry.pageHash[0], entry.relocationCount);
        }
        if (offset != data.size() || end != header.extent
                || relocationCount != header.relocationCount
                || digest != header.payloadHash)
            throw std::runtime_error("payload checksum/extent/trailing data");
        uint32_t duplicateVariants = 0;
        for (size_t n = 0; n < records.size(); ++n)
            for (size_t previous = 0; previous < n; ++previous)
                if (sameSourceVariant(records[n], records[previous])) {
                    ++duplicateVariants;
                    break;
                }
        printf("version=%u records=%u native_bytes=%u extent=%u relocations=%u fixed=%u fpu_blocks=%u smc_blocks=%u smc_page_12f=%u duplicate_variants=%u build_id=",
            header.version, header.count, nativeBytes, header.extent, relocationCount,
            fixedBlocks, fpuBlocks, smcBlocks, smcPage12fBlocks, duplicateVariants);
        for (auto byte : header.buildId) printf("%02x", byte);
        puts("");

        if (argc == 4) {
            const std::string mode = argv[2];
            if (mode == "--bad-build") {
                header.buildId[0] ^= 1;
            } else if (mode == "--truncate") {
                data.resize(data.size() - 32);
            } else if (mode == "--bad-page" || mode == "--bad-offset"
                    || mode == "--bad-native") {
                if (parsed.empty()) throw std::runtime_error("empty cache");
                for (size_t index = 0; index < parsed.size(); ++index) {
                    ParsedRecord& item = parsed[index];
                    if (mode == "--bad-page") item.record.pageHash[0] ^= 1;
                    if (mode == "--bad-offset" && index == 0) item.record.offset = 0;
                    if (legacy) {
                        LegacyRecord oldEntry = item.record;
                        oldEntry.flags &= ~FixedOffset;
                        memcpy(data.data() + item.recordOffset, &oldEntry, sizeof(oldEntry));
                    } else {
                        memcpy(data.data() + item.recordOffset, &item.record, sizeof(item.record));
                    }
                    if (mode == "--bad-native") data[item.codeOffset] ^= 1;
                }
                header.payloadHash = 0;
                for (const ParsedRecord& item : parsed) {
                    header.payloadHash = XXH64(data.data() + item.recordOffset,
                        legacy ? sizeof(LegacyRecord) : sizeof(Record), header.payloadHash);
                    const size_t relocationBytes =
                        size_t(item.record.relocationCount) * sizeof(Relocation);
                    if (relocationBytes)
                        header.payloadHash = XXH64(data.data() + item.relocationOffset,
                            relocationBytes, header.payloadHash);
                    header.payloadHash = XXH64(data.data() + item.codeOffset,
                        item.record.size, header.payloadHash);
                }
            } else {
                throw std::runtime_error("unknown mutation");
            }
            header.headerHash = hashHeader(header);
            memcpy(data.data(), &header, sizeof(header));
            std::ofstream output(argv[3], std::ios::binary);
            output.write(reinterpret_cast<const char*>(data.data()), data.size());
            if (!output) throw std::runtime_error("write failed");
            printf("wrote %s\n", argv[3]);
        }
    } catch (const std::exception& error) {
        fprintf(stderr, "native-cache: %s\n", error.what());
        return 1;
    }
}
