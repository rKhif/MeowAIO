#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::FlashW
		{
			struct FlashWResult
			{
				Riot::AIBaseClient* Object;
				Riot::Vector3f Position;
			};

			bool GetSpellState();
			std::optional<FlashWResult> GetActionState( Riot::Vector3f Position );
			void InvokeAction( bool Pause, bool FlashOnly );
			float GetTotalRadius();
			float GetTotalDelay();
		}
	}
}
