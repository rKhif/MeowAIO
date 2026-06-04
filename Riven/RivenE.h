#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::E
		{
			inline Riot::Vector3f Position = Riot::Vector3f::Zero;

			bool IsReady( float delta );
			float Delay();
			float Radius();
			void OnProcessSpell( Riot::Spell::SpellCastInfo* CastInfo );
			Riot::Spell::SpellDataInst* GetSpell();
		}
	}
}
