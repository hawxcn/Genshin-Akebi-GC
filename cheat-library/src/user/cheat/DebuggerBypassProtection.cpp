#include "pch-il2cpp.h"
#include "DebuggerBypassProtection.h"

#include <cheat/debugger.h>

namespace cheat
{
	void DebuggerBypassProtection::ApplyPre()  { DebuggerBypassPre(); }
	void DebuggerBypassProtection::ApplyPost() { DebuggerBypassPost(); }
}
