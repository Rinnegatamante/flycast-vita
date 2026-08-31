#pragma once
#include <cstdint>
#include <cstring>

namespace mvc2 {

// The entry debit has already happened. Never absorb the next scheduler call.
inline uint32_t copyLoopBatch(uint32_t count, int remaining, int blockCycles)
{
	if (blockCycles != 5 || remaining <= 0 || remaining > 448)
		return 1;
	const uint32_t capacity = 1 + (remaining - 1) / 5;
	return count != 0 && count < capacity ? count : capacity;
}

// Each 8-byte read completes before its write. In particular, forward overlap
// must propagate data just as repeated FMOV does; whole-range memcpy/memmove
// would not implement that contract. Return the final raw DR0 memory bits.
inline uint64_t copyLoopBytes(uint8_t* destination, const uint8_t* source, uint32_t iterations)
{
	// Callers prove eight-byte alignment. Volatile maintains ordered eight-byte
	// accesses to the lockable VRAM mapping; may_alias permits raw guest bytes.
	using Alias64 = uint64_t __attribute__((may_alias));
	auto* out = reinterpret_cast<volatile Alias64*>(destination);
	auto* in = reinterpret_cast<const volatile Alias64*>(source);
	uint64_t last;
	do {
		last = *in++;
		*out++ = last;
	} while (--iterations != 0);
	return last;
}

} // namespace mvc2
