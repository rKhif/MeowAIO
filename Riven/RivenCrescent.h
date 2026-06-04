#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Item::Crescent
		{
			bool GetSpellState();
			bool GetActionState();
			void InvokeAction( bool Pause );
		}
	}
}
