#include "../core/hw/sh4/hle/mvc2_copy_loop.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main()
{
	uint32_t seed = 0xb385183e;
	auto random = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
	unsigned long long cases = 0;
	for (int budget = -16; budget <= 1024; ++budget)
		for (int cycles : {1, 2, 4, 5, 6, 7, 224, 448})
			for (uint32_t count : {0u, 1u, 2u, 3u, 89u, 90u, 91u, 448u, 0x7fffffffu, 0xffffffffu})
			{
				uint32_t expected = 1;
				int left = budget;
				if (cycles == 5 && left > 0 && left <= 448)
					while (count - expected != 0 && left - cycles > 0)
					{
						left -= cycles;
						++expected;
					}
				assert(mvc2::copyLoopBatch(count, budget, cycles) == expected);
				++cases;
			}
	for (unsigned trial = 0; trial < 100000; ++trial)
	{
		alignas(8) std::array<uint8_t, 4096> expected, actual;
		for (auto& byte : expected) byte = random();
		actual = expected;
		const uint32_t source = (random() % 256) * 8;
		const uint32_t destination = (random() % 256) * 8;
		const uint32_t iterations = 1 + random() % 90;
		std::array<uint8_t, 8> last;
		for (uint32_t i = 0; i < iterations; ++i)
		{
			for (unsigned j = 0; j < 8; ++j) last[j] = expected[source + i * 8 + j];
			for (unsigned j = 0; j < 8; ++j) expected[destination + i * 8 + j] = last[j];
		}
		const uint64_t result = mvc2::copyLoopBytes(actual.data() + destination, actual.data() + source, iterations);
		assert(expected == actual);
		assert(std::memcmp(&result, last.data(), 8) == 0);
		++cases;
	}
	for (unsigned trial = 0; trial < 10000; ++trial)
	{
		uint32_t slowCount = trial == 0 ? 0 : random(), fastCount = slowCount;
		uint32_t slowAddress = random(), fastAddress = slowAddress;
		int slowBudget = 1 + random() % 448, fastBudget = slowBudget;
		for (unsigned boundary = 0; boundary < 20; ++boundary)
		{
			bool slowDone = false, fastDone = false;
			for (;;)
			{
				slowBudget -= 5;
				if (slowBudget <= 0) break;
				slowAddress += 8;
				if (--slowCount == 0) { slowDone = true; break; }
			}
			for (;;)
			{
				fastBudget -= 5;
				if (fastBudget <= 0) break;
				const uint32_t n = mvc2::copyLoopBatch(fastCount, fastBudget, 5);
				fastAddress += n * 8;
				fastCount -= n;
				fastBudget -= (n - 1) * 5;
				if (fastCount == 0) { fastDone = true; break; }
			}
			assert(slowCount == fastCount && slowAddress == fastAddress);
			assert(slowBudget == fastBudget && slowDone == fastDone);
			++cases;
			if (slowDone) break;
			slowBudget += 448;
			fastBudget += 448;
			slowAddress += 8;
			fastAddress += 8;
			--slowCount;
			--fastCount;
			if (slowCount == 0) break;
		}
	}
	std::printf("MVC2 copy-loop contract: %llu cases passed\n", cases);
}
