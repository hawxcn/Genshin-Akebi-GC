#include "pch-il2cpp.h"
#include "UnityBinding.h"

#include <Windows.h>
#include <helpers.h>                    // il2cppi_get_base_address / il2cppi_get_unity_address
#include <cheat-base/ResourceLoader.h>
#include <cheat/ILPatternScanner.h>

namespace runtime::unity
{
	UnityBinding::UnityBinding() = default;
	UnityBinding::~UnityBinding() = default;

	bool UnityBinding::IsReady() const { return m_ready; }
	void UnityBinding::MarkReady() { m_ready = true; }

	uintptr_t UnityBinding::ModuleBase(std::string_view module) const
	{
		if (module.empty() || module == "UserAssembly.dll")
			return il2cppi_get_base_address();
		if (module == "UnityPlayer.dll")
			return il2cppi_get_unity_address();
		return reinterpret_cast<uintptr_t>(GetModuleHandleA(std::string(module).c_str()));
	}

	void* UnityBinding::ResolveFunction(std::string_view symbol)
	{
		// Unity's main path uses compile-time static offsets; name resolution here only
		// covers symbols registered in signatures.json -> best-effort.
		if (!m_scanner)
			m_scanner = std::make_unique<ILPatternScanner>();
		if (!m_sigParsed)
		{
			std::string signatures = ResourceLoader::Load("Signatures", RT_RCDATA);
			m_scanner->ParseSignatureFile(signatures);
			m_sigParsed = true;
		}
		uintptr_t addr = m_scanner->Search("UserAssembly.dll", std::string(symbol));
		return reinterpret_cast<void*>(addr);
	}
}
