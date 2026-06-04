#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::Q
		{
			inline constexpr float radius_t[ 4 ] = { 225.f, 225.f, 325.f, 325.f };
			inline constexpr float delay_t[ 4 ] = { 0.225f, 0.225f, 0.325f, 0.325f };

			bool IsReady( float delta );
			float Radius();
			float Delay();
			float Damage();
			Riot::Spell::SpellDataInst* GetSpell();
		}
	}
}
