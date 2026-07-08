#pragma once

#include <Windows.h>

#include <cheat-base/runtime/IEngineAdapter.h>
#include <cheat-base/render/renderer.h>

namespace cheat
{
	// Assemble features and wire engine-specific plumbing (heartbeat, cursor) from
	// the adapter chosen by bootstrap. backend selects the render device (P2 5.4).
	void Init(runtime::IEngineAdapter& adapter, renderer::DXVersion backend);
}