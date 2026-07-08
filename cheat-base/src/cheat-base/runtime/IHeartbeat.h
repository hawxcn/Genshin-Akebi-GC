#pragma once
#include <functional>

namespace runtime
{
	// Engine-agnostic contract for the per-frame heartbeat source.
	// The adapter hooks the engine's per-frame function, calls tick once per frame,
	// then lets the original run.
	// Unity: hooks app::GameManager_Update.
	// UE5  : hooks UWorld::Tick / UGameEngine::Tick (P3).
	class IHeartbeat
	{
	public:
		virtual ~IHeartbeat() = default;

		// Install the heartbeat. tick is invoked once per frame.
		// Semantics all adapters must honor:
		//   - tick() is called inside the engine per-frame function, before the original runs;
		//   - tick() is wrapped by the platform's exception guard (e.g. SEH) so a failure
		//     reading game memory inside tick does not crash the game;
		//   - install only once (repeated calls are undefined).
		virtual void Install(std::function<void()> tick) = 0;
	};
}
