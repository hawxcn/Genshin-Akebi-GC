#pragma once
#include <cheat-base/Patch.h>

// Unity(IL2CPP) convenience macros for the engine-agnostic Patch primitive.
// They resolve "module base + static offset" against UserAssembly.dll via
// il2cppi_get_base_address(). Moved out of cheat-base/Patch.h in P2 5.1 so the
// core stays engine-neutral. Include this (instead of Patch.h directly) from
// Unity feature code that patches by static offset.
//
// il2cppi_get_base_address() is declared in framework/helpers.h; translation
// units using these macros must have it in scope (Unity feature code already
// includes helpers.h via pch-il2cpp.h).

#define OPatch(offset, value) Patch::Install(il2cppi_get_base_address() + offset, value)
#define OUnpatch(offset) Patch::Restore(il2cppi_get_base_address() + offset)
#define TogglePatch(field, targetField, offset, patchBytes) if (field == &targetField) { if (targetField.GetValue()) OPatch(offset, patchBytes); else OUnpatch(offset); return; }
