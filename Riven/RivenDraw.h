#pragma once
#include <optional>

#include "AIBaseClient.h"
#include "vector3.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Draw
		{
			struct Combo
			{
				Riot::Vector3f Position;
				float TotalRadius, DisplayRadius;
				std::string Text;
			};

			std::optional<Combo> GetFlashCombo();
			std::optional<Combo> GetGapcloseCombo( Riot::AIBaseClient* Object );
			void DrawCombo( Combo Value, std::uint32_t Color, Riot::AIBaseClient* Object );

			void OnDraw();
		}
	}
}
