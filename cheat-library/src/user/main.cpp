#include "pch-il2cpp.h"
#include "main.h"

#include <helpers.h>
#include <resource.h>
#include <bootstrap.h>
#include <cheat-base/cheat/misc/Settings.h>

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

	// Everything engine-specific (protection, adapter, runtime bring-up, backend
	// selection, feature assembly) is driven by framework.manifest.json inside
	// Bootstrap (P2 5.4). Run() only sets up paths, config and logging.
	if (!framework::Bootstrap())
	{
		LOG_ERROR("Framework bootstrap failed; aborting.");
		return;
	}

	LOG_DEBUG("Config path is at %s", (util::GetCurrentPath() / "cfg.json").string().c_str());
}
