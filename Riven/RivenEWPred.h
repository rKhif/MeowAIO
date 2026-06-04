#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::EW
		{
			inline Riot::Vector3f SourcePosition = Riot::Vector3f::Zero;

			std::optional<std::tuple<Riot::AIBaseClient*, Riot::Vector3f>> GetActionState();
			void InvokeAction( bool Pause );
			float GetTotalRadius();
			float GetDisplayRadius();
			bool GetSpellState();
			float GetTotalDelay();
			Riot::AIBaseClient* GetPushState();
		}
	}
}
