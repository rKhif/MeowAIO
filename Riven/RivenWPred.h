#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::W
		{
			inline Riot::Vector3f SourcePosition = Riot::Vector3f::Zero;

			Riot::AIBaseClient* GetActionState( Riot::Vector3f Position );
			void InvokeAction( bool Pause );
			float GetTotalRadius();
			float GetDisplayRadius();
			bool GetSpellState();
			float GetTotalDelay();
			Riot::AIBaseClient* GetPushState();
		}
	}
}
