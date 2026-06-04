#pragma once
#include "SDK.h"

namespace Features::Champions
{
	namespace Riven
	{
		void OnBuffGainLose( Riot::AIBaseClient* Sender, Riot::Spell::Buff::BuffInstance* Buff, bool IsGain );
		void OnProcessSpell( Riot::AIBaseClient* Sender, Riot::Spell::SpellCastInfo* CastInfo );
		void OnCreateObject( Riot::GameObject* Object );

		void Initialize();
	}
}
