#pragma once
#include <cstdint>

namespace mvc2 {

// One iteration is already debited. Extra iterations may run only while the
// next entry debit stays strictly positive, so the JIT still observes every
// scheduler boundary with the same registers and remaining cycle counter.
inline uint32_t cacheLoopBatch(uint32_t count, int remainingCycles, int blockCycles)
{
	if (blockCycles != 4 || remainingCycles <= 0 || remainingCycles > 448)
		return 1;
	const uint32_t capacity = 1 + ((remainingCycles - 1) >> 2);
	// DT with an initial zero iterates 2^32 times, not zero times.
	return count != 0 && count < capacity ? count : capacity;
}

} // namespace mvc2
