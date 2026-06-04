#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Pred::EFlashW
		{
			struct EFlashWResult
			{
				Riot::AIBaseClient* Object;
				Riot::Vector3f Position;
			};

			bool GetSpellState();
			std::optional<EFlashWResult> GetActionState();
			void InvokeAction( bool Pause );
			float GetTotalRadius();
			float GetTotalDelay();
		}
	}
}
