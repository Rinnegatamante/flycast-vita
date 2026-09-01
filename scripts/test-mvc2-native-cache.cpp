// Host-side format checks and deterministic negative-test artifacts.
// g++ -std=c++11 -Icore -Icore/deps/xxHash scripts/test-mvc2-native-cache.cpp
//     core/deps/xxHash/xxhash.c -o build-vita/test-mvc2-native-cache.exe
#include "hw/sh4/dyna/mvc2_native_format.h"
#include <xxhash.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <set>
using namespace mvc2_native;
static constexpr uint32_t Capacity = 10 * 1024 * 1024, RamMask = 0xffffff;
static uint64_t hashHeader(Header header) {
    header.headerHash = 0;
    return XXH64(&header, sizeof(header), 0);
}
static void selfTest() {
    std::set<uint32_t> hashes;
    for (uint32_t n = 0; n < MaxRecords; ++n) {
        uint64_t key = blockKey(0x8c010000 + 2 * n, 0x00040001);
        assert(uint32_t(key) == 0x00040001); // Regression: old 32-bit std::hash.
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
        case 11: bad.flags = 8; break;
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
    puts("native-cache format: boundary, overlap, RAM/page and enum tests passed");
}
int main(int argc, char** argv) {
    try {
        selfTest();
        if (argc == 1) return 0;
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("cannot open input");
        std::vector<char> data((std::istreambuf_iterator<char>(input)), {});
        if (data.size() < sizeof(Header) || data.size() > 12 * 1024 * 1024)
            throw std::runtime_error("file size");
        Header header;
        memcpy(&header, data.data(), sizeof(header));
        if (memcmp(header.magic, "MVC2ARM1", 8) || header.version != Version
                || header.headerSize != sizeof(Header) || header.recordSize != sizeof(Record)
                || header.count > MaxRecords || header.extent < header.prefixSize
                || header.extent > Capacity || header.headerHash != hashHeader(header))
            throw std::runtime_error("header checksum/format");
        size_t offset = sizeof(Header);
        uint32_t end = header.prefixSize, nativeBytes = 0, fpuBlocks = 0;
        uint32_t smcBlocks = 0, smcPage12fBlocks = 0;
        std::vector<Record> records;
        records.reserve(header.count);
        uint64_t digest = 0;
        uint32_t dumpPc = 0;
        if (argc == 3 && std::string(argv[2]).compare(0, 5, "--pc=") == 0)
            dumpPc = std::stoul(argv[2] + 5, nullptr, 16);
        for (uint32_t n = 0; n < header.count; ++n) {
            if (offset + sizeof(Record) > data.size()) throw std::runtime_error("truncated record");
            Record entry;
            memcpy(&entry, data.data() + offset, sizeof(entry));
            if (!validRecord(entry, header.prefixSize, end, Capacity, RamMask))
                throw std::runtime_error("record bounds/metadata");
            digest = XXH64(&entry, sizeof(entry), digest);
            offset += sizeof(entry);
            if (offset + entry.size > data.size()) throw std::runtime_error("truncated native bytes");
            digest = XXH64(data.data() + offset, entry.size, digest);
            offset += entry.size;
            end = entry.offset + entry.size;
            if (entry.size > MaxNativeBytes - nativeBytes)
                throw std::runtime_error("native byte limit");
            nativeBytes += entry.size;
            fpuBlocks += (entry.flags & HasFpuOp) != 0;
            if (entry.flags & NeedsSmcCheck) {
                smcBlocks++;
                if (((entry.guestPc & RamMask) & ~0xfffu) == 0x12f000u)
                    smcPage12fBlocks++;
            }
            records.push_back(entry);
            if (dumpPc && entry.guestPc == dumpPc) {
                printf("record[%u] offset=%u size=%u guest_bytes=%u fpu=%08x flags=%u cycles=%u guest_ops=%u host_ops=%u end=%u branch=%08x next=%08x relink=%u hash=%016llx native=",
                       n, entry.offset, entry.size, entry.guestBytes, entry.fpuMode,
                       entry.flags, entry.cycles, entry.guestOps, entry.hostOps,
                       entry.endType, entry.branchPc, entry.nextPc, entry.relinkOffset,
                       (unsigned long long)entry.pageHash[0]);
                const size_t shown = entry.size < 32 ? entry.size : 32;
                for (size_t byte = 0; byte < shown; ++byte)
                    printf("%02x", (unsigned char)data[offset - entry.size + byte]);
                puts("");
            }
        }
        if (offset != data.size() || end != header.extent || digest != header.payloadHash)
            throw std::runtime_error("payload checksum/extent/trailing data");
        uint32_t duplicateVariants = 0;
        for (size_t n = 0; n < records.size(); ++n) {
            for (size_t previous = 0; previous < n; ++previous) {
                if (sameSourceVariant(records[n], records[previous])) {
                    ++duplicateVariants;
                    break;
                }
            }
        }
        printf("records=%u native_bytes=%u extent=%u fpu_blocks=%u smc_blocks=%u smc_page_12f=%u duplicate_variants=%u build_id=",
               header.count, nativeBytes, header.extent, fpuBlocks, smcBlocks,
               smcPage12fBlocks, duplicateVariants);
        for (auto byte : header.buildId) printf("%02x", byte);
        puts("");
        if (argc == 4) {
            const std::string mode = argv[2];
            if (mode == "--bad-build") header.buildId[0] ^= 1;
            else if (mode == "--truncate") data.resize(data.size() - 32);
            else if (mode == "--bad-page" || mode == "--bad-offset" || mode == "--bad-native") {
                if (!header.count) throw std::runtime_error("empty cache");
                Record entry;
                offset = sizeof(Header);
                for (uint32_t n = 0; n < header.count; ++n) {
                    memcpy(&entry, data.data() + offset, sizeof(entry));
                    if (mode == "--bad-page") entry.pageHash[0] ^= 1;
                    if (mode == "--bad-offset" && n == 0) entry.offset = 0;
                    memcpy(data.data() + offset, &entry, sizeof(entry));
                    offset += sizeof(entry);
                    if (mode == "--bad-native") data[offset] ^= 1;
                    offset += entry.size;
                }
                // Recalculate the checksum so identity/source/validation guards,
                // rather than the transport checksum alone, are exercised.
                header.payloadHash = 0;
                offset = sizeof(Header);
                for (uint32_t n = 0; n < header.count; ++n) {
                    memcpy(&entry, data.data() + offset, sizeof(entry));
                    header.payloadHash = XXH64(&entry, sizeof(entry), header.payloadHash);
                    offset += sizeof(entry);
                    header.payloadHash = XXH64(data.data() + offset, entry.size, header.payloadHash);
                    offset += entry.size;
                }
            } else throw std::runtime_error("unknown mutation");
            header.headerHash = hashHeader(header);
            memcpy(data.data(), &header, sizeof(header));
            std::ofstream output(argv[3], std::ios::binary);
            output.write(data.data(), data.size());
            if (!output) throw std::runtime_error("write failed");
            printf("wrote %s\n", argv[3]);
        }
    } catch (const std::exception& error) {
        fprintf(stderr, "native-cache: %s\n", error.what());
        return 1;
    }
}
