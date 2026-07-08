#pragma once
#include <cheat-base/protection/IProtection.h>

namespace cheat
{
	// Game-layer protection that adapts the existing DebuggerBypassPre/Post stubs
	// (declared in debugger.h) to the engine-agnostic protection::IProtection slot
	// (P2 5.3). The actual bypass logic stays private/game-specific; this only
	// bridges it onto the framework's protection contract so bootstrap can apply
	// it around runtime startup, gated by a manifest switch.
	class DebuggerBypassProtection : public protection::IProtection
	{
	public:
		std::string_view Id() const override { return "debugger-bypass"; }
		void ApplyPre() override;
		void ApplyPost() override;
	};
}
