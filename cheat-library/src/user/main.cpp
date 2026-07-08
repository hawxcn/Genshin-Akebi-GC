#include "pch-il2cpp.h"
#include "main.h"

#include <helpers.h>
#include <il2cpp-init.h>
#include <adapters/unity-il2cpp/UnityLifecycle.h>
#include <cheat/cheat.h>
#include <cheat-base/cheat/misc/Settings.h>

#include <tlhelp32.h>
#include <cheat/ILPatternScanner.h>
#include <resource.h>
#include <cheat/DebuggerBypassProtection.h>

void Run(HMODULE* phModule)
{
	ResourceLoader::SetModuleHandle(*phModule);
	util::SetCurrentPath(util::GetModulePath(*phModule));

	// Init config
	config::Initialize((util::GetCurrentPath() / "cfg.json").string());

	// Init logger
	auto& settings = cheat::feature::Settings::GetInstance();
	if (settings.f_FileLogging)
	{
		Logger::PrepareFileLogging((util::GetCurrentPath() / "logs").string());
		Logger::SetLevel(Logger::Level::Trace, Logger::LoggerType::FileLogger);
	}

	if (settings.f_ConsoleLogging)
	{
		Logger::SetLevel(Logger::Level::Debug, Logger::LoggerType::ConsoleLogger);
		il2cppi_new_console();
	}

	runtime::unity::UnityLifecycle lifecycle;

	// Protection now runs through the engine-agnostic protection::IProtection slot
	// (P2 5.3). Behavior is unchanged: same debugger-bypass stubs, same order
	// (Pre -> wait -> Post -> bind). P2 5.4 moves this into bootstrap with the set
	// of protections driven by the manifest.
	cheat::DebuggerBypassProtection debuggerBypass;
	debuggerBypass.ApplyPre();
	lifecycle.WaitForRuntime();
	debuggerBypass.ApplyPost();
	lifecycle.InitBinding();

	cheat::Init();

    LOG_DEBUG("Config path is at %s", (util::GetCurrentPath() / "cfg.json").string().c_str());
}