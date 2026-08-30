#include "hle_patch.h"

#include "cfg/cfg.h"
#include "hw/sh4/sh4_mem.h"

#include <algorithm>
#include <vector>
#include <zlib.h>

namespace {

std::vector<HlePatchSet *>& patchSets()
{
	static std::vector<HlePatchSet *> registry;
	return registry;
}

HlePatchSet *activePatchSet;

bool verifyPatch(HlePatch& patch)
{
	if (patch.verified >= 0)
		return patch.verified == 1;

	const u8 *code = GetMemPtr(patch.vaddr, patch.verify_len);
	if (code == nullptr)
		return false;

	const u32 actual = crc32(0, code, patch.verify_len);
	patch.verified = actual == patch.crc ? 1 : 0;
	if (patch.verified == 0)
		WARN_LOG(DYNAREC, "HLE %08X CRC mismatch: got %08X, expected %08X",
				patch.vaddr, actual, patch.crc);
	else
		NOTICE_LOG(DYNAREC, "HLE %08X verified", patch.vaddr);
	return patch.verified == 1;
}

}

HlePatchSetRegistration::HlePatchSetRegistration(HlePatchSet *patch_set)
{
	hle_RegisterPatchSet(patch_set);
}

void hle_RegisterPatchSet(HlePatchSet *patch_set)
{
	if (patch_set == nullptr || patch_set->product_id == nullptr ||
		patch_set->patches == nullptr || patch_set->patch_count == 0)
		return;
	auto& registry = patchSets();
	if (std::find(registry.begin(), registry.end(), patch_set) == registry.end())
		registry.push_back(patch_set);
}

void hle_Init()
{
	activePatchSet = nullptr;
	if (!cfgLoadBool("perf", "HlePatches", false))
		return;

	for (HlePatchSet *patch_set : patchSets())
	{
		if (settings.content.gameId == patch_set->product_id)
		{
			if (activePatchSet != nullptr)
			{
				WARN_LOG(DYNAREC, "Multiple HLE patch sets match product id %s; using %s",
						settings.content.gameId.c_str(), activePatchSet->name);
				break;
			}
			activePatchSet = patch_set;
		}
	}

	if (activePatchSet == nullptr)
		return;
	for (u32 i = 0; i < activePatchSet->patch_count; ++i)
		activePatchSet->patches[i].verified = -1;
	NOTICE_LOG(DYNAREC, "%s HLE enabled (%u entries)",
			activePatchSet->name, activePatchSet->patch_count);
}

HleNativeFn hle_LookupNative(u32 vaddr)
{
	if (activePatchSet == nullptr)
		return nullptr;
	for (u32 i = 0; i < activePatchSet->patch_count; ++i)
	{
		HlePatch& patch = activePatchSet->patches[i];
		if (patch.vaddr == vaddr)
			return verifyPatch(patch) ? patch.native : nullptr;
	}
	return nullptr;
}
