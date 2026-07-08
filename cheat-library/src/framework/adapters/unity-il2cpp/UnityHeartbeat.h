#pragma once
#include <cheat-base/runtime/IHeartbeat.h>

namespace runtime::unity
{
	// Unity implementation of IHeartbeat: hooks app::GameManager_Update, calls tick each frame.
	class UnityHeartbeat : public IHeartbeat
	{
	public:
		void Install(std::function<void()> tick) override;
	};
}
