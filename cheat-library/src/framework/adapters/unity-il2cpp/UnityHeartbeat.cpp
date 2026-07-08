#include "pch-il2cpp.h"
#include "UnityHeartbeat.h"

#include <il2cpp-appdata.h>
#include <helpers.h>                    // SAFE_BEGIN / SAFE_EEND
#include <cheat-base/HookManager.h>     // HookManager / CALL_ORIGIN

namespace
{
	// GameManager.Update is globally unique, so a single slot suffices. A named hook
	// satisfies HookManager's named-pointer + CALL_ORIGIN constraints.
	std::function<void()> s_tick;

	void GameManager_Update_Hook(app::GameManager* __this, MethodInfo* method)
	{
		SAFE_BEGIN();
		if (s_tick)
			s_tick();
		SAFE_EEND();

		CALL_ORIGIN(GameManager_Update_Hook, __this, method);
	}
}

namespace runtime::unity
{
	void UnityHeartbeat::Install(std::function<void()> tick)
	{
		s_tick = std::move(tick);
		HookManager::install(app::GameManager_Update, GameManager_Update_Hook);
	}
}
