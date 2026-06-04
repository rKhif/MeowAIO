#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::EFlashQ
		{
			struct EFlashQResult
			{
				Riot::AIBaseClient* Object;
				Riot::Vector3f Position;
			};

			bool GetSpellState();
			std::optional<EFlashQResult> GetActionState();
			void InvokeAction( bool Pause );
			float GetTotalRadius();
			float GetTotalDelay();
		}
	}
}
