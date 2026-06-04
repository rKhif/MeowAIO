#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::FlashQ
		{
			bool GetSpellState();
			bool GetActionState( Riot::Vector3f Position );
			void InvokeAction( bool Pause, bool FlashOnly );
			float GetTotalRadius();
			float GetTotalDelay();
		}
	}
}
