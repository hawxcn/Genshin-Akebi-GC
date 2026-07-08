#pragma once
#include <string_view>

namespace protection
{
	// Engine-agnostic contract for an anti-cheat / anti-analysis countermeasure.
	//
	// The framework owns the *slots* (when countermeasures run); the concrete
	// countermeasure logic is engine/game specific and lives in the adapter or
	// game layer. bootstrap applies the registered protections around runtime
	// startup, each gated by a manifest switch (P2 5.3 / 5.4).
	//
	// Two phases, matching the original Run() ordering:
	//   ApplyPre()  -- before waiting for / touching the game runtime
	//   ApplyPost() -- after the game module is loaded, before binding init
	class IProtection
	{
	public:
		virtual ~IProtection() = default;

		// Stable identifier, also used as the manifest switch key (e.g. "debugger-bypass").
		virtual std::string_view Id() const = 0;

		// Phase before the runtime is loaded. Default: no-op.
		virtual void ApplyPre() {}

		// Phase after the runtime module is loaded. Default: no-op.
		virtual void ApplyPost() {}
	};
}
