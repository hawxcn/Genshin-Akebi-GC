#include "pch-il2cpp.h"
#include "UnityAdapter.h"

namespace runtime
{
	// Factory declared in cheat-base/runtime/IEngineAdapter.h. This product only
	// ships the Unity(IL2CPP) adapter; the default branch is a hard error so a
	// bad manifest runtime id fails loudly instead of silently doing nothing.
	// UE gets its branch in P3.
	IEngineAdapter* CreateAdapter(std::string_view engineId)
	{
		if (engineId == "unity-il2cpp")
			return new runtime::unity::UnityAdapter();

		LOG_ERROR("Unknown engine adapter id in manifest: '%s'.", std::string(engineId).c_str());
		return nullptr;
	}
}
