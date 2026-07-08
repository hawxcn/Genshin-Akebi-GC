#pragma once

namespace runtime
{
	class IRuntimeBinding;

	// Engine-agnostic contract for "get the runtime ready and complete binding".
	// Note: Heartbeat()/Cursor() live on IEngineAdapter, not here, so ILifecycle
	// keeps a single responsibility = make the runtime ready. Interfaces are
	// drafts until P5.
	class ILifecycle
	{
	public:
		virtual ~ILifecycle() = default;

		// Block until the runtime is initializable.
		// Unity: poll for UserAssembly.dll + fixed Sleep for IL2CPP metadata init.
		virtual void WaitForRuntime() = 0;

		// Resolve all symbols/offsets. Unity: calls the exported init_il2cpp().
		virtual void InitBinding() = 0;

		// Expose the binding instance for upper layers (Patch/Hook) to get base addr etc.
		virtual IRuntimeBinding& Binding() = 0;
	};
}
