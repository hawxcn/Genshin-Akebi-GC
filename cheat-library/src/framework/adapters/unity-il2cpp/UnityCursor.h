#pragma once
#include <cheat-base/runtime/ICursorController.h>

namespace runtime::unity
{
	// Unity implementation of ICursorController via UnityEngine.Cursor (app::Cursor_*).
	// Engine-level API, reusable across Unity(IL2CPP) games.
	class UnityCursor : public ICursorController
	{
	public:
		void SetVisibility(bool visible) override;
		bool GetVisibility() override;
	};
}
