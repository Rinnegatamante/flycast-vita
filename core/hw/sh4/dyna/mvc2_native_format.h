#pragma once
#include <cstdint>
#include <cstring>

// The native format is same-executable/ABI only. The portable format below
// stores guest SH4 identity only and is deliberately independent of Flycast.
// Neither format serializes C++ pointers, containers or object images.
namespace mvc2_native {
constexpr uint32_t LegacyVersion = 2;
constexpr uint32_t Version = 3;
constexpr uint32_t MaxRecords = 100000;
constexpr uint32_t MaxNativeBytes = 10 * 1024 * 1024;
constexpr uint32_t MaxBlockBytes = 16 * 1024;
constexpr uint32_t MaxPages = 4;
constexpr uint32_t MaxRelocations = 1024;
constexpr uint32_t MaxTotalRelocations = 1000000;
constexpr uint32_t PortableVersion = 1;
constexpr uint32_t MaxPortableRecords = 100000;
// Little-endian bytes are "MVC2US1\1". This format is intentionally tied to
// Marvel vs. Capcom 2 US v1.001, but not to a Flycast executable or backend.
constexpr uint64_t PortableGameIdentity = UINT64_C(0x013153553243564d);
constexpr uint32_t HasFpuOp = 1u;
constexpr uint32_t HasJcond = 2u;
constexpr uint32_t NeedsSmcCheck = 4u;
// Version-2 records did not describe their PC-relative native branches. They
// remain replayable at their original offset, but an offline packer must not
// move them.
constexpr uint32_t FixedOffset = 8u;
constexpr uint32_t CodegenFlags = HasFpuOp | HasJcond | NeedsSmcCheck;
inline uint64_t blockKey(uint32_t pc, uint32_t fpuMode) {
    return uint64_t(pc) << 32 | fpuMode;
}
inline uint32_t hashKey32(uint64_t key) {
    // std::hash<uint64_t> truncates to size_t on Vita: the high PC half
    // would disappear and every block with the same FPSCR would collide.
    return uint32_t(key >> 32) ^ uint32_t(key);
}
struct Header {
    char magic[8];
    uint32_t version, headerSize, recordSize, count;
    uint32_t codeBase, prefixSize, extent, contextBase;
    uint64_t layoutHash, configHash, prefixHash, payloadHash, headerHash;
    uint8_t buildId[20];
    uint32_t relocationCount;
};
struct LegacyRecord {
    uint32_t offset, size, guestPc, fpuMode, guestBytes, cycles;
    uint32_t guestOps, hostOps, endType, branchPc, nextPc, relinkOffset, flags;
    int32_t tRegister, flagsCondition;
    uint32_t pageCount;
    uint64_t pageHash[MaxPages];
};
struct Record : LegacyRecord {
    uint32_t relocationCount;
    uint32_t reserved;
};
struct Relocation {
    uint32_t offset;
    uint32_t target;
};
constexpr uint32_t PortableExactSourceHash = 1u;
constexpr uint32_t PortableWritableSource = 2u;
constexpr uint32_t PortableFlags = PortableExactSourceHash | PortableWritableSource;
struct PortableHeader {
    char magic[8];
    uint32_t version, headerSize, recordSize, count;
    uint64_t gameIdentity, payloadHash, headerHash;
    uint32_t reserved[4];
};
struct PortableRecord {
    uint32_t guestPc, fpuMode, guestBytes, flags;
    uint32_t pageCount, reserved;
    uint64_t sourceHash;
    uint64_t pageHash[MaxPages];
};
static_assert(sizeof(Header) == 104, "cache header ABI");
static_assert(sizeof(LegacyRecord) == 96, "legacy cache record ABI");
static_assert(sizeof(Record) == 104, "cache record ABI");
static_assert(sizeof(Relocation) == 8, "cache relocation ABI");
static_assert(sizeof(PortableHeader) == 64, "portable header ABI");
static_assert(sizeof(PortableRecord) == 64, "portable record ABI");
inline Record upgradeLegacyRecord(const LegacyRecord& legacy) {
    Record record{};
    memcpy(static_cast<LegacyRecord*>(&record), &legacy, sizeof(legacy));
    record.flags |= FixedOffset;
    return record;
}
inline bool sameSourceVariant(const Record& left, const Record& right) {
    // Native offsets and emitted sizes can differ with allocator position.
    // Lookup safety is determined by the same guest identity, protection
    // class and exact source/page hashes used by sourceMatches().
    return left.guestPc == right.guestPc && left.fpuMode == right.fpuMode
        && left.guestBytes == right.guestBytes
        && (left.flags & CodegenFlags) == (right.flags & CodegenFlags)
        && left.pageCount == right.pageCount
        && memcmp(left.pageHash, right.pageHash, sizeof(left.pageHash)) == 0;
}
inline bool validPortableRecord(const PortableRecord& entry, uint32_t ramMask) {
    if ((entry.guestPc & 1) || (entry.guestPc & 0x1f000000) != 0x0c000000
            || !entry.guestBytes || (entry.guestBytes & 1) || entry.guestBytes > 8192
            || entry.guestBytes > ramMask + 1 - (entry.guestPc & ramMask)
            || (entry.flags & ~PortableFlags) || entry.reserved)
        return false;
    const uint32_t pages = ((entry.guestPc & 4095) + entry.guestBytes + 4095) / 4096;
    return pages == entry.pageCount && pages && pages <= MaxPages;
}
inline bool samePortableSource(const PortableRecord& left, const PortableRecord& right) {
    return left.guestPc == right.guestPc && left.fpuMode == right.fpuMode
        && left.guestBytes == right.guestBytes && left.flags == right.flags
        && left.pageCount == right.pageCount && left.sourceHash == right.sourceHash
        && memcmp(left.pageHash, right.pageHash, sizeof(left.pageHash)) == 0;
}
inline bool validRecord(const Record& entry, uint32_t prefix, uint32_t previousEnd,
                        uint32_t codeCapacity, uint32_t ramMask) {
    if (entry.offset < prefix || entry.offset < previousEnd || (entry.offset & 3)
            || entry.offset > codeCapacity - MaxBlockBytes
            || !entry.size || entry.size > MaxBlockBytes || (entry.size & 3)
            || entry.relinkOffset >= entry.size || (entry.relinkOffset & 3)
            || (entry.guestPc & 1) || (entry.guestPc & 0x1f000000) != 0x0c000000
            || !entry.guestBytes || (entry.guestBytes & 1) || entry.guestBytes > 8192
            || entry.guestBytes > ramMask + 1 - (entry.guestPc & ramMask)
            || !entry.cycles || entry.cycles > 1000000 || !entry.guestOps
            || entry.guestOps > 4096
            || (entry.flags & ~(CodegenFlags | FixedOffset))
            || entry.tRegister < -1 || entry.tRegister > 15
            || entry.flagsCondition < -1 || entry.flagsCondition > 14
            || entry.relocationCount > MaxRelocations || entry.reserved
            || ((entry.flags & FixedOffset) && entry.relocationCount))
        return false;
    const uint32_t pages = ((entry.guestPc & 4095) + entry.guestBytes + 4095) / 4096;
    if (pages != entry.pageCount || !pages || pages > MaxPages)
        return false;
    switch (entry.endType) {
    case 0: case 2: case 6: case 8: case 10: case 12: case 14: case 16: case 17:
        return true;
    default:
        return false;
    }
}
inline bool isA32BranchImmediate(uint32_t instruction) {
    // A32 B/BL. cond=0xf is BLX-immediate and has different H-bit semantics;
    // the Flycast ARM backend emits register BLX for far calls instead.
    return (instruction & 0x0e000000u) == 0x0a000000u
        && (instruction >> 28) != 0xf;
}
inline bool decodeA32BranchTarget(uint32_t instruction, uint32_t instructionAddress,
                                  uint32_t& target) {
    if (!isA32BranchImmediate(instruction))
        return false;
    const int32_t words = static_cast<int32_t>(instruction << 8) >> 8;
    const int64_t address = static_cast<int64_t>(instructionAddress) + 8
        + static_cast<int64_t>(words) * 4;
    if (address < 0 || address > 0xffffffffll)
        return false;
    target = static_cast<uint32_t>(address);
    return true;
}
inline bool validRelocation(const Record& record, const Relocation& relocation,
                            const uint8_t* nativeCode, uint32_t codeBase) {
    if ((relocation.offset & 3) || relocation.offset > record.size - 4)
        return false;
    uint32_t instruction;
    memcpy(&instruction, nativeCode + relocation.offset, sizeof(instruction));
    uint32_t decodedTarget;
    if (!decodeA32BranchTarget(instruction,
            codeBase + record.offset + relocation.offset, decodedTarget)
            || decodedTarget != relocation.target)
        return false;
    const uint64_t blockStart = static_cast<uint64_t>(codeBase) + record.offset;
    const uint64_t blockEnd = blockStart + record.size;
    return relocation.target < blockStart || relocation.target >= blockEnd;
}
inline bool targetsMovableCode(const Relocation& relocation, uint32_t codeBase,
                               uint32_t prefixSize, uint32_t codeCapacity) {
    const uint64_t target = relocation.target;
    const uint64_t bodyStart = static_cast<uint64_t>(codeBase) + prefixSize;
    const uint64_t bodyEnd = static_cast<uint64_t>(codeBase) + codeCapacity;
    return target >= bodyStart && target < bodyEnd;
}
enum class RebaseResult {
    Ok,
    FixedOffsetRecord,
    InvalidRelocation,
    BranchOutOfRange,
};
inline RebaseResult rebaseA32Block(uint8_t* nativeCode, const Record& record,
                                   uint32_t newOffset, const Relocation* relocations,
                                   uint32_t codeBase) {
    if (newOffset == record.offset)
        return RebaseResult::Ok;
    if (record.flags & FixedOffset)
        return RebaseResult::FixedOffsetRecord;
    for (uint32_t index = 0; index < record.relocationCount; ++index) {
        const Relocation& relocation = relocations[index];
        if (!validRelocation(record, relocation, nativeCode, codeBase))
            return RebaseResult::InvalidRelocation;
        const int64_t source = static_cast<int64_t>(codeBase) + newOffset
            + relocation.offset + 8;
        const int64_t displacement = static_cast<int64_t>(relocation.target) - source;
        if ((displacement & 3) || displacement < -33554432ll
                || displacement > 33554428ll)
            return RebaseResult::BranchOutOfRange;
        uint32_t instruction;
        memcpy(&instruction, nativeCode + relocation.offset, sizeof(instruction));
        const uint32_t immediate = static_cast<uint32_t>(displacement / 4) & 0x00ffffffu;
        instruction = (instruction & 0xff000000u) | immediate;
        memcpy(nativeCode + relocation.offset, &instruction, sizeof(instruction));
    }
    return RebaseResult::Ok;
}
}
