#pragma once
#include <cheat-base/runtime/IRuntimeBinding.h>
#include <memory>

class ILPatternScanner;

namespace runtime::unity
{
	// Unity(IL2CPP) implementation of IRuntimeBinding.
	class UnityBinding : public IRuntimeBinding
	{
	public:
		UnityBinding();
		~UnityBinding() override;

		bool      IsReady() const override;
		uintptr_t ModuleBase(std::string_view module) const override;
		void*     ResolveFunction(std::string_view symbol) override;
		// ProcessEvent family uses the base default empty impl (Unity does not support it).

		// Set by UnityLifecycle after InitBinding() succeeds.
		void MarkReady();

	private:
		bool m_ready = false;
		bool m_sigParsed = false;                       // lazy-parse flag for ResolveFunction
		std::unique_ptr<ILPatternScanner> m_scanner;    // decision (2): delegate name resolution
	};
}
