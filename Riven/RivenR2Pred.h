#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::R2
		{
			bool GetSpellState();
			bool GetActionState( Riot::Vector3f Position );
			void InvokeAction( bool Pause );
		}
	}
}
