#pragma once
#include <cheat-base/runtime/ILifecycle.h>
#include "UnityBinding.h"

namespace runtime::unity
{
	// Unity(IL2CPP) implementation of ILifecycle: wait for UserAssembly + init_il2cpp.
	class UnityLifecycle : public ILifecycle
	{
	public:
		void WaitForRuntime() override;   // wait for UserAssembly.dll + fixed Sleep
		void InitBinding() override;      // init_il2cpp() and mark binding ready
		IRuntimeBinding& Binding() override { return m_binding; }

	private:
		UnityBinding m_binding;
	};
}
