#include "../core/hw/sh4/hle/mvc2_cache_loop.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <initializer_list>

int main()
{
	uint32_t seed = 0xa1578639;
	unsigned long long cases = 0;
	const uint32_t edges[] = {0, 1, 2, 3, 110, 111, 112, 113, 448, 0x7fffffffu, 0xffffffffu};
	for (int budget = -16; budget <= 1024; ++budget)
		for (int cycles : {1, 2, 3, 4, 5, 7, 224, 448})
			for (uint32_t count : edges)
			{
				uint32_t expected = 1;
				int remaining = budget;
				if (cycles == 4 && budget > 0 && budget <= 448)
					while (count - expected != 0 && remaining - cycles > 0)
					{
						remaining -= cycles;
						++expected;
					}
				assert(mvc2::cacheLoopBatch(count, budget, cycles) == expected);
				++cases;
			}
	// Compare the full sequence of scheduler-visible register states and cycle
	// residues, including entry debits, across many successive chunks.
	for (unsigned trial = 0; trial < 10000; ++trial)
	{
		auto random = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
		const uint32_t initialCount = trial < 11 ? edges[trial] : random();
		uint32_t slowCount = initialCount, fastCount = initialCount;
		uint32_t slowAddress = random(), fastAddress = slowAddress;
		int slowBudget = 1 + random() % 448, fastBudget = slowBudget;
		for (unsigned boundary = 0; boundary < 20; ++boundary)
		{
			// Both stop immediately before the next scheduler call, after debit.
			bool slowDone = false, fastDone = false;
			for (;;)
			{
				slowBudget -= 4;
				if (slowBudget <= 0) break;
				slowAddress += 32;
				if (--slowCount == 0) { slowDone = true; break; }
			}
			for (;;)
			{
				fastBudget -= 4;
				if (fastBudget <= 0) break;
				const uint32_t n = mvc2::cacheLoopBatch(fastCount, fastBudget, 4);
				fastAddress += n * 32;
				fastCount -= n;
				fastBudget -= (n - 1) * 4;
				if (fastCount == 0) { fastDone = true; break; }
			}
			assert(slowCount == fastCount && slowAddress == fastAddress);
			assert(slowBudget == fastBudget && slowDone == fastDone);
			++cases;
			if (slowDone) break;
			slowBudget += 448;
			fastBudget += 448;
			// The already-debited first iteration after the scheduler resumes.
			slowAddress += 32;
			fastAddress += 32;
			--slowCount;
			--fastCount;
			if (slowCount == 0) break;
		}
	}
	std::printf("MVC2 cache-loop contract: %llu cases passed\n", cases);
}
