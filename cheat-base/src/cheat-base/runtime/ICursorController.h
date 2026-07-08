#pragma once

namespace runtime
{
	// Engine-agnostic contract for cursor visibility/lock.
	// Unity: via UnityEngine.Cursor (app::Cursor_*).
	// UE5  : implemented on the UE side (P3, game-specific).
	class ICursorController
	{
	public:
		virtual ~ICursorController() = default;

		// Set cursor visibility. Semantics: visible -> unlocked (free move), invisible -> locked.
		virtual void SetVisibility(bool visible) = 0;

		// Read whether the cursor is currently visible.
		virtual bool GetVisibility() = 0;
	};
}
