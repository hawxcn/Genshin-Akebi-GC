#pragma once
#include <cheat-base/runtime/IEngineAdapter.h>
#include <cheat-base/render/renderer.h>

#include "UnityLifecycle.h"
#include "UnityHeartbeat.h"
#include "UnityCursor.h"

namespace runtime::unity
{
	// Unity(IL2CPP) engine adapter: aggregates the four Unity capabilities behind
	// the engine-agnostic IEngineAdapter facade that bootstrap talks to (P2 5.4).
	// Owns lifecycle (which owns the binding), heartbeat and cursor for the whole
	// process lifetime -- CreateAdapter() heap-allocates one and bootstrap keeps it.
	class UnityAdapter : public IEngineAdapter
	{
	public:
		const char*        Name() const override { return "unity-il2cpp"; }
		ILifecycle&        Lifecycle() override { return m_lifecycle; }
		IRuntimeBinding&   Binding()   override { return m_lifecycle.Binding(); }
		IHeartbeat&        Heartbeat() override { return m_heartbeat; }
		ICursorController& Cursor()    override { return m_cursor; }
		renderer::DXVersion PreferredBackend() const override { return renderer::DXVersion::D3D11; }

	private:
		UnityLifecycle m_lifecycle;
		UnityHeartbeat m_heartbeat;
		UnityCursor    m_cursor;
	};
}
