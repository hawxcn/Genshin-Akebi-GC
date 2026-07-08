#include "pch-il2cpp.h"
#include "bootstrap.h"

#include <fstream>
#include <vector>
#include <memory>

#include <nlohmann/json.hpp>

#include <cheat-base/util.h>
#include <cheat-base/runtime/IEngineAdapter.h>
#include <cheat-base/runtime/ILifecycle.h>
#include <cheat-base/protection/IProtection.h>

#include <cheat/cheat.h>
#include <cheat/DebuggerBypassProtection.h>

namespace framework
{
	namespace
	{
		// Parsed subset of framework.manifest.json this host cares about.
		struct Manifest
		{
			std::string runtime;                      // engine adapter id, e.g. "unity-il2cpp"
			std::string render = "auto";              // "auto" | "d3d11" | "d3d12"
			std::vector<std::string> enabledProtections; // protection ids toggled on
		};

		bool LoadManifest(Manifest& out)
		{
			auto path = util::GetCurrentPath() / "framework.manifest.json";
			std::ifstream in(path, std::ios::in);
			if (!in.is_open())
			{
				LOG_ERROR("framework.manifest.json not found next to the DLL (%s). Aborting startup.",
					path.string().c_str());
				return false;
			}

			nlohmann::json j;
			try
			{
				j = nlohmann::json::parse(in);
			}
			catch (const nlohmann::json::parse_error& ex)
			{
				LOG_ERROR("framework.manifest.json parse error at byte %llu: %s", ex.byte, ex.what());
				return false;
			}

			if (!j.contains("runtime") || !j["runtime"].is_string())
			{
				LOG_ERROR("framework.manifest.json missing required string field 'runtime'.");
				return false;
			}
			out.runtime = j["runtime"].get<std::string>();

			if (j.contains("render") && j["render"].is_string())
				out.render = j["render"].get<std::string>();

			// protection: object of { "<id>": bool }. Only enabled ids are collected.
			if (j.contains("protection") && j["protection"].is_object())
			{
				for (auto& [id, enabled] : j["protection"].items())
				{
					if (enabled.is_boolean() && enabled.get<bool>())
						out.enabledProtections.push_back(id);
				}
			}

			return true;
		}

		// Build the game-layer protections this product knows about. Only those
		// whose id is enabled in the manifest are returned. As more countermeasures
		// land they register here.
		std::vector<std::unique_ptr<protection::IProtection>> BuildEnabledProtections(const Manifest& manifest)
		{
			std::vector<std::unique_ptr<protection::IProtection>> all;
			all.push_back(std::make_unique<cheat::DebuggerBypassProtection>());

			std::vector<std::unique_ptr<protection::IProtection>> enabled;
			for (auto& p : all)
			{
				bool on = false;
				for (auto& id : manifest.enabledProtections)
					if (id == p->Id()) { on = true; break; }

				if (on)
				{
					LOG_DEBUG("Protection enabled: %s", std::string(p->Id()).c_str());
					enabled.push_back(std::move(p));
				}
			}
			return enabled;
		}

		renderer::DXVersion ResolveBackend(const Manifest& manifest, runtime::IEngineAdapter& adapter)
		{
			if (manifest.render == "d3d11") return renderer::DXVersion::D3D11;
			if (manifest.render == "d3d12") return renderer::DXVersion::D3D12;
			// "auto" (or anything else): defer to the adapter's preferred backend.
			return adapter.PreferredBackend();
		}
	}

	bool Bootstrap()
	{
		Manifest manifest;
		if (!LoadManifest(manifest))
			return false;

		// Adapter lives for the whole process (heartbeat hook / cursor references
		// point into it). Intentionally never freed.
		runtime::IEngineAdapter* adapter = runtime::CreateAdapter(manifest.runtime);
		if (adapter == nullptr)
			return false;   // CreateAdapter already logged the unknown-id error.

		LOG_DEBUG("Engine adapter: %s", adapter->Name());

		// Protections outlive Bootstrap for the same reason; leak intentionally.
		auto* protections = new std::vector<std::unique_ptr<protection::IProtection>>(
			BuildEnabledProtections(manifest));

		auto& lifecycle = adapter->Lifecycle();

		// Startup order preserved from the old inline Run():
		//   protection.Pre -> wait for runtime -> protection.Post -> bind.
		for (auto& p : *protections) p->ApplyPre();
		lifecycle.WaitForRuntime();
		for (auto& p : *protections) p->ApplyPost();
		lifecycle.InitBinding();

		renderer::DXVersion backend = ResolveBackend(manifest, *adapter);
		cheat::Init(*adapter, backend);
		return true;
	}
}
