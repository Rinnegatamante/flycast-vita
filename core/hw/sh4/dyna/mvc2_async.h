#pragma once
#include "types.h"
struct RuntimeBlockInfo;
#if defined(__vita__) && HOST_CPU == CPU_ARM && FEAT_SHREC == DYNAREC_JIT
bool mvc2AsyncCompile(RuntimeBlockInfo* block);
void mvc2AsyncBind(RuntimeBlockInfo* block);
void mvc2AsyncStop(bool reset);
void mvc2AsyncReport(u64 origin);
bool mvc2AsyncWorker();
void* mvc2AsyncEmitPtr();
u32 mvc2AsyncEmitFree();
void mvc2AsyncEmitSkip(u32 bytes);
#else
inline bool mvc2AsyncCompile(RuntimeBlockInfo*) { return false; }
inline void mvc2AsyncBind(RuntimeBlockInfo*) {}
inline void mvc2AsyncStop(bool) {}
inline void mvc2AsyncReport(u64) {}
inline bool mvc2AsyncWorker() { return false; }
inline void* mvc2AsyncEmitPtr() { return nullptr; }
inline u32 mvc2AsyncEmitFree() { return 0; }
inline void mvc2AsyncEmitSkip(u32) {}
#endif
