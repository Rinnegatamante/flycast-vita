/* CRC-verified, game-specific SH4 HLE patch registry. */
#pragma once

#include "types.h"

using HleNativeFn = void (*)();

struct HlePatch
{
	u32 vaddr;
	u32 verify_len;
	u32 crc;
	HleNativeFn native;
	int verified;
};

struct HlePatchSet
{
	const char *product_id;
	const char *name;
	HlePatch *patches;
	u32 patch_count;
};

class HlePatchSetRegistration
{
public:
	explicit HlePatchSetRegistration(HlePatchSet *patch_set);
};

#define HLE_CONCAT_INNER(a, b) a##b
#define HLE_CONCAT(a, b) HLE_CONCAT_INNER(a, b)
#define HLE_REGISTER_PATCH_SET(patch_set) \
	static HlePatchSetRegistration HLE_CONCAT(hle_patch_registration_, __LINE__)(&(patch_set))

void hle_RegisterPatchSet(HlePatchSet *patch_set);
void hle_Init();
HleNativeFn hle_LookupNative(u32 vaddr);
