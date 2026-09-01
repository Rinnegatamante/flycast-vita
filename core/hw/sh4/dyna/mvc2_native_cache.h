#pragma once
#include "types.h"
#include "mvc2_native_format.h"
struct RuntimeBlockInfo;
#if defined(__vita__) && FEAT_SHREC == DYNAREC_JIT && HOST_CPU == CPU_ARM
void mvc2NativeCacheBegin();
void mvc2NativeCacheReset();
void mvc2NativeCachePoll();
void mvc2NativeCacheInvalidatePage(u32 guestAddress);
void mvc2NativeCacheRecord(RuntimeBlockInfo* block, int tRegister, int flagsCondition);
RuntimeBlockInfo* mvc2NativeCacheLookup(u32 guestPc, u32 fpuMode);
bool rdv_ValidateNativeBlock(const mvc2_native::Record& record);
#else
inline void mvc2NativeCacheBegin() {}
inline void mvc2NativeCacheReset() {}
inline void mvc2NativeCachePoll() {}
inline void mvc2NativeCacheInvalidatePage(u32) {}
inline void mvc2NativeCacheRecord(RuntimeBlockInfo*, int, int) {}
inline RuntimeBlockInfo* mvc2NativeCacheLookup(u32, u32) { return nullptr; }
#endif
