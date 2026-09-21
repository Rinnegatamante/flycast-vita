#pragma once
#include "types.h"
#include "mvc2_native_format.h"
struct RuntimeBlockInfo;
#if defined(__vita__) && FEAT_SHREC == DYNAREC_JIT && HOST_CPU == CPU_ARM
enum class Mvc2PortableWarmResult : u32 {
    Compiled,
    Existing,
    NoSpace,
    DecodeFailed,
    Staged,
};
void mvc2NativeCacheBegin();
void mvc2NativeCacheReset();
void mvc2NativeCachePoll();
bool mvc2NativeCacheTakeExitRequest();
void mvc2NativeCachePrewarmInitial();
void mvc2NativeCacheInvalidatePage(u32 guestAddress);
bool mvc2NativeCacheWantsRelocations();
void mvc2NativeCacheRecord(RuntimeBlockInfo* block, int tRegister, int flagsCondition,
                           const mvc2_native::Relocation* relocations, u32 relocationCount);
RuntimeBlockInfo* mvc2NativeCacheLookup(u32 guestPc, u32 fpuMode);
bool rdv_ValidateNativeBlock(const mvc2_native::Record& record);
void mvc2NativeCacheStagePortableBlock(RuntimeBlockInfo* block,
                                       const mvc2_native::PortableRecord& source);
Mvc2PortableWarmResult rdv_PrewarmPortableBlock(
    const mvc2_native::PortableRecord& source, bool stageFpuBlock = false,
    const u8* storedSource = nullptr);
#else
inline void mvc2NativeCacheBegin() {}
inline void mvc2NativeCacheReset() {}
inline void mvc2NativeCachePoll() {}
inline bool mvc2NativeCacheTakeExitRequest() { return false; }
inline void mvc2NativeCachePrewarmInitial() {}
inline void mvc2NativeCacheInvalidatePage(u32) {}
inline bool mvc2NativeCacheWantsRelocations() { return false; }
inline void mvc2NativeCacheRecord(RuntimeBlockInfo*, int, int,
                                  const mvc2_native::Relocation*, u32) {}
inline RuntimeBlockInfo* mvc2NativeCacheLookup(u32, u32) { return nullptr; }
#endif
