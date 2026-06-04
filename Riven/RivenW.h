#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		namespace Spell::W
		{
			bool IsReady( float delta );
			float Radius();
			float Damage();
			Riot::Spell::SpellDataInst* GetSpell();
		}
	}
}
