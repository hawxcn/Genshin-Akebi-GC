#pragma once
#include <string_view>

// Forward-declare the scoped enum (default underlying type int) to avoid pulling
// the heavy renderer.h into this interface header. Concrete adapters (introduced
// in P2) include <cheat-base/render/renderer.h>.
namespace renderer { enum class DXVersion; }

namespace runtime
{
	class ILifecycle;
	class IRuntimeBinding;
	class IHeartbeat;
	class ICursorController;

	// An engine adapter = aggregate facade of the capabilities above.
	// bootstrap (P2) only talks to this interface.
	// Note: placeholder only in P0, unused in P1 (Run() constructs the concrete
	// adapter directly), finalized in P2 when bootstrap + CreateAdapter land.
	class IEngineAdapter
	{
	public:
		virtual ~IEngineAdapter() = default;

		virtual const char*        Name() const = 0;   // "unity-il2cpp" / "unreal"
		virtual ILifecycle&        Lifecycle() = 0;
		virtual IRuntimeBinding&   Binding()   = 0;
		virtual IHeartbeat&        Heartbeat() = 0;
		virtual ICursorController& Cursor()    = 0;
		virtual renderer::DXVersion PreferredBackend() const = 0;
	};

	// Selects a concrete adapter based on the manifest's runtime field (implemented in P2).
	IEngineAdapter* CreateAdapter(std::string_view engineId);
}
