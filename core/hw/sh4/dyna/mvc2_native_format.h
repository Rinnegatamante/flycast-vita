#pragma once
#include <cstdint>
#include <cstring>

// Same executable/ABI only. No C++ pointers, containers or object images on disk.
namespace mvc2_native {
constexpr uint32_t Version = 2;
constexpr uint32_t MaxRecords = 12000;
constexpr uint32_t MaxNativeBytes = 2 * 1024 * 1024;
constexpr uint32_t MaxBlockBytes = 16 * 1024;
constexpr uint32_t MaxPages = 4;
constexpr uint32_t HasFpuOp = 1u;
constexpr uint32_t HasJcond = 2u;
constexpr uint32_t NeedsSmcCheck = 4u;
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
    uint32_t reserved;
};
struct Record {
    uint32_t offset, size, guestPc, fpuMode, guestBytes, cycles;
    uint32_t guestOps, hostOps, endType, branchPc, nextPc, relinkOffset, flags;
    int32_t tRegister, flagsCondition;
    uint32_t pageCount;
    uint64_t pageHash[MaxPages];
};
static_assert(sizeof(Header) == 104, "cache header ABI");
static_assert(sizeof(Record) == 96, "cache record ABI");
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
            || (entry.flags & ~(HasFpuOp | HasJcond | NeedsSmcCheck))
            || entry.tRegister < -1 || entry.tRegister > 15
            || entry.flagsCondition < -1 || entry.flagsCondition > 14)
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
}
