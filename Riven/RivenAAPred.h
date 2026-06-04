#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::AA
		{
			std::optional<std::tuple<Riot::AIBaseClient*, Riot::Vector3f>> GetPushState();
			Riot::AIBaseClient* GetPushResult();
			void InvokeAction( bool Pause );
			float GetTotalRadius();
			float GetDisplayRadius();
			bool GetSpellState();
			float GetTotalDelay();
		}
	}
}
