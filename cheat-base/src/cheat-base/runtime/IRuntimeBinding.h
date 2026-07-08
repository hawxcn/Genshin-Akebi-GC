#pragma once
#include <cstdint>
#include <string_view>

namespace runtime
{
	// Engine-agnostic contract for symbol resolution and module base address.
	// Unity(IL2CPP) relies mainly on compile-time static offsets; ResolveFunction
	// delegates to ILPatternScanner on a best-effort basis. The full value of this
	// interface is realized by the UE5 adapter (P3) and active calls (P4).
	class IRuntimeBinding
	{
	public:
		virtual ~IRuntimeBinding() = default;

		// Whether runtime metadata / object tables are ready.
		// Unity: true after InitBinding() succeeds.
		virtual bool IsReady() const = 0;

		// Base address of a module, e.g. "UserAssembly.dll" / "UnityPlayer.dll". 0 if unknown.
		virtual uintptr_t ModuleBase(std::string_view module) const = 0;

		// Resolve a native function address by name.
		// Unity: delegates to ILPatternScanner (search by signature name).
		//        Limitation: only resolves symbols that have a signature in
		//        signatures.json; app:: functions bound purely by static offset
		//        (no signature) cannot be found by name -> best-effort.
		virtual void* ResolveFunction(std::string_view symbol) = 0;

		// --- Active-call capability: used by UE5 (P4); Unity leaves these empty ---
		virtual bool  SupportsProcessEvent() const { return false; }
		// Call ufunction on uobject; params is an engine-layout parameter buffer.
		virtual void  ProcessEvent(void* uobject, void* ufunction, void* params) { }
		// Find a function by name (UE only; Unity returns nullptr).
		virtual void* FindFunction(void* uobject, std::string_view funcName) { return nullptr; }
	};
}
