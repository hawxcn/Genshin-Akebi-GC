#include "pch-il2cpp.h"
#include "UnityCursor.h"

#include <il2cpp-appdata.h>

namespace runtime::unity
{
	void UnityCursor::SetVisibility(bool visible)
	{
		app::Cursor_set_visible(visible, nullptr);
		app::Cursor_set_lockState(visible ? app::CursorLockMode__Enum::None
		                                   : app::CursorLockMode__Enum::Locked, nullptr);
	}

	bool UnityCursor::GetVisibility()
	{
		return app::Cursor_get_visible(nullptr);
	}
}
