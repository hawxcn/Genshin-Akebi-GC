#include "pch-il2cpp.h"
#include "UnityLifecycle.h"

#include <Windows.h>
#include <il2cpp-init.h>
#include <cheat-base/Logger.h>

namespace runtime::unity
{
	void UnityLifecycle::WaitForRuntime()
	{
		while (GetModuleHandle("UserAssembly.dll") == nullptr)
		{
			LOG_DEBUG("UserAssembly.dll isn't initialized, waiting for 2 sec.");
			Sleep(2000);
		}
		// Both original branches slept 15000; unified message (see A-0).
		LOG_DEBUG("Waiting 15 sec for game initialization.");
		Sleep(15000);
	}

	void UnityLifecycle::InitBinding()
	{
		init_il2cpp();
		m_binding.MarkReady();
	}
}
