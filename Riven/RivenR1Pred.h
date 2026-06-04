#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::R1
		{
			bool GetSpellState();
			bool GetActionState();
			void InvokeAction( bool Pause );
		}
	}
}
