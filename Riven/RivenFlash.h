#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::Flash
		{
			inline float LastCastTime = 0.f;
			inline Riot::Vector3f CastPosition = Riot::Vector3f::Zero;

			bool IsReady( float delta );
			void OnCastSpell( Riot::Spell::kSpellSlot Slot, Riot::Vector3f StartPosition );
			float Radius();
		}
	}
}
